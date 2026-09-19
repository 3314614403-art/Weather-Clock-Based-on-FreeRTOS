#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32f4xx.h"
#include "esp_at.h"

#define ESP_AT_DEBUG    1

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

typedef enum
{
    AT_ACK_NONE,
    AT_ACK_OK,
    AT_ACK_ERROR,
    AT_ACK_BUSY,
    AT_ACK_READY,
    AT_ACK_PROMPT,
} at_ack_t;

typedef struct
{
    at_ack_t ack;
    const char *string;
} at_ack_match_t;

static const at_ack_match_t at_ack_matches[] =
{
    {AT_ACK_OK, "OK\r\n"},
    {AT_ACK_ERROR, "ERROR\r\n"},
    {AT_ACK_BUSY, "busy p...\r\n"},
    {AT_ACK_READY, "ready\r\n"},
};

static char rxbuf[1024];
static uint32_t rxlen;
static char linebuf[128];
static uint32_t linelen;
static at_ack_t rxack;
static volatile bool at_command_active;
static SemaphoreHandle_t at_ack_sempahore;
static SemaphoreHandle_t at_command_mutex;
static esp_at_urc_callback_t at_urc_callback;
static void *at_urc_context;

static bool esp_at_write_command(const char *command, uint32_t timeout);
static bool esp_at_wait_boot(uint32_t timeout);
static bool esp_at_wait_ready(uint32_t timeout);
static void esp_at_prepare_receive(void);

static void esp_at_io_init(void)
{
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void esp_at_usart_init(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;

    USART_Init(USART2, &USART_InitStructure);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

static void esp_at_int_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(USART2_IRQn, 5);
}

static void esp_at_lowlevel_init(void)
{
    esp_at_usart_init();
    esp_at_int_init();
    esp_at_io_init();
}

bool esp_at_init(void)
{
    at_ack_sempahore = xSemaphoreCreateBinary();
    configASSERT(at_ack_sempahore);
    at_command_mutex = xSemaphoreCreateMutex();
    configASSERT(at_command_mutex);

    esp_at_lowlevel_init();

    if (!esp_at_wait_boot(3000))
        return false;
    if (!esp_at_write_command("AT+RST\r\n", 2000))
        return false;
    if (!esp_at_wait_ready(5000))
        return false;

    return true;
}

static void esp_at_usart_write_bytes(const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i)
    {
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USART2, data[i]);
    }
    while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET)
        ;
}

static void esp_at_usart_write(const char *data)
{
    esp_at_usart_write_bytes((const uint8_t *)data, strlen(data));
}

static at_ack_t match_internal_ack(const char *str)
{
    for (uint32_t i = 0; i < ARRAY_SIZE(at_ack_matches); i++)
    {
        if (strcmp(str, at_ack_matches[i].string) == 0)
            return at_ack_matches[i].ack;
    }

    return AT_ACK_NONE;
}

static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
    bool acked = xSemaphoreTake(at_ack_sempahore, pdMS_TO_TICKS(timeout)) == pdPASS;
    return acked ? rxack : AT_ACK_NONE;
}

static void esp_at_prepare_receive(void)
{
    taskENTER_CRITICAL();
    rxlen = 0;
    rxbuf[0] = '\0';
    linelen = 0;
    linebuf[0] = '\0';
    rxack = AT_ACK_NONE;
    at_command_active = true;
    taskEXIT_CRITICAL();

    while (xSemaphoreTake(at_ack_sempahore, 0) == pdPASS)
        ;
}

static bool esp_at_wait_ready(uint32_t timeout)
{
    at_ack_t ack;

    if (xSemaphoreTake(at_command_mutex, pdMS_TO_TICKS(timeout)) != pdPASS)
        return false;
    esp_at_prepare_receive();
    ack = esp_at_usart_wait_receive(timeout);
    at_command_active = false;
    xSemaphoreGive(at_command_mutex);
    return ack == AT_ACK_READY;
}

