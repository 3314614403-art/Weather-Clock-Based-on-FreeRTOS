#ifndef __IMAGE_H__
#define __IMAGE_H__

#include <stdint.h>

typedef struct
{
    /* 图片像素采用显示驱动所需格式，data 指向编译进固件的只读数组。 */
    uint16_t width;
    uint16_t height;
    const uint8_t *data;
} image_t;

/* 图片数据本体由同目录下自动生成的 img_*.c / icon_*.c 文件提供。 */
extern const image_t img_meihua;
extern const image_t img_error;
extern const image_t img_wifi;
extern const image_t icon_wenduji;
extern const image_t icon_wifi;
extern const image_t icon_duoyun;
extern const image_t icon_leizhenyu;
extern const image_t icon_qing;
extern const image_t icon_yintian;
extern const image_t icon_yueliang;
extern const image_t icon_zhongxue;
extern const image_t icon_zhongyu;
extern const image_t icon_na;

#endif /* __IMAGE_H__ */
