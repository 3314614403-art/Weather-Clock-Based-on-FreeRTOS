#ifndef __WEAHTER_H__
#define __WEAHTER_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
	/* 以下字符串数组的大小与 weather.c 中 sscanf 的最大扫描宽度相匹配。 */
	char city[32];
	/* API 返回的完整行政区路径（字段名沿用原项目的 loaction 拼写）。 */
	char loaction[128];
	char weather[16];
	int weather_code;
	float temperature;
} weather_info_t;

/* 解析心知天气 now.json 响应；关键节点缺失时返回 false。 */
bool parse_seniverse_response(const char *response, weather_info_t *info);

#endif /* __WEAHTER_H__ */