static bool esp_at_write_command(const char *command, uint32_t timeout)
{
    at_ack_t ack;

    if (command == NULL ||
        xSemaphoreTake(at_command_mutex, pdMS_TO_TICKS(timeout)) != pdPASS)
        return false;

#if ESP_AT_DEBUG
    printf("[DEBUG] Send: %s\n", command);
#endif

    esp_at_prepare_receive();
    esp_at_usart_write(command);
    ack = esp_at_usart_wait_receive(timeout);
    at_command_active = false;

#if ESP_AT_DEBUG
    printf("[DEBUG] Response:\n%s\n", rxbuf);
#endif

    xSemaphoreGive(at_command_mutex);
    return ack == AT_ACK_OK;
}

bool esp_at_execute_command(const char *command, uint32_t timeout_ms)
{
    return esp_at_write_command(command, timeout_ms);
}

bool esp_at_execute_data_command(const char *command,
                                 const uint8_t *data,
                                 size_t data_length,
                                 uint32_t timeout_ms)
{
    at_ack_t ack;
    bool success = false;

    if (command == NULL || data == NULL || data_length == 0U ||
        xSemaphoreTake(at_command_mutex, pdMS_TO_TICKS(timeout_ms)) != pdPASS)
        return false;

#if ESP_AT_DEBUG
    printf("[DEBUG] Send data command: %s\n", command);
#endif

    esp_at_prepare_receive();
    esp_at_usart_write(command);
    ack = esp_at_usart_wait_receive(timeout_ms);
    if (ack == AT_ACK_PROMPT)
    {
        while (xSemaphoreTake(at_ack_sempahore, 0) == pdPASS)
            ;
        rxack = AT_ACK_NONE;
        esp_at_usart_write_bytes(data, data_length);
        ack = esp_at_usart_wait_receive(timeout_ms);
        success = (ack == AT_ACK_OK);
    }

    at_command_active = false;
    xSemaphoreGive(at_command_mutex);
    return success;
}

void esp_at_set_urc_callback(esp_at_urc_callback_t callback, void *context)
{
    taskENTER_CRITICAL();
    at_urc_callback = callback;
    at_urc_context = context;
    taskEXIT_CRITICAL();
}

static const char *esp_at_get_response(void)
{
    return rxbuf;
}

static bool esp_at_wait_boot(uint32_t timeout)
{
    for (int t = 0; t < timeout; t += 100)
    {
        if (esp_at_write_command("AT\r\n", 100))
            return true;
    }

    return false;
}

bool esp_at_wifi_init(void)
{
    return esp_at_write_command("AT+CWMODE=1\r\n", 2000);
}

bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    char cmd[192];
    int len;

    if (ssid == NULL || pwd == NULL)
        return false;

    len = snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (len < 0 || (size_t)len >= sizeof(cmd))
        return false;
    if (mac)
        len += snprintf(cmd + len, sizeof(cmd) - (size_t)len, ",\"%s\"", mac);
    if (len < 0 || (size_t)len + 2U >= sizeof(cmd))
        return false;
    snprintf(cmd + len, sizeof(cmd) - (size_t)len, "\r\n");

    return esp_at_write_command(cmd, 5000);
}

static bool parse_cwstate_response(const char *response, esp_wifi_info_t *info)
{
//    AT+CWSTATE?
//    +CWSTATE:2,"Xiaomi Mi MIX 3_5577"

//    OK
	response = strstr(response, "+CWSTATE:");
	if (response == NULL)
		return false;

	int wifi_state;
	if (sscanf(response, "+CWSTATE:%d,\"%63[^\"]", &wifi_state, info->ssid) != 2)
		return false;

	info->connected = (wifi_state == 2);

	return true;
}

static bool parse_cwjap_response(const char *response, esp_wifi_info_t *info)
{
//    AT+CWJAP?
//    +CWJAP:"Xiaomi Mi MIX 3_5577","da:b5:3a:e3:2f:60",9,-48,0,1,3,0,1

//    OK
	response = strstr(response, "+CWJAP:");
	if (response == NULL)
		return false;

	if (sscanf(response, "+CWJAP:\"%63[^\"]\",\"%17[^\"]\",%d,%d", info->ssid, info->bssid, &info->channel, &info->rssi) != 4)
		return false;

	return true;
}

bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (!esp_at_write_command("AT+CWSTATE?\r\n", 2000))
        return false;

    if (!parse_cwstate_response(esp_at_get_response(), info))
        return false;

    if (info->connected == true)
    {
        if (!esp_at_write_command("AT+CWJAP?\r\n", 2000))
            return false;

        if (!parse_cwjap_response(esp_at_get_response(), info))
            return false;
    }

    return true;
}

bool wifi_is_connected(void)
{
    esp_wifi_info_t info;
    if (esp_at_get_wifi_info(&info))
    {
        return info.connected;
    }
    return false;
}

bool esp_at_sntp_init(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8\r\n", 2000))
        return false;

    return true;
}

static uint8_t month_str_to_num(const char *month_str)
{
	const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	for (uint8_t i = 0; i < 12; i++)
	{
		if (strcmp(month_str, months[i]) == 0)
		{
			return i + 1;
		}
	}
	return 0;
}


static uint8_t weekday_str_to_num(const char *weekday_str)
{
	const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
	for (uint8_t i = 0; i < 7; i++) {
		if (strcmp(weekday_str, weekdays[i]) == 0)
		{
			return i + 1;
		}
	}
	return 0;
}

static bool parse_cipsntptime_response(const char *response, esp_date_time_t *date)
{
//	AT+CIPSNTPTIME?
//	+CIPSNTPTIME:Sun Jul 27 14:07:19 2025
//	OK
	char weekday_str[8];
	char month_str[4];
	response = strstr(response, "+CIPSNTPTIME:");
	if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu",
			   weekday_str, month_str,
			   &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
		return false;

	date->weekday = weekday_str_to_num(weekday_str);
	date->month = month_str_to_num(month_str);

	return true;
}

bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?\r\n", 2000))
        return false;

    if (!parse_cipsntptime_response(esp_at_get_response(), date))
        return false;

    return true;
}

const char *esp_at_http_get(const char *url)
{
//    +HTTPCLIENT:261,{"results":[{"location":{"id":"WTEMH46Z5N09","name":"Hefei","country":"CN","path":"Hefei,Hefei,Anhui,China","timezone":"Asia/Shanghai","timezone_offset":"+08:00"},"now":{"text":"Cloudy","code":"4","temperature":"32"},"last_update":"2025-07-26T16:30:00+08:00"}]}

//    OK
    char txbuf[512];
    snprintf(txbuf, sizeof(txbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2\r\n", url);
    bool ret = esp_at_write_command(txbuf, 5000);
    return ret ? esp_at_get_response() : NULL;
}

static bool is_ble_urc(const char *line)
{
    return strncmp(line, "+BLECONN:", 9U) == 0 ||
           strncmp(line, "+BLEDISCONN:", 12U) == 0 ||
           strncmp(line, "+WRITE:", 7U) == 0;
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        at_ack_t ack;
        BaseType_t higher_priority_task_woken = pdFALSE;
        char received = (char)USART_ReceiveData(USART2);

        if (at_command_active && rxlen < sizeof(rxbuf) - 1U)
        {
            rxbuf[rxlen++] = received;
            rxbuf[rxlen] = '\0';
        }

        if (linelen < sizeof(linebuf) - 1U)
            linebuf[linelen++] = received;

        if (received == '>' && linelen == 1U)
        {
            rxack = AT_ACK_PROMPT;
            xSemaphoreGiveFromISR(at_ack_sempahore,
                                  &higher_priority_task_woken);
            linelen = 0;
            linebuf[0] = '\0';
        }
        else if (received == '\n')
        {
            if (linelen < sizeof(linebuf))
            {
                linebuf[linelen] = '\0';
                ack = match_internal_ack(linebuf);
                if (ack != AT_ACK_NONE)
                {
                    rxack = ack;
                    xSemaphoreGiveFromISR(at_ack_sempahore,
                                          &higher_priority_task_woken);
                }
                else if (is_ble_urc(linebuf) && at_urc_callback != NULL)
                    at_urc_callback(linebuf, at_urc_context);
            }

            linelen = 0;
            linebuf[0] = '\0';
        }

        portYIELD_FROM_ISR(higher_priority_task_woken);
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}
