#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "workqueue.h"
#include "rtc.h"
#include "aht20.h"
#include "esp_at.h"
#include "weather.h"
#include "page.h"
#include "ble.h"
#include "app.h"
#include "local_config.h"

/*
 * 本文件是应用层调度中心：定时采集 RTC、室内传感器和网络天气，
 * 再把有变化的数据送给页面或 BLE。所有间隔均以毫秒表示。
 */
#define MILLISECONDS(x) (x)
#define SECONDS(x)      MILLISECONDS((x) * 1000)
#define MINUTES(x)      SECONDS((x) * 60)
#define HOURS(x)        MINUTES((x) * 60)
#define DAYS(x)          HOURS((x) * 24)

#define TIME_SYNC_INTERVAL          HOURS(1)
#define WIFI_UPDATE_INTERVAL        SECONDS(5)
#define TIME_UPDATE_INTERVAL        SECONDS(1)
#define INNER_UPDATE_INTERVAL       SECONDS(3)
#define OUTDOOR_UPDATE_INTERVAL     MINUTES(1)

/* 预留的主循环事件位；当前实现使用软件定时器，暂未使用这些位。 */
#define MLOOP_EVT_TIME_SYNC         (1 << 0)
#define MLOOP_EVT_WIFI_UPDATE       (1 << 1)
#define MLOOP_EVT_INNER_UPDATE      (1 << 2)
#define MLOOP_EVT_OUTDOOR_UPDATE    (1 << 3)
#define MLOOP_EVT_ALL               (MLOOP_EVT_TIME_SYNC | \
                                     MLOOP_EVT_WIFI_UPDATE | \
                                     MLOOP_EVT_INNER_UPDATE | \
                                     MLOOP_EVT_OUTDOOR_UPDATE)

static TimerHandle_t time_sync_timer;
static TimerHandle_t wifi_update_timer;
static TimerHandle_t time_update_timer;
static TimerHandle_t inner_update_timer;
static TimerHandle_t outdoor_update_timer;

/* 从 ESP32 的 SNTP 时间校准 STM32 RTC；失败时缩短重试周期。 */
static void time_sync(void)
{
    uint32_t restart_sync_delay = TIME_SYNC_INTERVAL;
    rtc_date_time_t rtc_date = { 0 };

    esp_date_time_t esp_date = { 0 };
    if (!esp_at_sntp_get_time(&esp_date))
    {
        printf("[SNTP] get time failed\n");
        restart_sync_delay = SECONDS(1);
        goto err;
    }
    
    if (esp_date.year < 2000)
    {
        printf("[SNTP] invalid date formate\n");
        restart_sync_delay = SECONDS(1);
        goto err;
    }
    
    printf("[SNTP] sync time: %04u-%02u-%02u %02u:%02u:%02u (%d)\n",
        esp_date.year, esp_date.month, esp_date.day,
        esp_date.hour, esp_date.minute, esp_date.second, esp_date.weekday);
    
    rtc_date.year = esp_date.year;
    rtc_date.month = esp_date.month;
    rtc_date.day = esp_date.day;
    rtc_date.hour = esp_date.hour;
    rtc_date.minute = esp_date.minute;
    rtc_date.second = esp_date.second;
    rtc_date.weekday = esp_date.weekday;
    rtc_set_time(&rtc_date);
    
err:
    /*
     * time_sync_timer 是单次定时器。改变周期同时会重新激活它：
     * 成功后每小时校时，失败则 1 秒后快速重试。
     */
    xTimerChangePeriod(time_sync_timer, pdMS_TO_TICKS(restart_sync_delay), 0);
}

/* 轮询 Wi-Fi 状态，仅在连接状态改变时刷新 SSID 区域。 */
static void wifi_update(void)
{
    /* 静态保存上一次快照，跨调用比较以避免无意义重绘。 */
    static esp_wifi_info_t last_info = { 0 };

    esp_wifi_info_t info = { 0 };
    if (!esp_at_get_wifi_info(&info))
    {
        printf("[AT] wifi info get failed\n");
        return;
    }
    
    if (memcmp(&info, &last_info, sizeof(esp_wifi_info_t)) == 0)
    {
        return;
    }
    
    if (last_info.connected == info.connected)
    {
        return;
    }
    
    if (info.connected)
    {
        printf("[WIFI] connected to %s\n", info.ssid);
        printf("[WIFI] SSID: %s, BSSID: %s, Channel: %d, RSSI: %d\n",
                info.ssid, info.bssid, info.channel, info.rssi);
        main_page_redraw_wifi_ssid(info.ssid);
    }
    else
    {
        printf("[WIFI] disconnected from %s\n", last_info.ssid);
        main_page_redraw_wifi_ssid("wifi lost");
    }
    
    memcpy(&last_info, &info, sizeof(esp_wifi_info_t));
}

/* 每秒读取 RTC；秒数变化时更新时间和日期区域。 */
static void time_update(void)
{
    static rtc_date_time_t last_date = { 0 };
    
    rtc_date_time_t date;
    rtc_get_time(&date);
    
    if (date.year < 2020)
    {
        return;
    }
    
    if (memcmp(&date, &last_date, sizeof(rtc_date_time_t)) == 0)
    {
        return;
    }
    
    memcpy(&last_date, &date, sizeof(rtc_date_time_t));
    main_page_redraw_time(&date);
    main_page_redraw_date(&date);
}

