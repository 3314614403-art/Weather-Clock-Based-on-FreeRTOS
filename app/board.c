#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "tim_delay.h"
#include "console.h"
#include "rtc.h"
#include "aht20.h"

/*
 * 调度器启动前的芯片级初始化。
 * 这里只打开后续驱动会使用的外设时钟，并选择 32.768 kHz LSE 作为 RTC 时钟。
 */
void board_lowlevel_init(void)
{
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

    /* 访问 RTC/LSE 所在的备份域前必须显式解除写保护。 */
    PWR_BackupAccessCmd(ENABLE);
    RCC_LSEConfig(RCC_LSE_ON);
    /* RTC 依赖 LSE，因此必须等晶振稳定后再选择时钟源。 */
    while(RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET);
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
}

/* 初始化依赖正常系统时基或 RTOS 环境的板级驱动。 */
void board_init(void)
{
    tim_delay_init();
    console_init();
    printf("[SYS] Build Date: %s %s\n", __DATE__, __TIME__);
    
    rtc_init();
    aht20_init();
}

int fputc(int ch, FILE *f)
{
    /* 重定向 printf：逐字节阻塞发送到 USART1 调试串口。 */
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    return ch;
}

void vAssertCalled(const char *file, int line)
{
    /* 断言后关闭中断并停在故障现场，便于调试器检查调用栈。 */
    portDISABLE_INTERRUPTS();
    printf("Assert Called: %s(%d)\n", file, line);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    /* FreeRTOS 栈溢出钩子：打印出错任务名，再进入统一断言处理。 */
    printf("Stack Overflowed: %s\n", pcTaskName);
    configASSERT(0);
}

void vApplicationMallocFailedHook(void)
{
    /* FreeRTOS 堆耗尽通常意味着任务栈或消息缓冲区配置不足。 */
    printf("Malloc Failed\n");
    configASSERT(0);
}
