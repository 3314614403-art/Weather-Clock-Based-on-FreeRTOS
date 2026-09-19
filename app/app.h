#ifndef __APP_H__
#define __APP_H__

#include <stdbool.h>
#include <stdint.h>

#define APP_VERSION "v1.0"

/* 创建应用层周期任务并立即执行首轮数据更新。 */
void app_init(void);

/* 设置室内温湿度采样周期，合法范围为 1~60 秒。 */
bool app_set_inner_update_interval(uint16_t seconds);

#endif /* __APP_H__ */
