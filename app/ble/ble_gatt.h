#ifndef WEATHER_CLOCK_BLE_GATT_H
#define WEATHER_CLOCK_BLE_GATT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 下列索引必须与烧录到 ESP32-C3 ble_data 分区的 gatts_data.csv 一致。 */
#define BLE_WEATHER_SERVICE_INDEX       1U
#define BLE_ENVIRONMENT_CHAR_INDEX      1U
#define BLE_SAMPLE_PERIOD_CHAR_INDEX    2U

#define BLE_WEATHER_SERVICE_UUID        0xFFF0U
#define BLE_ENVIRONMENT_CHAR_UUID       0xFFF1U
#define BLE_SAMPLE_PERIOD_CHAR_UUID     0xFFF2U

#define BLE_SAMPLE_PERIOD_MIN_SECONDS   1U
#define BLE_SAMPLE_PERIOD_MAX_SECONDS   60U

typedef struct
{
    /* 使用百分之一单位避免 GATT 载荷中出现平台相关的 float 格式。 */
    int16_t temperature_centi_celsius;
    uint16_t humidity_centi_percent;
} ble_environment_value_t;

void ble_environment_from_float(float temperature,
                                float humidity,
                                ble_environment_value_t *value);
/* 编码为 4 字节小端序：温度低/高字节，湿度低/高字节。 */
size_t ble_environment_encode(const ble_environment_value_t *value,
                              uint8_t *output,
                              size_t output_size);
/* 解码手机写入的 1~2 位 ASCII 秒数，并检查 1~60 秒范围。 */
bool ble_sample_period_decode(const uint8_t *data,
                              size_t data_length,
                              uint16_t *seconds);

#endif /* WEATHER_CLOCK_BLE_GATT_H */
