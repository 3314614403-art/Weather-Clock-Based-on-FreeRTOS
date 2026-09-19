#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "workqueue.h"
#include "esp_at.h"
#include "app.h"
#include "ble.h"
#include "ble_gatt.h"
#include "esp_at_ble.h"

#define BLE_QUEUE_LENGTH 8U
#define BLE_TASK_STACK_DEPTH 768U
#define BLE_TASK_PRIORITY 6U
#define BLE_URC_MAX_LENGTH 96U

/* BLE 任务统一消费传感器数据和 ESP-AT 主动上报事件（URC）。 */
typedef enum
{
    BLE_MESSAGE_ENVIRONMENT,
    BLE_MESSAGE_URC,
} ble_message_type_t;

typedef struct
{
    ble_message_type_t type;
    /* 消息类型决定 union 中哪个成员有效，从而控制队列元素大小。 */
    union
    {
        ble_environment_value_t environment;
        char urc[BLE_URC_MAX_LENGTH];
    } data;
} ble_message_t;

static QueueHandle_t ble_queue;
static TaskHandle_t ble_task_handle;
static volatile bool ble_connected;
static uint8_t ble_connection_index;

/* 断连后的重新广播会发送 AT 命令，因此放到串行工作队列中执行。 */
static void restart_advertising_work(void *parameter)
{
    (void)parameter;
    if (!esp_at_ble_start_advertising())
        printf("[BLE] restart advertising failed\n");
}

static void ble_urc_received(const char *line, void *context)
{
    ble_message_t message;
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    if (ble_queue == NULL || line == NULL)
        return;

    /*
     * 回调运行在 ESP-AT 接收/中断路径：这里只复制事件并快速入队，
     * 解析和 AT 命令处理留给普通任务，避免阻塞底层接收。
     */
    memset(&message, 0, sizeof(message));
    message.type = BLE_MESSAGE_URC;
    strncpy(message.data.urc, line, sizeof(message.data.urc) - 1U);
    (void)xQueueSendFromISR(ble_queue, &message, &higher_priority_task_woken);
    /* 若 BLE 任务优先级更高，立即请求一次上下文切换以降低事件延迟。 */
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* 处理手机写入 0xFFF2 特征值的事件，用它调整室内采样周期。 */
static void handle_write_event(const esp_at_ble_event_t *event)
{
    uint16_t seconds;

    /* 只接受特征值本身的写入；0xFFFF 表示此次写入不针对描述符。 */
    if (event->service_index != BLE_WEATHER_SERVICE_INDEX ||
        event->characteristic_index != BLE_SAMPLE_PERIOD_CHAR_INDEX ||
        event->descriptor_index != 0xFFFFU)
        return;

    if (!ble_sample_period_decode(event->value, event->value_length, &seconds))
    {
        printf("[BLE] invalid sample period\n");
        return;
    }

    if (app_set_inner_update_interval(seconds))
        printf("[BLE] sample period: %u seconds\n", (unsigned int)seconds);
    else
        printf("[BLE] sample period update failed\n");
}

static void handle_urc(const char *line)
{
    esp_at_ble_event_t event;

    if (!esp_at_ble_parse_event(line, &event))
        return;

    switch (event.type)
    {
    case ESP_AT_BLE_EVENT_CONNECTED:
        ble_connected = true;
        ble_connection_index = event.connection_index;
        printf("[BLE] connected: %u\n",
               (unsigned int)ble_connection_index);
        break;

    case ESP_AT_BLE_EVENT_DISCONNECTED:
        ble_connected = false;
        printf("[BLE] disconnected: %u\n",
               (unsigned int)event.connection_index);
        /* 部分 ESP-AT 固件断连后不会自动恢复广播，显式重新启动。 */
        workqueue_run(restart_advertising_work, NULL);
        break;

    case ESP_AT_BLE_EVENT_WRITE:
        handle_write_event(&event);
        break;

    default:
        break;
    }
}

static void handle_environment_work(void *parameter)
{
    ble_environment_value_t *environment = parameter;
    uint8_t value[4];
    size_t value_length;

    if (environment == NULL)
        return;

    /* GATT 载荷固定为 4 字节小端序：温度 int16 + 湿度 uint16。 */
    value_length = ble_environment_encode(environment, value, sizeof(value));
    if (value_length == 0U)
        goto exit;

    /* 始终更新可读属性；有客户端连接时再额外发送 Notify。 */
    if (!esp_at_ble_set_environment(value, value_length))
    {
        printf("[BLE] set environment failed\n");
        goto exit;
    }

    if (ble_connected &&
        !esp_at_ble_notify_environment(ble_connection_index, value, value_length))
        printf("[BLE] environment notify failed\n");

exit:
    /* 参数副本由 queue_environment_work() 分配，所有路径都在此释放。 */
    vPortFree(environment);
}

static void queue_environment_work(const ble_environment_value_t *environment)
{
    ble_environment_value_t *copy;

    /* 工作队列稍后才执行，因此不能直接传递 ble_task() 栈上的消息地址。 */
    copy = pvPortMalloc(sizeof(*copy));
    if (copy == NULL)
    {
        printf("[BLE] environment work allocation failed\n");
        return;
    }

    *copy = *environment;
    workqueue_run(handle_environment_work, copy);
}

static void ble_task(void *parameter)
{
    ble_message_t message;

    (void)parameter;
    for (;;)
    {
        if (xQueueReceive(ble_queue, &message, portMAX_DELAY) != pdPASS)
            continue;

        if (message.type == BLE_MESSAGE_ENVIRONMENT)
            queue_environment_work(&message.data.environment);
        else if (message.type == BLE_MESSAGE_URC)
            handle_urc(message.data.urc);
    }
}

bool ble_init(void)
{
    /* 支持幂等调用，避免重复创建任务和注册回调。 */
    if (ble_queue != NULL)
        return true;

    ble_queue = xQueueCreate(BLE_QUEUE_LENGTH, sizeof(ble_message_t));
    if (ble_queue == NULL)
        return false;

    if (xTaskCreate(ble_task, "ble", BLE_TASK_STACK_DEPTH, NULL,
                    BLE_TASK_PRIORITY, &ble_task_handle) != pdPASS)
    {
        vQueueDelete(ble_queue);
        ble_queue = NULL;
        return false;
    }

    /* 注册回调后，ESP-AT 接收到连接、断连和写入 URC 都会进入 BLE 队列。 */
    esp_at_set_urc_callback(ble_urc_received, NULL);
    if (!esp_at_ble_start_server("WeatherClock"))
    {
        esp_at_set_urc_callback(NULL, NULL);
        vTaskDelete(ble_task_handle);
        ble_task_handle = NULL;
        vQueueDelete(ble_queue);
        ble_queue = NULL;
        return false;
    }

    printf("[BLE] advertising as WeatherClock\n");
    return true;
}

bool ble_publish_environment(float temperature, float humidity)
{
    ble_message_t message;

    if (ble_queue == NULL)
        return false;

    memset(&message, 0, sizeof(message));
    message.type = BLE_MESSAGE_ENVIRONMENT;
    ble_environment_from_float(temperature, humidity, &message.data.environment);
    /* 0 tick 表示绝不阻塞传感器更新流程；拥塞时允许丢弃中间样本。 */
    return xQueueSend(ble_queue, &message, 0) == pdPASS;
}

bool ble_is_connected(void)
{
    return ble_connected;
}
