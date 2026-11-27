/**
 * @file    mmc5983ma.h
 * @brief   MMC5983MA 三轴磁力计驱动 —— 公共接口
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 *
 * @details
 * 面向嵌入式裸机环境，只依赖 C99 标准库。总线（I2C）通过
 * mmc_bus_read_fn / mmc_bus_write_fn 回调注入，延时通过 mmc_delay_us_fn 回调注入，
 * 因此同一份源码可以运行在 STM32、ESP32 或 PC 上，也便于用 mock 总线做单元测试。
 *
 * 典型用法：
 * @code
 *   mmc_t mag;
 *   if (mmc_init(&mag, i2c_read, i2c_write, delay_us, &bus) == MMC_OK) {
 *       mmc_field_t f;
 *       if (mmc_measure(&mag, &f, 50) == MMC_OK) {
 *           // f.x_g / f.y_g / f.z_g，单位 Gauss
 *       }
 *   }
 * @endcode
 *
 * @warning 本驱动不是线程安全的。多任务共用同一个 mmc_t 实例时，调用方需自行加锁。
 */
#ifndef MMC5983MA_H
#define MMC5983MA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>   /* NULL */

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* 器件常量                                                            */
/* ================================================================== */
#define MMC_I2C_ADDR_7BIT      0x30u  /**< 7 位从机地址 */
#define MMC_PRODUCT_ID_VALUE   0x30u  /**< Product ID1（0x2F）的期望值 */

#define MMC_RAW_ZERO           131072u /**< 18 位输出的零点（2^17），对应 0 Gauss */
#define MMC_COUNTS_PER_GAUSS   16384.0f /**< 灵敏度，单位 counts/Gauss（±8 G 量程） */

/* ================================================================== */
/* 寄存器地址                                                          */
/* ================================================================== */
#define MMC_REG_XOUT0          0x00u  /**< Xout[17:10] */
#define MMC_REG_XOUT1          0x01u  /**< Xout[9:2] */
#define MMC_REG_YOUT0          0x02u  /**< Yout[17:10] */
#define MMC_REG_YOUT1          0x03u  /**< Yout[9:2] */
#define MMC_REG_ZOUT0          0x04u  /**< Zout[17:10] */
#define MMC_REG_ZOUT1          0x05u  /**< Zout[9:2] */
#define MMC_REG_XYZOUT2        0x06u  /**< bit[7:6]=X[1:0] bit[5:4]=Y[1:0] bit[3:2]=Z[1:0] */
#define MMC_REG_TOUT           0x07u  /**< 温度输出，8 位无符号 */
#define MMC_REG_STATUS         0x08u  /**< 器件状态 */
#define MMC_REG_CTRL0          0x09u  /**< 控制寄存器 0（写） */
#define MMC_REG_CTRL1          0x0Au  /**< 控制寄存器 1（写） */
#define MMC_REG_CTRL2          0x0Bu  /**< 控制寄存器 2（写） */
#define MMC_REG_CTRL3          0x0Cu  /**< 控制寄存器 3（写） */
#define MMC_REG_PRODUCT_ID     0x2Fu  /**< Product ID1，固定为 0x30 */

/* STATUS（0x08）位定义 */
#define MMC_STATUS_MEAS_M_DONE  0x01u  /**< 磁场测量完成 */
#define MMC_STATUS_MEAS_T_DONE  0x02u  /**< 温度测量完成 */
#define MMC_STATUS_OTP_RD_DONE  0x10u  /**< OTP 读取完成 */

/* CTRL0（0x09）位定义 */
#define MMC_CTRL0_TM_M           0x01u  /**< 触发磁场测量（自动清零） */
#define MMC_CTRL0_TM_T           0x02u  /**< 触发温度测量（自动清零） */
#define MMC_CTRL0_INT_MEAS_DONE  0x04u  /**< 测量完成中断使能 */
#define MMC_CTRL0_SET            0x08u  /**< 置位（Set）操作（自动清零） */
#define MMC_CTRL0_RESET          0x10u  /**< 复位（Reset）操作（自动清零） */
#define MMC_CTRL0_AUTO_SR_EN     0x20u  /**< 自动 Set/Reset */
#define MMC_CTRL0_OTP_READ       0x40u  /**< 重新读取 OTP */