/* 启动 AHT20 测量，读取室内温湿度，并同步到屏幕和 BLE。 */
static void inner_update(void)
{
    static float last_temperature, last_humidity;
    
    if (!aht20_start_measurement())
    {
        printf("[AHT20] start measurement failed\n");
        return;
    }
    
    if (!aht20_wait_for_measurement())
    {
        printf("[AHT20] wait for measurement failed\n");
        return;
    }
    
    float temperature = 0.0f, humidity = 0.0f;
    
    if (!aht20_read_measurement(&temperature, &humidity))
    {
        printf("[AHT20] read measurement failed\n");
        return;
    }
    
    if (temperature == last_temperature && humidity == last_humidity)
    {
        return;
    }
    
    last_temperature = temperature;
    last_humidity = humidity;
    
    printf("[AHT20] Temperature: %.1f, Humidity: %.1f\n", temperature, humidity);
    main_page_redraw_inner_temperature(temperature);
    main_page_redraw_inner_humidity(humidity);
    /* 发布函数采用非阻塞入队；队列满或 BLE 未初始化时允许丢弃本次数据。 */
    (void)ble_publish_environment(temperature, humidity);
}

/* 请求心知天气接口，并在数据发生变化时更新室外天气卡片。 */
static void outdoor_update(void)
{
    static weather_info_t last_weather = { 0 };
    
    weather_info_t weather = { 0 };
    const char *weather_url = WEATHER_URL;
    /* 返回缓冲区由 ESP-AT 驱动管理，本函数只在当前调用内读取。 */
    const char *weather_http_response = esp_at_http_get(weather_url);
    if (weather_http_response == NULL)
    {
        printf("[WEATHER] http error\n");
        return;
    }
    
    if (!parse_seniverse_response(weather_http_response, &weather))
    {
        printf("[WEATHER] parse failed\n");
        return;
    }
    
    if (memcmp(&last_weather, &weather, sizeof(weather_info_t)) == 0)
    {
        return;
    }
    
    memcpy(&last_weather, &weather, sizeof(weather_info_t));
    printf("[WEATHER] %s, %s, %.1f\n", weather.city, weather.weather, weather.temperature);
    
    main_page_redraw_outdoor_temperature(weather.temperature);
    main_page_redraw_outdoor_weather_icon(weather.weather_code);
}

typedef void (*app_job_t)(void);

/* 将无参数的应用任务适配为 workqueue 所需的 void (*)(void *) 形式。 */
static void app_work(void *param)
{
    app_job_t job = (app_job_t)param;
    job();
}

static void work_timer_cb(TimerHandle_t timer)
{
    /* 定时器 ID 被用来保存真正要执行的函数指针。 */
    app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
    /* 网络和传感器操作可能阻塞，不能直接放在 FreeRTOS 定时器任务中。 */
    workqueue_run(app_work, job);
}

static void app_timer_cb(TimerHandle_t timer)
{
    app_job_t job = (app_job_t)pvTimerGetTimerID(timer);
    /* RTC 读取很短，可直接在定时器回调上下文执行。 */
    job();
}

void app_init(void)
{
    /*
     * xTimerCreate 的 ID 参数保存对应更新函数：
     * 周期定时器负责稳定刷新，校时定时器则由 time_sync() 自行安排下次触发。
     */
    time_update_timer = xTimerCreate("time update", pdMS_TO_TICKS(TIME_UPDATE_INTERVAL), pdTRUE, time_update, app_timer_cb);
    time_sync_timer = xTimerCreate("time sync", pdMS_TO_TICKS(200), pdFALSE, time_sync, work_timer_cb);
    wifi_update_timer = xTimerCreate("wifi update", pdMS_TO_TICKS(WIFI_UPDATE_INTERVAL), pdTRUE, wifi_update, work_timer_cb);
    inner_update_timer = xTimerCreate("inner upadte", pdMS_TO_TICKS(INNER_UPDATE_INTERVAL), pdTRUE, inner_update, work_timer_cb);
    outdoor_update_timer = xTimerCreate("outdoor update", pdMS_TO_TICKS(OUTDOOR_UPDATE_INTERVAL), pdTRUE, outdoor_update, work_timer_cb);

    /* 启动时立即取得第一批数据，无需等待各自的首个定时周期。 */
    workqueue_run(app_work, time_sync);
    workqueue_run(app_work, wifi_update);
    workqueue_run(app_work, inner_update);
    workqueue_run(app_work, outdoor_update);
    
    xTimerStart(time_update_timer, 0);
    xTimerStart(time_sync_timer, 0);
    xTimerStart(wifi_update_timer, 0);
    xTimerStart(inner_update_timer, 0);
    xTimerStart(outdoor_update_timer, 0);
}

bool app_set_inner_update_interval(uint16_t seconds)
{
    /* BLE 侧也会校验范围；这里再次校验，保护应用层公开接口。 */
    if (inner_update_timer == NULL ||
        seconds < 1U || seconds > 60U)
        return false;

    /* 改变活动定时器的周期后，新周期从当前时刻重新计时。 */
    return xTimerChangePeriod(inner_update_timer,
                              pdMS_TO_TICKS(SECONDS(seconds)),
                              0) == pdPASS;
}
