#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "st7789.h"
#include "ui.h"
#include "font.h"
#include "image.h"

/* UI 对外操作在队列中用动作类型区分，真正的 SPI 绘制只在 ui_func() 中发生。 */
typedef enum
{
    UI_ACTION_FILL_COLOR,
    UI_ACTION_WRITE_STRING,
    UI_ACTION_DRAW_IMAGE,
} ui_action_t;

typedef struct
{
    ui_action_t action;
    /* 每种动作只使用 union 中与 action 对应的成员，减少队列消息体积。 */
    union
    {
        struct
        {
            uint16_t x;
            uint16_t y;
            uint16_t width;
            uint16_t height;
            uint16_t color;
        } fill_color;
        struct
        {
            uint16_t x;
            uint16_t y;
            const char *str;
            uint16_t color;
            uint16_t bg_color;
            const font_t *font;
        } write_string;
        struct
        {
            uint16_t x;
            uint16_t y;
            const image_t *image;
        } draw_image;
    };
} ui_message_t;

static QueueHandle_t ui_queue;

/*
 * 屏幕专用消费者任务。
 * 所有 ST7789 操作在这里串行执行，调用者无需直接争用 SPI/屏幕资源。
 */
static void ui_func(void *param)
{
    ui_message_t msg;
    
    st7789_init();
    
    while (1)
    {
        xQueueReceive(ui_queue, &msg, portMAX_DELAY);
        
        switch (msg.action)
        {
        case UI_ACTION_FILL_COLOR:
            st7789_fill_color(msg.fill_color.x, msg.fill_color.y,
                              msg.fill_color.width, msg.fill_color.height,
                              msg.fill_color.color);
            break;
        case UI_ACTION_WRITE_STRING:
            st7789_write_string(msg.write_string.x, msg.write_string.y,
                                msg.write_string.str,
                                msg.write_string.color, msg.write_string.bg_color,
                                msg.write_string.font);
            /* 字符串副本由 ui_write_string() 分配，完成绘制后在消费端释放。 */
            vPortFree((void*)msg.write_string.str);
            break;
        case UI_ACTION_DRAW_IMAGE:
            st7789_draw_image(msg.draw_image.x, msg.draw_image.y,
                              msg.draw_image.image);
            break;
        default:
            printf("Unknown UI action: %d\n", msg.action);
            break;
        }
    }
}

void ui_init(void)
{
    /* 16 个消息可吸收一次页面重绘产生的短时突发操作。 */
    ui_queue = xQueueCreate(16, sizeof(ui_message_t));
    configASSERT(ui_queue);
    xTaskCreate(ui_func, "ui", 1024, NULL, 8, NULL);
}

void ui_fill_color(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    ui_message_t msg;
    msg.action = UI_ACTION_FILL_COLOR;
    msg.fill_color.x = x;
    msg.fill_color.y = y;
    msg.fill_color.width = width;
    msg.fill_color.height = height;
    msg.fill_color.color = color;
    
    /* 阻塞投递确保绘图命令不被静默丢弃，并保持调用顺序。 */
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}

void ui_write_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg_color, const font_t *font)
{
    /*
     * 队列只复制消息结构，不复制 str 指向的内容。因此先制作堆副本，
     * 即使调用者传入栈上临时字符串，UI 任务稍后读取时数据仍然有效。
     */
    char *pstr = pvPortMalloc(strlen(str) + 1);
    if (pstr == NULL)
    {
        printf("ui write string malloc failed: %s", str);
        return;
    }
    strcpy(pstr, str);
    
    ui_message_t msg;
    msg.action = UI_ACTION_WRITE_STRING;
    msg.write_string.x = x;
    msg.write_string.y = y;
    msg.write_string.str = pstr;
    msg.write_string.color = color;
    msg.write_string.bg_color = bg_color;
    msg.write_string.font = font;
    
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}

void ui_draw_image(uint16_t x, uint16_t y, const image_t *image)
{
    ui_message_t msg;
    msg.action = UI_ACTION_DRAW_IMAGE;
    msg.draw_image.x = x;
    msg.draw_image.y = y;
    msg.draw_image.image = image;
    
    /* 图片资源是全局只读常量，队列中保存指针即可，无需复制像素数据。 */
    xQueueSend(ui_queue, &msg, portMAX_DELAY);
}
