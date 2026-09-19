#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_at.h"
#include "ble_gatt.h"
#include "esp_at_ble.h"

#define BLE_AT_TIMEOUT_MS 2000U
/* ESP-AT 的空描述符字段在内部事件结构中用 0xFFFF 表示。 */
#define BLE_NO_DESCRIPTOR 0xFFFFU

/* 从 *cursor 读取十进制无符号数，并把游标推进到数字之后。 */
static bool parse_uint(const char **cursor, uint32_t *value)
{
    char *end;
    unsigned long parsed;

    if (cursor == NULL || *cursor == NULL || value == NULL ||
        **cursor < '0' || **cursor > '9')
        return false;

    parsed = strtoul(*cursor, &end, 10);
    if (end == *cursor)
        return false;

    *value = (uint32_t)parsed;
    *cursor = end;
    return true;
}

static bool consume_comma(const char **cursor)
{
    if (cursor == NULL || *cursor == NULL || **cursor != ',')
        return false;
    ++(*cursor);
    return true;
}

bool esp_at_ble_start_advertising(void)
{
    return esp_at_execute_command("AT+BLEADVSTART\r\n", BLE_AT_TIMEOUT_MS);
}

bool esp_at_ble_start_server(const char *device_name)
{
    char command[96];
    const char *cursor;

    if (device_name == NULL || strlen(device_name) > 32U)
        return false;

    /* 拒绝会逃逸 AT 字符串或提前结束命令行的设备名字符。 */
    for (cursor = device_name; *cursor != '\0'; ++cursor)
    {
        if (*cursor == '"' || *cursor == '\r' || *cursor == '\n')
            return false;
    }

    /* 2 = BLE Server 模式；SYSMSG=4 开启连接状态主动上报。 */
    if (!esp_at_execute_command("AT+BLEINIT=2\r\n", BLE_AT_TIMEOUT_MS))
        return false;
    if (!esp_at_execute_command("AT+SYSMSG=4\r\n", BLE_AT_TIMEOUT_MS))
        return false;

    snprintf(command, sizeof(command), "AT+BLENAME=\"%s\"\r\n", device_name);
    if (!esp_at_execute_command(command, BLE_AT_TIMEOUT_MS))
        return false;

    /* 根据已烧录的 gatts_data.csv 创建并启动服务表。 */
    if (!esp_at_execute_command("AT+BLEGATTSSRVCRE\r\n", BLE_AT_TIMEOUT_MS))
        return false;
    if (!esp_at_execute_command("AT+BLEGATTSSRVSTART\r\n", BLE_AT_TIMEOUT_MS))
        return false;

    /* 广播包包含设备名和 16 位天气服务 UUID，方便手机扫描过滤。 */
    snprintf(command, sizeof(command),
             "AT+BLEADVDATAEX=\"%s\",\"%04X\",,0\r\n",
             device_name, (unsigned int)BLE_WEATHER_SERVICE_UUID);
    return esp_at_execute_command(command, BLE_AT_TIMEOUT_MS);
}

bool esp_at_ble_set_environment(const uint8_t *value, size_t value_length)
{
    char command[64];

    if (value == NULL || value_length == 0U || value_length > 4U)
        return false;

    snprintf(command, sizeof(command),
             "AT+BLEGATTSSETATTR=%u,%u,,%u\r\n",
             BLE_WEATHER_SERVICE_INDEX,
             BLE_ENVIRONMENT_CHAR_INDEX,
             (unsigned int)value_length);
    /* ESP-AT 先接收命令，再进入数据接收阶段读取原始二进制载荷。 */
    return esp_at_execute_data_command(command, value, value_length,
                                       BLE_AT_TIMEOUT_MS);
}

bool esp_at_ble_notify_environment(uint8_t connection_index,
                                   const uint8_t *value,
                                   size_t value_length)
{
    char command[64];

    if (value == NULL || value_length == 0U || value_length > 4U)
        return false;

    snprintf(command, sizeof(command),
             "AT+BLEGATTSNTFY=%u,%u,%u,%u\r\n",
             (unsigned int)connection_index,
             BLE_WEATHER_SERVICE_INDEX,
             BLE_ENVIRONMENT_CHAR_INDEX,
             (unsigned int)value_length);
    return esp_at_execute_data_command(command, value, value_length,
                                       BLE_AT_TIMEOUT_MS);
}

bool esp_at_ble_parse_event(const char *line, esp_at_ble_event_t *event)
{
    const char *cursor;
    uint32_t connection;
    uint32_t service;
    uint32_t characteristic;
    uint32_t descriptor = BLE_NO_DESCRIPTOR;
    uint32_t length;

    if (line == NULL || event == NULL)
        return false;

    memset(event, 0, sizeof(*event));
    event->descriptor_index = BLE_NO_DESCRIPTOR;

    if (strncmp(line, "+BLECONN:", 9U) == 0)
    {
        cursor = line + 9U;
        if (!parse_uint(&cursor, &connection) || connection > 255U)
            return false;
        event->type = ESP_AT_BLE_EVENT_CONNECTED;
        event->connection_index = (uint8_t)connection;
        return true;
    }

    if (strncmp(line, "+BLEDISCONN:", 12U) == 0)
    {
        cursor = line + 12U;
        if (!parse_uint(&cursor, &connection) || connection > 255U)
            return false;
        event->type = ESP_AT_BLE_EVENT_DISCONNECTED;
        event->connection_index = (uint8_t)connection;
        return true;
    }

    /* 非本模块关心的 URC 直接返回 false，由其他业务自行处理。 */
    if (strncmp(line, "+WRITE:", 7U) != 0)
        return false;

    cursor = line + 7U;
    if (!parse_uint(&cursor, &connection) || !consume_comma(&cursor) ||
        !parse_uint(&cursor, &service) || !consume_comma(&cursor) ||
        !parse_uint(&cursor, &characteristic) || !consume_comma(&cursor))
        return false;

    /*
     * ESP-AT 的 WRITE URC 有两种形式：
     *   +WRITE:conn,srv,char,,len,value       （无描述符）
     *   +WRITE:conn,srv,char,desc,len,value   （有描述符）
     * 部分版本还省略无描述符位置，形成 char,len,value。
     */
    if (*cursor == ',')
    {
        ++cursor;
        if (!parse_uint(&cursor, &length) || !consume_comma(&cursor))
            return false;
    }
    else
    {
        uint32_t first_field;
        size_t remaining;

        if (!parse_uint(&cursor, &first_field) || !consume_comma(&cursor))
            return false;

        /* 剩余字段中是否还有逗号，用来区分 len 与 desc。 */
        remaining = strcspn(cursor, "\r\n");
        if (memchr(cursor, ',', remaining) == NULL)
        {
            length = first_field;
        }
        else
        {
            descriptor = first_field;
            if (!parse_uint(&cursor, &length) || !consume_comma(&cursor))
                return false;
        }
    }
    if (connection > 255U || service > 65535U ||
        characteristic > 65535U || descriptor > 65535U || length > 65535U)
        return false;
    /* 声明长度不能超过当前 URC 行中实际剩余的数据。 */
    if (strcspn(cursor, "\r\n") < length)
        return false;

    event->type = ESP_AT_BLE_EVENT_WRITE;
    event->connection_index = (uint8_t)connection;
    event->service_index = (uint16_t)service;
    event->characteristic_index = (uint16_t)characteristic;
    event->descriptor_index = (uint16_t)descriptor;
    event->value = (const uint8_t *)cursor;
    event->value_length = (uint16_t)length;
    return true;
}
