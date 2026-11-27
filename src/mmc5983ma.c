/**
 * @file    mmc5983ma.c
 * @brief   MMC5983MA 三轴磁力计驱动 —— 实现
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 *
 * @details
 * 实现约定：
 *   - 所有寄存器访问都经 bus_read / bus_write，失败立即返回错误码；
 *   - 18 位数据由 Xout0/Xout1/XYZout2 三处拼接，零点为 131072；
 *   - 驱动只在复位与轮询时使用 delay_us 回调，不做长时间阻塞；
 *   - 不打印任何调试信息。
 *
 * 详细接口说明见 mmc5983ma.h。
 */
#include "mmc5983ma.h"

/* ================================================================== */
/* 内部实现                                                            */
/* ================================================================== */

/**
 * @brief  封装读回调，把非 0 返回值统一转换为 MMC_ERR_BUS
 * @param  dev 设备实例
 * @param  reg 起始寄存器
 * @param  buf 输出缓冲
 * @param  len 字节数
 * @return MMC_OK 或 MMC_ERR_*
 */
static mmc_status_t bus_read(mmc_t *dev, uint8_t reg, uint8_t *buf, uint16_t len)
{
    if (dev == NULL || dev->read == NULL) {
        return MMC_ERR_PARAM;
    }
    return (dev->read(dev->ctx, reg, buf, len) == 0) ? MMC_OK : MMC_ERR_BUS;
}

/**
 * @brief  封装写回调，把非 0 返回值统一转换为 MMC_ERR_BUS
 * @param  dev 设备实例
 * @param  reg 起始寄存器
 * @param  buf 待写数据
 * @param  len 字节数
 * @return MMC_OK 或 MMC_ERR_*
 */
static mmc_status_t bus_write(mmc_t *dev, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    if (dev == NULL || dev->write == NULL) {
        return MMC_ERR_PARAM;
    }
    return (dev->write(dev->ctx, reg, buf, len) == 0) ? MMC_OK : MMC_ERR_BUS;
}

/**
 * @brief  把 18 位无符号码值换算为 Gauss
 * @param  raw 18 位码值（0 ~ 262143）
 * @return 磁感应强度，单位 Gauss
 */
static float raw_to_gauss(uint32_t raw)
{
    return ((float)raw - (float)MMC_RAW_ZERO) / MMC_COUNTS_PER_GAUSS;
}

/**
 * @brief  按影子副本修改 CTRL1 的位域并写回
 *
 * @param  dev  设备实例
 * @param  mask 需要修改的位域掩码
 * @param  val  新值（自动与 mask 对齐）
 * @return MMC_OK 或 MMC_ERR_*
 *
 * @note   CTRL1 是只写寄存器，读回值不可靠，因此用 dev->ctrl1 作为软件缓存，
 *         避免读-改-写把无关位（尤其是 SW_RST）带入。
 */
static mmc_status_t write_ctrl1(mmc_t *dev, uint8_t mask, uint8_t val)
{
    uint8_t new_val = (uint8_t)((dev->ctrl1 & (uint8_t)~mask) | (val & mask));
    mmc_status_t st = bus_write(dev, MMC_REG_CTRL1, &new_val, 1);
    if (st == MMC_OK) {
        dev->ctrl1 = new_val;
    }
    return st;
}

/* ================================================================== */
/* 公共接口实现                                                        */
/* ================================================================== */

mmc_status_t mmc_read_reg(mmc_t *dev, uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return MMC_ERR_PARAM;
    }
    return bus_read(dev, reg, value, 1);
}

mmc_status_t mmc_write_reg(mmc_t *dev, uint8_t reg, uint8_t value)
{
    return bus_write(dev, reg, &value, 1);
}

mmc_status_t mmc_soft_reset(mmc_t *dev)
{
    if (dev == NULL || dev->delay_us == NULL) {
        return MMC_ERR_PARAM;
    }

    mmc_status_t st = mmc_write_reg(dev, MMC_REG_CTRL1, MMC_CTRL1_SW_RST);
    if (st != MMC_OK) {
        return st;
    }

    /* 手册：软复位等同于上电，需要 10 ms 才能访问寄存器 */
    dev->delay_us(dev->ctx, 10000u);
    dev->ready = false;
    dev->ctrl1 = 0;                              /* 复位后 CTRL1 回到默认值 */
    dev->bw = (uint8_t)MMC_BW_8MS_100HZ;
    return MMC_OK;
}

mmc_status_t mmc_set_bandwidth(mmc_t *dev, mmc_bw_t bw)
{
    if (dev == NULL) {
        return MMC_ERR_PARAM;
    }
    if ((unsigned)bw > (unsigned)MMC_BW_05MS_800HZ) {
        return MMC_ERR_PARAM;
    }

    /* BW[1:0] 位于 CTRL1 的 bit[1:0]，其余位（尤其是 SW_RST）必须保持为 0 */
    mmc_status_t st = write_ctrl1(dev,
                                  (uint8_t)(MMC_CTRL1_BW1 | MMC_CTRL1_BW0),
                                  (uint8_t)bw);
    if (st == MMC_OK) {
        dev->bw = (uint8_t)bw;
    }
    return st;
}

mmc_status_t mmc_trigger_measurement(mmc_t *dev)
{
    if (dev == NULL) {
        return MMC_ERR_PARAM;
    }
    return mmc_write_reg(dev, MMC_REG_CTRL0, MMC_CTRL0_TM_M);
}

mmc_status_t mmc_trigger_temperature(mmc_t *dev)
{
    if (dev == NULL) {
        return MMC_ERR_PARAM;
    }
    return mmc_write_reg(dev, MMC_REG_CTRL0, MMC_CTRL0_TM_T);
}

