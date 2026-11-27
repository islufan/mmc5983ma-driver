/**
 * @file    mmc5983ma_hal.h
 * @brief   MMC5983MA 驱动与 STM32 HAL 的适配层 —— 接口
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 *
 * @details
 * 只有使用 STM32 HAL 时才需要编译本文件；纯 C 环境可以只用 src/ 下的驱动。
 *
 * 用法：
 * @code
 *   mmc_hal_i2c_t bus;
 *   mmc_hal_i2c_bind(&bus, &hi2c1, 100);
 *   mmc_init(&mag, mmc_hal_i2c_read, mmc_hal_i2c_write, mmc_hal_delay_us, &bus);
 * @endcode
 */
#ifndef MMC5983MA_HAL_H
#define MMC5983MA_HAL_H

#include "mmc5983ma.h"
#include "main.h"   /* 提供 I2C_HandleTypeDef */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief I2C 适配实例，作为 mmc_init() 的 ctx 传入
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;        /**< 已初始化的 I2C 句柄 */
    uint16_t           addr_7bit;   /**< 7 位从机地址，默认 MMC_I2C_ADDR_7BIT */
    uint32_t           timeout_ms;  /**< HAL 传输超时，单位 ms；为 0 时取 100 */
} mmc_hal_i2c_t;

/**
 * @brief  绑定 I2C 句柄与从机地址
 *
 * @param bus        适配实例，由调用方分配
 * @param hi2c       已初始化的 I2C 句柄
 * @param timeout_ms 传输超时（ms），传 0 使用默认 100
 *
 * @note 从机地址固定为 0x30（7 位），如需修改可写 bus->addr_7bit。
 */
void mmc_hal_i2c_bind(mmc_hal_i2c_t *bus, I2C_HandleTypeDef *hi2c, uint32_t timeout_ms);

/**
 * @brief  I2C 读回调，供 mmc_bus_read_fn 使用
 *
 * @param ctx 指向 mmc_hal_i2c_t
 * @param reg 起始寄存器
 * @param buf 输出缓冲区
 * @param len 字节数
 * @return 0 成功，-1 失败
 */
int mmc_hal_i2c_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len);

/**
 * @brief  I2C 写回调，供 mmc_bus_write_fn 使用
 *
 * @param ctx 指向 mmc_hal_i2c_t
 * @param reg 起始寄存器
 * @param buf 待写数据
 * @param len 字节数
 * @return 0 成功，-1 失败
 */
int mmc_hal_i2c_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len);

/**
 * @brief  微秒延时回调，供 mmc_delay_us_fn 使用
 *
 * @param ctx 指向 mmc_hal_i2c_t（可为 NULL）
 * @param us  微秒数
 *
 * @note   这里使用 HAL_Delay 向上取整到毫秒，精度足够本驱动使用。
 */
void mmc_hal_delay_us(void *ctx, uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* MMC5983MA_HAL_H */
