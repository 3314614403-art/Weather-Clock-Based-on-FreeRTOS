#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "weather.h"

/*
 * 从心知天气 HTTP 响应中提取页面需要的少量字段。
 * 这是针对固定响应格式的轻量解析器，不是通用 JSON 解析器；调用前应将 info 清零。
 */
bool parse_seniverse_response(const char *response, weather_info_t *info)
{
	/* 先定位 results，降低误匹配 HTTP 头或其他同名字段的概率。 */
	response = strstr(response, "\"results\":");
	if (response == NULL)
		return false;
	
	const char *location_response = strstr(response, "\"location\":");
	if (location_response == NULL)
		return false;
	
	const char *loaction_name_response = strstr(location_response, "\"name\":");
	if (loaction_name_response)
	{
		/* 扫描宽度小于目标数组容量，为末尾 '\0' 保留空间。 */
		sscanf(loaction_name_response, "\"name\": \"%31[^\"]\"", info->city);
	}
	
	const char *loaction_path_response = strstr(location_response, "\"path\":");
	if (loaction_path_response)
	{
		sscanf(loaction_path_response, "\"path\": \"%128[^\"]\"", info->loaction);
	}
	
	const char *now_response = strstr(response, "\"now\":");
	if (now_response == NULL)
		return false;
	
	const char *now_text_response = strstr(now_response, "\"text\":");
	if (now_text_response)
	{
		sscanf(now_text_response, "\"text\": \"%15[^\"]\"", info->weather);
	}
	
	const char *now_code_response = strstr(now_response, "\"code\":");
	if (now_code_response)
	{
		sscanf(now_code_response, "\"code\": \"%d\"", &info->weather_code);
	}
	
	char temperature_str[16] = { 0 };
	const char *now_temperature_response = strstr(now_response, "\"temperature\":");
	if (now_temperature_response)
	{
		if (sscanf(now_temperature_response, "\"temperature\": \"%15[^\"]\"", temperature_str) == 1)
			/* API 用字符串表示温度，atof 将其转换成页面使用的浮点数。 */
			info->temperature = atof(temperature_str);
	}
	
	return true;
}
