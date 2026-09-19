#ifndef WEATHER_CLOCK_BLE_H
#define WEATHER_CLOCK_BLE_H

#include <stdbool.h>

/* 创建 BLE 消息任务并让 ESP32-C3 以 WeatherClock 名称开始广播。 */
bool ble_init(void);

/* 非阻塞发布最新温湿度；尚未初始化或消息队列已满时返回 false。 */
bool ble_publish_environment(float temperature, float humidity);

/* 返回最近一次 ESP-AT 连接/断开事件记录的连接状态。 */
bool ble_is_connected(void);

#endif /* WEATHER_CLOCK_BLE_H */
