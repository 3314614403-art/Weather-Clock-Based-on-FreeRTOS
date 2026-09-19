#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_at.h"
#include "ble_gatt.h"
#include "esp_at_ble.h"

static char captured_command[128];
static uint8_t captured_data[16];
static size_t captured_data_length;

/* 用桩函数替代真实串口驱动，测试时只记录最终生成的 AT 命令。 */
bool esp_at_execute_command(const char *command, uint32_t timeout_ms)
{
    (void)timeout_ms;
    strncpy(captured_command, command, sizeof(captured_command) - 1U);
    captured_command[sizeof(captured_command) - 1U] = '\0';
    return true;
}

bool esp_at_execute_data_command(const char *command,
                                 const uint8_t *data,
                                 size_t data_length,
                                 uint32_t timeout_ms)
{
    (void)timeout_ms;
    assert(data_length <= sizeof(captured_data));
    strncpy(captured_command, command, sizeof(captured_command) - 1U);
    captured_command[sizeof(captured_command) - 1U] = '\0';
    memcpy(captured_data, data, data_length);
    captured_data_length = data_length;
    return true;
}

static void test_environment_encoding(void)
{
    /* 25.34°C / 61.20% 的小端序期望值，以及负温度的补码表示。 */
    const uint8_t expected[] = {0xE6U, 0x09U, 0xE8U, 0x17U};
    const uint8_t negative_expected[] = {0xDAU, 0xFDU, 0x00U, 0x00U};
    ble_environment_value_t environment;
    uint8_t encoded[4];

    ble_environment_from_float(25.34f, 61.20f, &environment);
    assert(ble_environment_encode(&environment, encoded, sizeof(encoded)) == 4U);
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);

    ble_environment_from_float(-5.50f, -1.0f, &environment);
    assert(ble_environment_encode(&environment, encoded, sizeof(encoded)) == 4U);
    assert(memcmp(encoded, negative_expected, sizeof(negative_expected)) == 0);
}

static void test_sample_period(void)
{
    /* 覆盖边界值、越界值和非数字输入。 */
    uint16_t seconds;

    assert(ble_sample_period_decode((const uint8_t *)"5", 1U, &seconds));
    assert(seconds == 5U);
    assert(ble_sample_period_decode((const uint8_t *)"60", 2U, &seconds));
    assert(seconds == 60U);
    assert(!ble_sample_period_decode((const uint8_t *)"0", 1U, &seconds));
    assert(!ble_sample_period_decode((const uint8_t *)"61", 2U, &seconds));
    assert(!ble_sample_period_decode((const uint8_t *)"5s", 2U, &seconds));
}

static void test_urc_parser(void)
{
    /* 覆盖连接、断连以及三种 WRITE 字段布局。 */
    esp_at_ble_event_t event;

    assert(esp_at_ble_parse_event("+BLECONN:0,\"11:22:33:44:55:66\"\r\n", &event));
    assert(event.type == ESP_AT_BLE_EVENT_CONNECTED);
    assert(event.connection_index == 0U);

    assert(esp_at_ble_parse_event("+BLEDISCONN:0,\"11:22:33:44:55:66\"\r\n", &event));
    assert(event.type == ESP_AT_BLE_EVENT_DISCONNECTED);

    assert(esp_at_ble_parse_event("+WRITE:0,1,2,,1,5\r\n", &event));
    assert(event.type == ESP_AT_BLE_EVENT_WRITE);
    assert(event.descriptor_index == 0xFFFFU);
    assert(event.value_length == 1U && event.value[0] == (uint8_t)'5');

    assert(esp_at_ble_parse_event("+WRITE:0,1,2,2,60\r\n", &event));
    assert(event.descriptor_index == 0xFFFFU);
    assert(event.value_length == 2U && memcmp(event.value, "60", 2U) == 0);

    assert(esp_at_ble_parse_event("+WRITE:0,1,1,1,2,01\r\n", &event));
    assert(event.descriptor_index == 1U);
    assert(event.value_length == 2U && memcmp(event.value, "01", 2U) == 0);
}

static void test_commands(void)
{
    /* 验证 GATT 索引、UUID、载荷长度最终被正确拼进 AT 命令。 */
    const uint8_t value[] = {0xE6U, 0x09U, 0xE8U, 0x17U};

    assert(esp_at_ble_start_server("WeatherClock"));
    assert(strcmp(captured_command,
                  "AT+BLEADVDATAEX=\"WeatherClock\",\"FFF0\",,0\r\n") == 0);

    assert(esp_at_ble_set_environment(value, sizeof(value)));
    assert(strcmp(captured_command, "AT+BLEGATTSSETATTR=1,1,,4\r\n") == 0);
    assert(captured_data_length == sizeof(value));
    assert(memcmp(captured_data, value, sizeof(value)) == 0);
}

int main(void)
{
    test_environment_encoding();
    test_sample_period();
    test_urc_parser();
    test_commands();
    puts("BLE protocol tests passed");
    return 0;
}
