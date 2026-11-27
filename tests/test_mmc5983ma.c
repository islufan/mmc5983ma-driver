/**
 * @file    test_mmc5983ma.c
 * @brief   驱动单元测试：用 mock 总线验证寄存器操作与数据换算，不需要真实硬件
 * @author  LuFan
 * @date    2026-11-27
 * @license MIT
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mmc5983ma.h"

/* ------------------------------------------------------------------ */
/* mock 总线                                                           */
/* ------------------------------------------------------------------ */
static uint8_t g_regs[256];
static int     g_bus_fail = 0;
static uint32_t g_delay_us_total = 0;
static int     g_delay_calls = 0;

static void regs_reset(void)
{
    memset(g_regs, 0, sizeof(g_regs));
    g_bus_fail = 0;
    g_delay_us_total = 0;
    g_delay_calls = 0;
    g_regs[MMC_REG_PRODUCT_ID] = MMC_PRODUCT_ID_VALUE;
}

static int mock_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    (void)ctx;
    if (g_bus_fail) {
        return -1;
    }
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = g_regs[(uint8_t)(reg + i)];
    }
    return 0;
}

static int mock_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    (void)ctx;
    if (g_bus_fail) {
        return -1;
    }
    for (uint16_t i = 0; i < len; i++) {
        g_regs[(uint8_t)(reg + i)] = buf[i];
    }
    return 0;
}

static void mock_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    g_delay_us_total += us;
    g_delay_calls++;
}

/* ------------------------------------------------------------------ */
/* 极简测试框架                                                        */
/* ------------------------------------------------------------------ */
static int g_failures = 0;

#define CHECK(cond, name)                                     \
    do {                                                      \
        if (cond) {                                           \
            printf("  ok   : %s\n", name);                    \
        } else {                                              \
            printf("  FAIL : %s\n", name);                    \
            g_failures++;                                     \
        }                                                     \
    } while (0)

static mmc_t make_dev(void)
{
    mmc_t dev;
    memset(&dev, 0, sizeof(dev));
    (void)mmc_init(&dev, mock_read, mock_write, mock_delay_us, NULL);
    return dev;
}

/** 把 18 位码值写入三个寄存器（X 轴） */
static void set_x_raw(uint32_t raw18)
{
    g_regs[MMC_REG_XOUT0] = (uint8_t)((raw18 >> 10) & 0xFFu);
    g_regs[MMC_REG_XOUT1] = (uint8_t)((raw18 >> 2) & 0xFFu);
    g_regs[MMC_REG_XYZOUT2] = (uint8_t)((g_regs[MMC_REG_XYZOUT2] & 0x3Fu) | ((raw18 & 0x03u) << 6));
}

/* ------------------------------------------------------------------ */
/* 用例                                                                */
/* ------------------------------------------------------------------ */
/**
 * @brief 初始化成功路径：软复位、Product ID 校验、默认带宽
 */
static void test_init_ok(void)
{
    printf("[test] init / product id\n");
    regs_reset();
    mmc_t dev;
    memset(&dev, 0, sizeof(dev));
    mmc_status_t st = mmc_init(&dev, mock_read, mock_write, mock_delay_us, NULL);
    CHECK(st == MMC_OK, "init returns MMC_OK");
    CHECK(dev.ready, "dev.ready is true");
    CHECK(g_delay_us_total >= 10000u, "soft reset waits at least 10 ms");
    CHECK((g_regs[MMC_REG_CTRL1] & 0x03u) == 0u, "default bandwidth = 100 Hz");
}

/**
 * @brief Product ID 不匹配时应返回 MMC_ERR_ID
 */
static void test_init_bad_id(void)
{
    printf("[test] init with wrong product id\n");
    regs_reset();
    g_regs[MMC_REG_PRODUCT_ID] = 0x00;
    mmc_t dev;
    memset(&dev, 0, sizeof(dev));
    mmc_status_t st = mmc_init(&dev, mock_read, mock_write, mock_delay_us, NULL);
    CHECK(st == MMC_ERR_ID, "returns MMC_ERR_ID");
    CHECK(!dev.ready, "dev.ready stays false");
}

/**
 * @brief 总线失败时应返回 MMC_ERR_BUS
 */