/* CTRL1（0x0A）位定义 */
#define MMC_CTRL1_SW_RST         0x80u  /**< 软复位，上电时间 10 ms */
#define MMC_CTRL1_YZ_INHIBIT     0x18u  /**< 关闭 Y、Z 通道（bit4:3） */
#define MMC_CTRL1_X_INHIBIT      0x04u  /**< 关闭 X 通道 */
#define MMC_CTRL1_BW1            0x02u  /**< 带宽选择高位 */
#define MMC_CTRL1_BW0            0x01u  /**< 带宽选择低位 */

/* CTRL3（0x0C）位定义 */
#define MMC_CTRL3_SPI_3W         0x40u  /**< 切换到 3 线 SPI 模式 */
#define MMC_CTRL3_ST_ENM         0x04u  /**< 施加反向额外磁场（自检） */
#define MMC_CTRL3_ST_ENP         0x02u  /**< 施加正向额外磁场（自检） */

/* ================================================================== */
/* 返回状态                                                            */
/* ================================================================== */
/**
 * @brief 所有 API 的返回状态
 */
typedef enum {
    MMC_OK = 0,      /**< 操作成功 */
    MMC_ERR_PARAM,   /**< 参数非法：空指针或枚举越界 */
    MMC_ERR_BUS,     /**< 总线读写失败（回调返回非 0） */
    MMC_ERR_ID,      /**< Product ID 与 MMC_PRODUCT_ID_VALUE 不符 */
    MMC_ERR_STATE,   /**< 状态错误，例如未初始化 */
    MMC_ERR_TIMEOUT  /**< 等待测量完成超时 */
} mmc_status_t;

/* ================================================================== */
/* 回调                                                                */
/* ================================================================== */
/**
 * @brief 总线读回调
 *
 * @param ctx 调用方上下文指针，由 mmc_init() 原样透传
 * @param reg 起始寄存器地址
 * @param buf 输出缓冲区，由驱动提供
 * @param len 需要读取的字节数
 *
 * @return 0 表示成功，非 0 表示失败（驱动会转换为 MMC_ERR_BUS）
 *
 * @note I2C 实现通常是"写寄存器地址 + 重复起始 + 读数据"。
 */
typedef int (*mmc_bus_read_fn)(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len);

/**
 * @brief 总线写回调
 *
 * @param ctx 调用方上下文指针，由 mmc_init() 原样透传
 * @param reg 起始寄存器地址
 * @param buf 待写入数据
 * @param len 需要写入的字节数
 *
 * @return 0 表示成功，非 0 表示失败（驱动会转换为 MMC_ERR_BUS）
 */
typedef int (*mmc_bus_write_fn)(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len);

/**
 * @brief 微秒级延时回调
 *
 * @param ctx 调用方上下文指针
 * @param us  需要延时的微秒数
 *
 * @note 用于软复位后的 10 ms 上电等待，以及轮询测量状态时的间隔。
 *       实现可以向上取整到毫秒延时。
 */
typedef void (*mmc_delay_us_fn)(void *ctx, uint32_t us);

/* ================================================================== */
/* 配置枚举                                                            */
/* ================================================================== */
/**
 * @brief 测量带宽与单次测量时间
 *
 * @note 带宽越大，单次测量越快，噪声越高。X/Y/Z 三轴并行测量。
 */
typedef enum {
    MMC_BW_8MS_100HZ = 0,  /**< BW[1:0]=00，测量 8 ms，带宽 100 Hz */
    MMC_BW_4MS_200HZ = 1,  /**< BW[1:0]=01，测量 4 ms，带宽 200 Hz */
    MMC_BW_2MS_400HZ = 2,  /**< BW[1:0]=10，测量 2 ms，带宽 400 Hz */
    MMC_BW_05MS_800HZ = 3  /**< BW[1:0]=11，测量 0.5 ms，带宽 800 Hz */
} mmc_bw_t;