mmc_status_t mmc_measurement_ready(mmc_t *dev, bool *ready)
{
    if (dev == NULL || ready == NULL) {
        return MMC_ERR_PARAM;
    }

    uint8_t status = 0;
    mmc_status_t st = bus_read(dev, MMC_REG_STATUS, &status, 1);
    if (st != MMC_OK) {
        return st;
    }

    *ready = (status & MMC_STATUS_MEAS_M_DONE) != 0;
    return MMC_OK;
}

mmc_status_t mmc_read_raw(mmc_t *dev, mmc_raw_t *out)
{
    if (dev == NULL || out == NULL) {
        return MMC_ERR_PARAM;
    }
    if (!dev->ready) {
        return MMC_ERR_STATE;
    }

    /* 0x00~0x06 连续 7 字节：X/Y/Z 各两个字节 + XYZout2 */
    uint8_t buf[7] = {0};
    mmc_status_t st = bus_read(dev, MMC_REG_XOUT0, buf, sizeof(buf));
    if (st != MMC_OK) {
        return st;
    }

    uint8_t xyz2 = buf[6];

    /* 18 位无符号：高 8 位在高地址之前的低位字节中，低 2 位在 XYZout2 */
    out->x = ((uint32_t)buf[0] << 10) | ((uint32_t)buf[1] << 2) | (uint32_t)((xyz2 >> 6) & 0x03u);
    out->y = ((uint32_t)buf[2] << 10) | ((uint32_t)buf[3] << 2) | (uint32_t)((xyz2 >> 4) & 0x03u);
    out->z = ((uint32_t)buf[4] << 10) | ((uint32_t)buf[5] << 2) | (uint32_t)((xyz2 >> 2) & 0x03u);
    return MMC_OK;
}

mmc_status_t mmc_read_field(mmc_t *dev, mmc_field_t *out)
{
    if (dev == NULL || out == NULL) {
        return MMC_ERR_PARAM;
    }

    mmc_raw_t raw;
    mmc_status_t st = mmc_read_raw(dev, &raw);
    if (st != MMC_OK) {
        return st;
    }

    out->x_g = raw_to_gauss(raw.x);
    out->y_g = raw_to_gauss(raw.y);
    out->z_g = raw_to_gauss(raw.z);
    return MMC_OK;
}

mmc_status_t mmc_read_temperature(mmc_t *dev, float *deg_c)
{
    if (dev == NULL || deg_c == NULL) {
        return MMC_ERR_PARAM;
    }

    uint8_t tout = 0;
    mmc_status_t st = bus_read(dev, MMC_REG_TOUT, &tout, 1);
    if (st != MMC_OK) {
        return st;
    }

    /* 8 位无符号，0.8 ℃/LSB，0x00 对应 -75 ℃ */
    *deg_c = (float)tout * 0.8f - 75.0f;
    return MMC_OK;
}

mmc_status_t mmc_measure(mmc_t *dev, mmc_field_t *out, uint32_t timeout_ms)
{
    if (dev == NULL || out == NULL) {
        return MMC_ERR_PARAM;
    }
    if (!dev->ready) {
        return MMC_ERR_STATE;
    }

    mmc_status_t st = mmc_trigger_measurement(dev);
    if (st != MMC_OK) {
        return st;
    }

    /* 轮询间隔 1 ms；100 Hz 带宽下单次测量约 8 ms */
    for (uint32_t elapsed = 0; elapsed < timeout_ms; elapsed++) {
        bool ready = false;
        st = mmc_measurement_ready(dev, &ready);
        if (st != MMC_OK) {
            return st;
        }
        if (ready) {
            return mmc_read_field(dev, out);
        }
        dev->delay_us(dev->ctx, 1000u);
    }

    return MMC_ERR_TIMEOUT;
}

mmc_status_t mmc_set_coil(mmc_t *dev)
{
    if (dev == NULL) {
        return MMC_ERR_PARAM;
    }
    return mmc_write_reg(dev, MMC_REG_CTRL0, MMC_CTRL0_SET);
}

mmc_status_t mmc_reset_coil(mmc_t *dev)
{
    if (dev == NULL) {
        return MMC_ERR_PARAM;
    }
    return mmc_write_reg(dev, MMC_REG_CTRL0, MMC_CTRL0_RESET);
}

mmc_status_t mmc_init(mmc_t *dev, mmc_bus_read_fn rd, mmc_bus_write_fn wr,
                      mmc_delay_us_fn delay_us, void *ctx)
{
    if (dev == NULL || rd == NULL || wr == NULL || delay_us == NULL) {
        return MMC_ERR_PARAM;
    }

    dev->read     = rd;
    dev->write    = wr;
    dev->delay_us = delay_us;
    dev->ctx      = ctx;
    dev->ctrl1    = 0;
    dev->bw       = (uint8_t)MMC_BW_8MS_100HZ;
    dev->ready    = false;

    mmc_status_t st = mmc_soft_reset(dev);
    if (st != MMC_OK) {
        return st;
    }

    uint8_t id = 0;
    st = mmc_read_reg(dev, MMC_REG_PRODUCT_ID, &id);
    if (st != MMC_OK) {
        return st;
    }
    if (id != MMC_PRODUCT_ID_VALUE) {
        return MMC_ERR_ID;
    }

    st = mmc_set_bandwidth(dev, MMC_BW_8MS_100HZ);
    if (st != MMC_OK) {
        return st;
    }

    dev->ready = true;
    return MMC_OK;
}
