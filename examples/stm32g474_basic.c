/**
 * @file    stm32g474_basic.c
 * @brief   STM32G474 使用示例：初始化 MMC5983MA 并通过串口输出磁场数据
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 */

#include <stdio.h>
#include <stdbool.h>

#include "mmc5983ma.h"
#include "mmc5983ma_hal.h"

/*
 * 接线（按你的板子改）：
 *   I2C1_SCL -> PB8
 *   I2C1_SDA -> PB9
 *   器件地址 -> 0x30（7 位）
 */

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

static mmc_t          g_mag;
static mmc_hal_i2c_t  g_bus;

/* printf 重定向到 USART1 */
int fputc(int ch, FILE *f)
{
    (void)f;
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart1, &c, 1, 100);
    return ch;
}

/**
 * @brief  初始化 I2C 适配层与 MMC5983MA
 * @note   内部已包含软复位后的 10 ms 等待；失败时通过串口打印错误码
 */
void app_mag_start(void)
{
    mmc_hal_i2c_bind(&g_bus, &hi2c1, 100);

    mmc_status_t st = mmc_init(&g_mag, mmc_hal_i2c_read, mmc_hal_i2c_write,
                               mmc_hal_delay_us, &g_bus);
    if (st != MMC_OK) {
        printf("mmc_init failed: %d\r\n", (int)st);
        return;
    }

    /* 400 Hz 带宽：单次测量 2 ms，适合快速姿态更新 */
    (void)mmc_set_bandwidth(&g_mag, MMC_BW_2MS_400HZ);
    printf("mmc5983ma ready\r\n");
}

/**
 * @brief  触发并读取一次磁场，串口打印三轴 Gauss 与温度
 * @note   在主循环中以 10 ms 周期调用即可
 */
void app_mag_poll(void)
{
    mmc_field_t field;
    if (mmc_measure(&g_mag, &field, 50) != MMC_OK) {
        return;
    }

    float temp = 0.0f;
    (void)mmc_read_temperature(&g_mag, &temp);

    printf("X %+8.4f G  Y %+8.4f G  Z %+8.4f G  T %.1f C\r\n",
           field.x_g, field.y_g, field.z_g, temp);
}

/* 在 main() 中调用：
 *
 *   app_mag_start();
 *   while (1) {
 *       app_mag_poll();
 *       HAL_Delay(10);
 *   }
 */