/* ================================================================== */
/* 数据结构                                                            */
/* ================================================================== */
/**
 * @brief 三个轴的 18 位原始码值
 *
 * @note 无符号格式，MMC_RAW_ZERO（131072）对应零磁场；
 *       换算为 Gauss：field = (raw - MMC_RAW_ZERO) / MMC_COUNTS_PER_GAUSS。
 */
typedef struct {
    uint32_t x;   /**< X 轴原始码值，0 ~ 262143 */
    uint32_t y;   /**< Y 轴原始码值，0 ~ 262143 */
    uint32_t z;   /**< Z 轴原始码值，0 ~ 262143 */
} mmc_raw_t;

/**
 * @brief 换算后的磁感应强度
 */
typedef struct {
    float x_g;   /**< X 轴磁场，单位 Gauss */
    float y_g;   /**< Y 轴磁场，单位 Gauss */
    float z_g;   /**< Z 轴磁场，单位 Gauss */
} mmc_field_t;

/**
 * @brief 设备实例
 *
 * @note 字段由驱动维护，调用方不应直接修改。
 * @note CTRL0/CTRL1/CTRL2/CTRL3 都是只写寄存器（手册 Mode 为 W），读回值不可靠，
 *       因此驱动在 ctrl1 字段中维护一份影子副本，用于位域修改。
 */
typedef struct {
    mmc_bus_read_fn  read;      /**< 读回调，由 mmc_init() 写入 */
    mmc_bus_write_fn write;     /**< 写回调，由 mmc_init() 写入 */
    mmc_delay_us_fn  delay_us;  /**< 延时回调，由 mmc_init() 写入 */
    void    *ctx;               /**< 回调上下文，由 mmc_init() 写入 */
    uint8_t  ctrl1;             /**< CTRL1 的影子副本（只写寄存器的软件缓存） */
    uint8_t  bw;                /**< 当前带宽设置，驱动内部缓存 */
    bool     ready;             /**< 是否已成功初始化 */
} mmc_t;

/* ================================================================== */
/* API                                                                 */
/* ================================================================== */

/**
 * @brief  初始化：软复位 → 校验 Product ID → 设置默认带宽
 *
 * @param  dev      设备实例，由调用方分配
 * @param  rd       总线读回调，不可为 NULL
 * @param  wr       总线写回调，不可为 NULL
 * @param  delay_us 延时回调，不可为 NULL
 * @param  ctx      透传给回调的上下文指针，可为 NULL
 *
 * @return MMC_OK        初始化成功
 * @return MMC_ERR_PARAM 任一必需指针为空
 * @return MMC_ERR_BUS   总线读写失败
 * @return MMC_ERR_ID    Product ID 不等于 0x30
 *
 * @note   软复位后需要 10 ms 上电时间，本函数通过 delay_us 回调等待。
 * @note   默认带宽为 MMC_BW_8MS_100HZ（100 Hz 输出、8 ms 测量时间）。
 */
mmc_status_t mmc_init(mmc_t *dev, mmc_bus_read_fn rd, mmc_bus_write_fn wr,
                      mmc_delay_us_fn delay_us, void *ctx);

/**
 * @brief  软复位
 *
 * @param  dev 设备实例
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   会清空所有寄存器并重新读取 OTP，复位后需等待 10 ms。
 * @note   执行后 dev->ready 置为 false，需重新调用 mmc_init()。
 */
mmc_status_t mmc_soft_reset(mmc_t *dev);

/**
 * @brief  设置测量带宽
 *
 * @param  dev 设备实例
 * @param  bw  目标带宽，取值见 mmc_bw_t
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   修改带宽不影响其它配置位（X/Y/Z 通道使能保持原状）。
 */