static void test_init_bus_error(void)
{
    printf("[test] init with failing bus\n");
    regs_reset();
    g_bus_fail = 1;
    mmc_t dev;
    memset(&dev, 0, sizeof(dev));
    mmc_status_t st = mmc_init(&dev, mock_read, mock_write, mock_delay_us, NULL);
    CHECK(st == MMC_ERR_BUS, "returns MMC_ERR_BUS");
}

/**
 * @brief 带宽写入 CTRL1 的 bit[1:0]，且不误置 SW_RST
 */
static void test_bandwidth(void)
{
    printf("[test] bandwidth bits\n");
    regs_reset();
    mmc_t dev = make_dev();

    CHECK(mmc_set_bandwidth(&dev, MMC_BW_2MS_400HZ) == MMC_OK, "set_bandwidth ok");
    CHECK((g_regs[MMC_REG_CTRL1] & 0x03u) == 2u, "BW[1:0] == 2 (400 Hz)");
    CHECK((g_regs[MMC_REG_CTRL1] & MMC_CTRL1_SW_RST) == 0u, "SW_RST not set");
    CHECK(dev.bw == (uint8_t)MMC_BW_2MS_400HZ, "dev.bw updated");

    CHECK(mmc_set_bandwidth(&dev, (mmc_bw_t)4) == MMC_ERR_PARAM, "invalid bandwidth rejected");

    CHECK(mmc_write_reg(&dev, MMC_REG_CTRL1, MMC_CTRL1_YZ_INHIBIT) == MMC_OK,
          "direct CTRL1 write accepted");
    CHECK(mmc_set_bandwidth(&dev, MMC_BW_4MS_200HZ) == MMC_OK,
          "bandwidth update after direct CTRL1 write");
    CHECK(g_regs[MMC_REG_CTRL1] == (MMC_CTRL1_YZ_INHIBIT | 1u),
          "CTRL1 shadow stays synchronized");
}

/**
 * @brief 18 位拼接：零点、正负 1 G、低 2 位
 */
static void test_raw_assembly(void)
{
    printf("[test] 18-bit assembly and gauss conversion\n");
    regs_reset();
    mmc_t dev = make_dev();

    /* 零点：131072 -> 0 G */
    g_regs[MMC_REG_XYZOUT2] = 0;
    set_x_raw(MMC_RAW_ZERO);
    mmc_field_t f;
    CHECK(mmc_read_field(&dev, &f) == MMC_OK, "read_field ok");
    CHECK(f.x_g > -0.001f && f.x_g < 0.001f, "raw 131072 -> 0 G");

    /* 正 1 G：131072 + 16384 = 147456 */
    set_x_raw(MMC_RAW_ZERO + 16384u);
    (void)mmc_read_field(&dev, &f);
    CHECK(f.x_g > 0.99f && f.x_g < 1.01f, "raw 147456 -> +1 G");

    /* 负 1 G：131072 - 16384 = 114688 */
    set_x_raw(MMC_RAW_ZERO - 16384u);
    (void)mmc_read_field(&dev, &f);
    CHECK(f.x_g < -0.99f && f.x_g > -1.01f, "raw 114688 -> -1 G");

    /* 低 2 位参与拼接：131072 + 3 */
    g_regs[MMC_REG_XYZOUT2] = 0;
    g_regs[MMC_REG_XOUT0] = (uint8_t)((MMC_RAW_ZERO >> 10) & 0xFFu);
    g_regs[MMC_REG_XOUT1] = (uint8_t)((MMC_RAW_ZERO >> 2) & 0xFFu);
    g_regs[MMC_REG_XYZOUT2] = (uint8_t)(0x03u << 6);   /* X[1:0] = 11 */
    mmc_raw_t raw;
    CHECK(mmc_read_raw(&dev, &raw) == MMC_OK, "read_raw ok");
    CHECK(raw.x == MMC_RAW_ZERO + 3u, "XYZout2 low bits merged into X");
}

/**
 * @brief 测量完成标志解析
 */
static void test_status(void)
{
    printf("[test] measurement ready flag\n");
    regs_reset();
    mmc_t dev = make_dev();
    bool ready = true;

    g_regs[MMC_REG_STATUS] = 0x00;
    (void)mmc_measurement_ready(&dev, &ready);
    CHECK(!ready, "no data -> false");

    g_regs[MMC_REG_STATUS] = MMC_STATUS_MEAS_M_DONE;
    (void)mmc_measurement_ready(&dev, &ready);
    CHECK(ready, "MEAS_M_DONE -> true");
}

/**
 * @brief 温度换算：0.8 ℃/LSB，0x00 = -75 ℃
 */
