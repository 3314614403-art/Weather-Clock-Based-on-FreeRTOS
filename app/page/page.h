#ifndef __PAGE_H__
#define __PAGE_H__

#include "rtc.h"

/* 完整页面绘制接口。 */
void welcome_page_display(void);
void error_page_display(const char *msg);
void wifi_page_display(void);
void main_page_display(void);

/* 主页面局部重绘接口，用于传感器或网络数据变化时减少刷新面积。 */
void main_page_redraw_wifi_ssid(const char *ssid);
void main_page_redraw_time(rtc_date_time_t *time);
void main_page_redraw_date(rtc_date_time_t *date);
void main_page_redraw_inner_temperature(float temperature);
void main_page_redraw_inner_humidity(float humidity);
void main_page_redraw_outdoor_city(const char *city);
void main_page_redraw_outdoor_temperature(float temperature);
void main_page_redraw_outdoor_weather_icon(const int code);

#endif /* __PAGE_H__ */
