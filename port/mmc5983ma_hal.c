/**
 * @file    mmc5983ma_hal.c
 * @brief   MMC5983MA 驱动与 STM32 HAL 的适配层 —— 实现
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 *
 * @details
 * 使用 HAL 的 I2C 内存读写接口：
 *   读：HAL_I2C_Mem_Read(&hi2c, addr, reg, 8bit, buf, len, timeout)
 *   写：HAL_I2C_Mem_Write(&hi2c, addr, reg, 8bit, buf, len, timeout)
 * 其中 addr 为 8 位格式（7 位地址左移 1 位）。
 */
#include "mmc5983ma_hal.h"

void mmc_hal_i2c_bind(mmc_hal_i2c_t *bus, I2C_HandleTypeDef *hi2c, uint32_t timeout_ms)
{
    if (bus == NULL) {
        return;
    }
    bus->hi2c       = hi2c;
    bus->addr_7bit  = (uint16_t)MMC_I2C_ADDR_7BIT;
    bus->timeout_ms = (timeout_ms == 0u) ? 100u : timeout_ms;
}

int mmc_hal_i2c_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    mmc_hal_i2c_t *bus = (mmc_hal_i2c_t *)ctx;
    if (bus == NULL || bus->hi2c == NULL || buf == NULL) {
        return -1;
    }

    uint16_t dev_addr = (uint16_t)(bus->addr_7bit << 1);
    if (HAL_I2C_Mem_Read(bus->hi2c, dev_addr, reg, I2C_MEMADD_SIZE_8BIT,
                         buf, len, bus->timeout_ms) != HAL_OK) {
        return -1;
    }
    return 0;
}

int mmc_hal_i2c_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    mmc_hal_i2c_t *bus = (mmc_hal_i2c_t *)ctx;
    if (bus == NULL || bus->hi2c == NULL || buf == NULL) {
        return -1;
    }

    uint16_t dev_addr = (uint16_t)(bus->addr_7bit << 1);
    if (HAL_I2C_Mem_Write(bus->hi2c, dev_addr, reg, I2C_MEMADD_SIZE_8BIT,
                          (uint8_t *)buf, len, bus->timeout_ms) != HAL_OK) {
        return -1;
    }
    return 0;
}

void mmc_hal_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    /* 向上取整到毫秒；本驱动的等待都在毫秒量级 */
    uint32_t ms = (us + 999u) / 1000u;
    HAL_Delay(ms);
}
