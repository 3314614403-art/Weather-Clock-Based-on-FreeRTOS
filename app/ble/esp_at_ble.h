#ifndef WEATHER_CLOCK_ESP_AT_BLE_H
#define WEATHER_CLOCK_ESP_AT_BLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    /* 将 ESP-AT 文本 URC 归一化为业务层容易处理的事件类型。 */
    ESP_AT_BLE_EVENT_NONE,
    ESP_AT_BLE_EVENT_CONNECTED,
    ESP_AT_BLE_EVENT_DISCONNECTED,
    ESP_AT_BLE_EVENT_WRITE,
} esp_at_ble_event_type_t;

typedef struct
{
    esp_at_ble_event_type_t type;
    uint8_t connection_index;
    uint16_t service_index;
    uint16_t characteristic_index;
    uint16_t descriptor_index;
    /* value 指向原始 URC 行内部，不拥有内存，使用期不能超过该行。 */
    const uint8_t *value;
    uint16_t value_length;
} esp_at_ble_event_t;

/* 配置 GATT Server、设备名和广播数据；GATT 表需预先烧录。 */
bool esp_at_ble_start_server(const char *device_name);
bool esp_at_ble_start_advertising(void);
bool esp_at_ble_set_environment(const uint8_t *value, size_t value_length);
bool esp_at_ble_notify_environment(uint8_t connection_index,
                                   const uint8_t *value,
                                   size_t value_length);
/* 解析 +BLECONN、+BLEDISCONN 和 +WRITE 三类 ESP-AT URC。 */
bool esp_at_ble_parse_event(const char *line, esp_at_ble_event_t *event);

#endif /* WEATHER_CLOCK_ESP_AT_BLE_H */
