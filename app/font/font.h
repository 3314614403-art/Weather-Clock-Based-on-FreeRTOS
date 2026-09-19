#ifndef __FONT_H
#define __FONT_H

// !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~

#include <stdint.h>

typedef struct
{
    /* UTF-8 汉字及其点阵数据；每个字体文件只收录项目实际使用的汉字。 */
    const char *name;
    const uint8_t *model;
} font_chinese_t;

typedef struct
{
    /* ASCII 点阵表和字符映射共同决定每个字形在 model 中的位置。 */
    const uint8_t *ascii_model;
    const char *ascii_map;
    const font_chinese_t *chinese;
    uint16_t size;
} font_t;

/* 字形数据本体由同目录下自动生成的 font*.c 文件提供。 */
extern const font_t font16_maple;
extern const font_t font20_maple_bold;
extern const font_t font24_maple_semibold;
extern const font_t font24_maple_bold;
extern const font_t font32_maple_bold;
extern const font_t font54_maple_bold;
extern const font_t font54_maple_semibold;
extern const font_t font64_maple_extrabold;
extern const font_t font76_maple_extrabold;

#endif /* __FONT_H */