mmc_status_t mmc_set_bandwidth(mmc_t *dev, mmc_bw_t bw);

/**
 * @brief  触发一次磁场测量
 *
 * @param  dev 设备实例
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   TM_M 位在测量结束后由芯片自动清零。
 * @note   触发后需等待测量完成（见 mmc_measurement_ready 与 mmc_measure）。
 */
mmc_status_t mmc_trigger_measurement(mmc_t *dev);

/**
 * @brief  查询磁场测量是否完成
 *
 * @param  dev   设备实例
 * @param  ready 输出标志，true 表示数据可读
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 */
mmc_status_t mmc_measurement_ready(mmc_t *dev, bool *ready);

/**
 * @brief  读取三轴原始码值
 *
 * @param  dev 设备实例
 * @param  out 输出结构体，不可为 NULL
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS / MMC_ERR_STATE
 *
 * @note   一次连续读取 0x00~0x06 共 7 字节，保证三轴来自同一次测量。
 */
mmc_status_t mmc_read_raw(mmc_t *dev, mmc_raw_t *out);

/**
 * @brief  读取并换算为 Gauss
 *
 * @param  dev 设备实例
 * @param  out 输出结构体，不可为 NULL
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS / MMC_ERR_STATE
 */
mmc_status_t mmc_read_field(mmc_t *dev, mmc_field_t *out);

/**
 * @brief  读取温度
 *
 * @param  dev   设备实例
 * @param  deg_c 输出温度，单位 ℃，不可为 NULL
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   tout 为 8 位无符号值，换算公式 T = tout * 0.8 - 75，范围 -75 ~ 125 ℃。
 */
mmc_status_t mmc_read_temperature(mmc_t *dev, float *deg_c);

/**
 * @brief  触发一次温度测量
 *
 * @param  dev 设备实例
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   TM_T 与 TM_M 不能同时置位；温度测量完成后同样会自动清零。
 */
mmc_status_t mmc_trigger_temperature(mmc_t *dev);

/**
 * @brief  执行一次完整的磁场测量（触发 → 轮询 → 读取 → 换算）
 *
 * @param  dev        设备实例
 * @param  out        输出结构体，不可为 NULL
 * @param  timeout_ms 等待超时，单位 ms
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS / MMC_ERR_STATE / MMC_ERR_TIMEOUT
 *
 * @note   轮询间隔为 1 ms，超过 timeout_ms 未完成则返回 MMC_ERR_TIMEOUT。
 *         100 Hz 带宽下单次测量约 8 ms，建议超时不少于 50 ms。
 */
mmc_status_t mmc_measure(mmc_t *dev, mmc_field_t *out, uint32_t timeout_ms);

/**
 * @brief  执行 Set 操作（大电流置位）
 *
 * @param  dev 设备实例
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @note   Set 脉冲约 500 ns，位在操作结束后自动清零。
 * @note   Set/Reset 用于消除磁滞与零点漂移，建议在温漂敏感场合周期执行。
 */
mmc_status_t mmc_set_coil(mmc_t *dev);

/**
 * @brief  执行 Reset 操作（大电流复位）
 *
 * @param  dev 设备实例
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 */
mmc_status_t mmc_reset_coil(mmc_t *dev);

/**
 * @brief  直接写寄存器
 *
 * @param  dev   设备实例
 * @param  reg   寄存器地址
 * @param  value 写入值
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 *
 * @warning 绕过驱动的配置校验，调用方需自行确认寄存器位定义。
 */
mmc_status_t mmc_write_reg(mmc_t *dev, uint8_t reg, uint8_t value);

/**
 * @brief  直接读寄存器
 *
 * @param  dev   设备实例
 * @param  reg   寄存器地址
 * @param  value 输出值，不可为 NULL
 * @return MMC_OK / MMC_ERR_PARAM / MMC_ERR_BUS
 */
mmc_status_t mmc_read_reg(mmc_t *dev, uint8_t reg, uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif /* MMC5983MA_H */
