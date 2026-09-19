#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "workqueue.h"

typedef struct
{
    /* 待执行函数及其不透明参数一起通过队列传递。 */
    work_t work;
    void *param;
} work_message_t;

static QueueHandle_t work_msg_queue;

/*
 * 唯一的工作线程，串行执行所有投递任务。
 * 这样可把阻塞操作移出软件定时器/中断回调，也避免多个任务同时访问 ESP-AT。
 */
static void work_func(void *param)
{
    work_message_t msg;
    
    while (1)
    {
        xQueueReceive(work_msg_queue, &msg, portMAX_DELAY);
        /* 队列中没有空任务；调用者必须保证函数指针及参数生命周期有效。 */
        msg.work(msg.param);
    }
}

void workqueue_init(void)
{
    /* 最多缓存 16 项；队列满时生产者会在 workqueue_run() 中等待。 */
    work_msg_queue = xQueueCreate(16, sizeof(work_message_t));
    configASSERT(work_msg_queue);
    xTaskCreate(work_func, "workqueue", 1024, NULL, 5, NULL);
}

void workqueue_run(work_t work, void *param)
{
    configASSERT(work_msg_queue);
    work_message_t msg = { work, param };
    /* portMAX_DELAY 提供可靠投递，但调用位置不能是 ISR。 */
    xQueueSend(work_msg_queue, &msg, portMAX_DELAY);
}
