# MMC5983MA 驱动

[![ci](https://github.com/islufan/mmc5983ma-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/islufan/mmc5983ma-driver/actions/workflows/ci.yml)

面向嵌入式 C99 的 MMC5983MA 三轴磁力计驱动。驱动本体不依赖 STM32 HAL、ESP-IDF 或其他平台 SDK，总线与延时通过回调提供。

## 特性

- I2C 总线适配，读写与延时均为可注入回调
- 18 位无符号输出，直接换算为 Gauss（16384 counts/G，±8 G）
- 带宽/测量时间可配置：8 ms / 4 ms / 2 ms / 0.5 ms
- 单次测量、Set/Reset 消磁、温度读取
- 状态码错误处理，含测量超时
- 不需要硬件的 mock 单元测试

## 项目结构

```text
src/       平台无关驱动
port/      STM32 HAL 适配层
examples/  STM32G474 示例
tests/     mock 单元测试
```

## 快速开始

总线需要提供读、写和延时三个回调。I2C 读为"写寄存器地址 + 重复起始 + 读数据"：

```c
static void delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    platform_delay_us(us);
}

static int bus_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    i2c_write_then_read(MMC_I2C_ADDR_7BIT, reg, buf, len);
    return 0;   /* 成功返回 0 */
}

static int bus_write(void *ctx, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    i2c_write(MMC_I2C_ADDR_7BIT, reg, buf, len);
    return 0;
}

mmc_t mag;
if (mmc_init(&mag, bus_read, bus_write, delay_us, NULL) == MMC_OK) {
    mmc_field_t field;
    if (mmc_measure(&mag, &field, 50) == MMC_OK) {
        /* field.x_g / field.y_g / field.z_g，单位 Gauss */
    }
}
```

STM32 HAL 适配层位于 `port/`：

```c
mmc_hal_i2c_t bus;
mmc_hal_i2c_bind(&bus, &hi2c1, 100);
mmc_init(&mag, mmc_hal_i2c_read, mmc_hal_i2c_write, mmc_hal_delay_us, &bus);
```

初始化会执行软复位（等待 10 ms）、校验 Product ID（应为 `0x30`），并把带宽设为 100 Hz。之后可以调整：

```c
mmc_set_bandwidth(&mag, MMC_BW_2MS_400HZ);
```

## API

| 函数 | 作用 |
| :--- | :--- |
| `mmc_init()` | 软复位、校验 Product ID、设置默认带宽 |
| `mmc_soft_reset()` | 软复位，之后需重新初始化 |
| `mmc_set_bandwidth()` | 设置带宽与单次测量时间 |
| `mmc_trigger_measurement()` | 触发一次磁场测量（不阻塞） |
| `mmc_measurement_ready()` | 查询磁场测量是否完成 |
| `mmc_read_raw()` | 读取三轴 18 位原始码值 |
| `mmc_read_field()` | 读取并换算为 Gauss |
| `mmc_measure()` | 触发 → 轮询 → 读取 → 换算的完整流程 |
| `mmc_trigger_temperature()` | 触发温度测量 |
| `mmc_read_temperature()` | 读取温度（℃） |
| `mmc_set_coil()` / `mmc_reset_coil()` | 执行 Set / Reset 消磁 |
| `mmc_read_reg()` / `mmc_write_reg()` | 直接访问寄存器 |

状态码包括 `MMC_OK`、`MMC_ERR_PARAM`、`MMC_ERR_BUS`、`MMC_ERR_ID`、`MMC_ERR_STATE` 和 `MMC_ERR_TIMEOUT`。

直接写 `CTRL1` 后，驱动会同步带宽和通道屏蔽位的内部缓存。写入 `SW_RST` 时仍需等待 10 ms，再继续访问寄存器。

## 配置

带宽与单次测量时间（`CTRL1[1:0]`）：

| 枚举 | BW[1:0] | 测量时间 | 带宽 |
| :--- | :--- | :--- | :--- |
| `MMC_BW_8MS_100HZ` | `00` | 8 ms | 100 Hz |
| `MMC_BW_4MS_200HZ` | `01` | 4 ms | 200 Hz |
| `MMC_BW_2MS_400HZ` | `10` | 2 ms | 400 Hz |
| `MMC_BW_05MS_800HZ` | `11` | 0.5 ms | 800 Hz |

三轴测量是**并行**的，带宽只影响单次测量的时长与噪声。

## 数据换算

18 位输出为无符号格式，零点在 `131072`（2^17），灵敏度 `16384 counts/G`：

```text
field(Gauss) = (raw18 - 131072) / 16384
temp(℃)      = tout * 0.8 - 75        （tout 为 8 位，范围 -75 ~ 125 ℃）
```

三轴 18 位数据的来源：

| 寄存器 | 内容 |
| :--- | :--- |
| `Xout0` (0x00) | `Xout[17:10]` |
| `Xout1` (0x01) | `Xout[9:2]` |
| `XYZout2` (0x06) | bit[7:6] = `Xout[1:0]`，bit[5:4] = `Yout[1:0]`，bit[3:2] = `Zout[1:0]` |

`mmc_read_field()` 已完成拼接与换算，无需自己处理位操作。

## 构建和测试

```bash
cmake -S . -B build -DMMC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

作为子目录集成时，测试默认关闭：

```cmake
add_subdirectory(mmc5983ma-driver)
target_link_libraries(my_app PRIVATE mmc5983ma)
```

## 常见问题

- Product ID 应读到 `0x30`（寄存器 `0x2F`）。读不到时检查 I2C 地址、上拉电阻与供电。
- 软复位后需要 10 ms 才能访问寄存器；等待期间读到的值无意义。
- 测量完成后再读数据：先看 `Status` 的 `Meas_M_Done`（bit0），再看 `XYZout`。
- `CTRL0~CTRL3` 都是只写寄存器，读回值不可靠。驱动内部用影子副本维护 `CTRL1`，请勿用"读-改-写"方式直接操作这些寄存器。
- 长时间使用后零点可能漂移，可周期性执行 `mmc_set_coil()` / `mmc_reset_coil()` 消磁。

## 许可

MIT