static void test_temperature(void)
{
    printf("[test] temperature formula\n");
    regs_reset();
    mmc_t dev = make_dev();
    float t = 0.0f;

    g_regs[MMC_REG_TOUT] = 0;
    CHECK(mmc_read_temperature(&dev, &t) == MMC_OK, "read_temperature ok");
    CHECK(t > -75.1f && t < -74.9f, "tout 0 -> -75.0 C");

    g_regs[MMC_REG_TOUT] = 100;
    CHECK(mmc_read_temperature(&dev, &t) == MMC_OK, "second read ok");
    CHECK(t > 4.9f && t < 5.1f, "tout 100 -> 5.0 C");

    g_regs[MMC_REG_TOUT] = 250;
    CHECK(mmc_read_temperature(&dev, &t) == MMC_OK, "third read ok");
    CHECK(t > 124.9f && t < 125.1f, "tout 250 -> 125.0 C");
}

/**
 * @brief 完整测量流程：触发 → 轮询 → 读取
 */
static void test_measure_flow(void)
{
    printf("[test] measure flow\n");
    regs_reset();
    mmc_t dev = make_dev();

    g_regs[MMC_REG_XYZOUT2] = 0;
    set_x_raw(MMC_RAW_ZERO + 16384u);
    g_regs[MMC_REG_STATUS] = MMC_STATUS_MEAS_M_DONE;

    mmc_field_t f;
    CHECK(mmc_measure(&dev, &f, 10) == MMC_OK, "measure returns MMC_OK");
    CHECK(f.x_g > 0.99f && f.x_g < 1.01f, "measured X == +1 G");
    CHECK(g_delay_calls > 0, "polling used delay callback");
}

/**
 * @brief 测量超时应返回 MMC_ERR_TIMEOUT
 */
static void test_measure_timeout(void)
{
    printf("[test] measure timeout\n");
    regs_reset();
    mmc_t dev = make_dev();

    g_regs[MMC_REG_STATUS] = 0x00;   /* 永不就绪 */
    mmc_field_t f;
    CHECK(mmc_measure(&dev, &f, 3) == MMC_ERR_TIMEOUT, "returns MMC_ERR_TIMEOUT");
}

/**
 * @brief Set / Reset 线圈操作写入正确的 CTRL0 位
 */
static void test_set_reset(void)
{
    printf("[test] set / reset coil\n");
    regs_reset();
    mmc_t dev = make_dev();

    CHECK(mmc_set_coil(&dev) == MMC_OK, "set_coil ok");
    CHECK(g_regs[MMC_REG_CTRL0] == MMC_CTRL0_SET, "CTRL0 SET bit written");

    CHECK(mmc_reset_coil(&dev) == MMC_OK, "reset_coil ok");
    CHECK(g_regs[MMC_REG_CTRL0] == MMC_CTRL0_RESET, "CTRL0 RESET bit written");
}

/**
 * @brief 空指针与非法参数的防护
 */
static void test_param_guards(void)
{
    printf("[test] parameter guards\n");
    regs_reset();
    mmc_t dev = make_dev();

    CHECK(mmc_init(NULL, mock_read, mock_write, mock_delay_us, NULL) == MMC_ERR_PARAM,
          "init(NULL) rejected");
    CHECK(mmc_init(&dev, mock_read, mock_write, NULL, NULL) == MMC_ERR_PARAM,
          "init without delay rejected");
    CHECK(mmc_read_field(&dev, NULL) == MMC_ERR_PARAM, "read_field(NULL) rejected");
    CHECK(mmc_read_temperature(&dev, NULL) == MMC_ERR_PARAM, "read_temperature(NULL) rejected");
    CHECK(mmc_set_bandwidth(&dev, (mmc_bw_t)9) == MMC_ERR_PARAM, "bandwidth out of range rejected");
}

int main(void)
{
    printf("=== mmc5983ma unit tests ===\n");
    test_init_ok();
    test_init_bad_id();
    test_init_bus_error();
    test_bandwidth();
    test_raw_assembly();
    test_status();
    test_temperature();
    test_measure_flow();
    test_measure_timeout();
    test_set_reset();
    test_param_guards();

    printf("=== %s (%d failure%s) ===\n",
           g_failures == 0 ? "PASS" : "FAIL",
           g_failures, g_failures == 1 ? "" : "s");
    return (g_failures == 0) ? 0 : 1;
}
