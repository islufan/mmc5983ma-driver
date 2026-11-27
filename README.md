# MMC5983MA Driver

[![ci](https://github.com/islufan/mmc5983ma-driver/actions/workflows/ci.yml/badge.svg)](https://github.com/islufan/mmc5983ma-driver/actions/workflows/ci.yml)

[**English**](README.md) | [简体中文](README.zh-CN.md)

A C99 driver for the MMC5983MA 3-axis magnetometer, written for bare-metal embedded targets. The driver does not depend on STM32 HAL, ESP-IDF or any other platform SDK: the bus and the delay are injected as callbacks.

## Features

- I2C bus adapter, read / write / delay are all injectable callbacks
- 18-bit unsigned output converted to Gauss (16384 counts/G, ±8 G range)
- Configurable bandwidth and measurement time: 8 ms / 4 ms / 2 ms / 0.5 ms
- Single measurement, Set/Reset degaussing, temperature reading
- Status-code based error handling, including measurement timeout
- Mock-bus unit tests that need no hardware

## Layout

```text
src/       platform-independent driver
port/      STM32 HAL adapter
examples/  STM32G474 example
tests/     mock-bus unit tests
```

## Quick Start

The bus needs three callbacks: read, write and delay. An I2C read is "write the register address, repeated start, read the payload":

```c
static void delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    platform_delay_us(us);
}

static int bus_read(void *ctx, uint8_t reg, uint8_t *buf, uint16_t len)
{
    i2c_write_then_read(MMC_I2C_ADDR_7BIT, reg, buf, len);
    return 0;   /* 0 on success */
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
        /* field.x_g / field.y_g / field.z_g, in Gauss */
    }
}
```

The STM32 HAL adapter lives in `port/`:

```c
mmc_hal_i2c_t bus;
mmc_hal_i2c_bind(&bus, &hi2c1, 100);
mmc_init(&mag, mmc_hal_i2c_read, mmc_hal_i2c_write, mmc_hal_delay_us, &bus);
```

Initialization performs a soft reset (waits 10 ms), verifies the Product ID (expected `0x30`) and sets the bandwidth to 100 Hz. It can be changed afterwards:

```c
mmc_set_bandwidth(&mag, MMC_BW_2MS_400HZ);
```

## API

| Function | Purpose |
| :--- | :--- |
| `mmc_init()` | Soft reset, verify Product ID, apply the default bandwidth |
| `mmc_soft_reset()` | Soft reset; re-initialization is required afterwards |
| `mmc_set_bandwidth()` | Set bandwidth and per-measurement time |
| `mmc_trigger_measurement()` | Start a magnetic measurement (non-blocking) |
| `mmc_measurement_ready()` | Poll whether the magnetic measurement is done |
| `mmc_read_raw()` | Read the 18-bit counts of all three axes |
| `mmc_read_field()` | Read and convert to Gauss |
| `mmc_measure()` | Trigger, poll, read and convert in one call |
| `mmc_trigger_temperature()` | Start a temperature measurement |
| `mmc_read_temperature()` | Read the temperature in °C |
| `mmc_set_coil()` / `mmc_reset_coil()` | Run the Set / Reset degaussing pulse |
| `mmc_read_reg()` / `mmc_write_reg()` | Direct register access |

Status codes: `MMC_OK`, `MMC_ERR_PARAM`, `MMC_ERR_BUS`, `MMC_ERR_ID`, `MMC_ERR_STATE`, `MMC_ERR_TIMEOUT`.

A direct write to `CTRL1` also updates the driver's internal copy of the bandwidth and channel-inhibit bits. After writing `SW_RST` you still have to wait 10 ms before touching the registers again.

## Configuration

Bandwidth and per-measurement time (`CTRL1[1:0]`):

| Enum | BW[1:0] | Measurement time | Bandwidth |
| :--- | :--- | :--- | :--- |
| `MMC_BW_8MS_100HZ` | `00` | 8 ms | 100 Hz |
| `MMC_BW_4MS_200HZ` | `01` | 4 ms | 200 Hz |
| `MMC_BW_2MS_400HZ` | `10` | 2 ms | 400 Hz |
| `MMC_BW_05MS_800HZ` | `11` | 0.5 ms | 800 Hz |

The three axes are measured **in parallel**; the bandwidth only changes the duration and the noise of a single measurement.

## Data Conversion

The 18-bit output is unsigned with its zero point at `131072` (2^17) and a sensitivity of `16384 counts/G`:

```text
field(Gauss) = (raw18 - 131072) / 16384
temp(degC)   = tout * 0.8 - 75        (tout is 8-bit, range -75 to 125 degC)
```

Where the 18 bits come from:

| Register | Contents |
| :--- | :--- |
| `Xout0` (0x00) | `Xout[17:10]` |
| `Xout1` (0x01) | `Xout[9:2]` |
| `XYZout2` (0x06) | bit[7:6] = `Xout[1:0]`, bit[5:4] = `Yout[1:0]`, bit[3:2] = `Zout[1:0]` |

`mmc_read_field()` already does the bit assembly and the conversion, so callers never touch the bit layout.

## Build and Test

```bash
cmake -S . -B build -DMMC_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

When the library is added as a subdirectory, tests are off by default:

```cmake
add_subdirectory(mmc5983ma-driver)
target_link_libraries(my_app PRIVATE mmc5983ma)
```

## Troubleshooting

- The Product ID should read `0x30` (register `0x2F`). If it does not, check the I2C address, the pull-up resistors and the supply.
- After a soft reset the registers are only accessible after 10 ms; anything read before that is meaningless.
- Read the data only after the measurement completes: check `Meas_M_Done` (bit0) in `Status`, then read `XYZout`.
- `CTRL0`–`CTRL3` are write-only; reading them back is not reliable. The driver keeps a shadow copy of `CTRL1`. Do not use read-modify-write on these registers.
- The zero point can drift over time. Run `mmc_set_coil()` / `mmc_reset_coil()` periodically to degauss.

## License

MIT
