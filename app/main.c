#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "app.h"
#include "ui.h"
#include "wifi.h"
#include "ble.h"
#include "page.h"

extern void board_lowlevel_init(void);
extern void board_init(void);

/*
 * 系统初始化任务。
 *
 * 把可能阻塞的外设初始化和联网过程放到调度器启动后的任务中执行，
 * 这样 wifi_wait_connect() 等函数可以安全地使用 vTaskDelay()。
 */
static void main_init(void *param)
{
    /* 先准备本地硬件和 UI，再进行可能耗时较长的网络连接。 */
    board_init();
    ui_init();

    welcome_page_display();

    wifi_init();
    wifi_page_display();
    wifi_wait_connect();

    main_page_display();
    app_init();

    /* BLE 是附加功能：初始化失败不妨碍天气时钟的核心功能继续运行。 */
    if (!ble_init())
        printf("[BLE] init failed; weather clock continues without BLE\n");

    /* 初始化只执行一次，完成后删除自身以释放任务资源。 */
    vTaskDelete(NULL);
}

int main(void)
{
    /* 此阶段调度器尚未运行，只做时钟等最底层、无需 RTOS 的准备。 */
    board_lowlevel_init();

    /* 工作队列必须早于任何向它投递任务的模块创建。 */
    workqueue_init();

    /* 初始化任务优先级较高，先完成设备启动流程，再进入正常运行状态。 */
    xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);

    /* 正常情况下该调用永不返回，之后所有工作都由 FreeRTOS 任务完成。 */
    vTaskStartScheduler();

    while (1)
    {
        ; // code should not run here
    }
}
