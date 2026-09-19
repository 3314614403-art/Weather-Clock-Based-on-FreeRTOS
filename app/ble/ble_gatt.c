#include "ble_gatt.h"

static int16_t clamp_temperature(float value)
{
    float scaled;

    /* IEEE-754 中 NaN 是唯一与自身不相等的值。 */
    if (value != value)
        return 0;
    if (value > 327.67f)
        return 32767;
    if (value < -327.68f)
        return -32768;

    /* 乘 100 转为 0.01 摄氏度单位，并以四舍五入而非截断方式取整。 */
    scaled = value * 100.0f;
    return (int16_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static uint16_t clamp_humidity(float value)
{
    /* 湿度无符号表示，最终限制在 0.00%~100.00%。 */
    if (value != value || value <= 0.0f)
        return 0;
    if (value >= 100.0f)
        return 10000;
    return (uint16_t)(value * 100.0f + 0.5f);
}

void ble_environment_from_float(float temperature,
                                float humidity,
                                ble_environment_value_t *value)
{
    if (value == NULL)
        return;

    value->temperature_centi_celsius = clamp_temperature(temperature);
    value->humidity_centi_percent = clamp_humidity(humidity);
}

size_t ble_environment_encode(const ble_environment_value_t *value,
                              uint8_t *output,
                              size_t output_size)
{
    uint16_t temperature;

    if (value == NULL || output == NULL || output_size < 4U)
        return 0;

    /* 转为 uint16_t 只为便于移位，负数的二进制补码位模式保持不变。 */
    temperature = (uint16_t)value->temperature_centi_celsius;
    output[0] = (uint8_t)temperature;
    output[1] = (uint8_t)(temperature >> 8);
    output[2] = (uint8_t)value->humidity_centi_percent;
    output[3] = (uint8_t)(value->humidity_centi_percent >> 8);
    return 4U;
}

bool ble_sample_period_decode(const uint8_t *data,
                              size_t data_length,
                              uint16_t *seconds)
{
    uint16_t value = 0;
    size_t i;

    if (data == NULL || seconds == NULL || data_length == 0U || data_length > 2U)
        return false;

    /* 特征值传输 ASCII（如 "5"、"60"），逐位累积为整数。 */
    for (i = 0; i < data_length; ++i)
    {
        if (data[i] < (uint8_t)'0' || data[i] > (uint8_t)'9')
            return false;
        value = (uint16_t)(value * 10U + (uint16_t)(data[i] - (uint8_t)'0'));
    }

    if (value < BLE_SAMPLE_PERIOD_MIN_SECONDS ||
        value > BLE_SAMPLE_PERIOD_MAX_SECONDS)
        return false;

    *seconds = value;
    return true;
}
