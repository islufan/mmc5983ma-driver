# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-11-28

First public release. The public API is stable from this version on.

### Added

- Platform-independent bus: read, write and microsecond-delay callbacks injected by the caller
- Bandwidth selection: 8 ms / 100 Hz, 4 ms / 200 Hz, 2 ms / 400 Hz, 0.5 ms / 800 Hz
- 18-bit unsigned output assembled from `Xout0`, `Xout1` and `XYZout2`, converted to Gauss
- Temperature reading from `Tout`, `T = tout * 0.8 - 75`
- One-shot measurement with polling and timeout (`mmc_measure`)
- Set / Reset degaussing (`mmc_set_coil`, `mmc_reset_coil`)
- Product ID check, soft reset with the required 10 ms wait
- Direct register read/write
- STM32 HAL I2C adapter in `port/`
- STM32G474 example in `examples/`
- Mock-bus unit tests covering bit assembly, sign handling, temperature, timeout and parameter guards
- GitHub Actions workflow building and testing on Ubuntu

### Fixed

- `CTRL1` is write-only, so the bandwidth update no longer does a read-modify-write. The driver keeps a shadow copy and only preserves the channel-inhibit bits, which keeps `SW_RST` and reserved bits out of the write.
- A direct write to `CTRL1` now updates the shadow copy, so a following `mmc_set_bandwidth()` no longer overwrites the channel-inhibit settings the caller just applied.

### Notes

- The driver is not thread-safe; guard a shared `mmc_t` instance if several tasks use it.
- Only one-shot measurements are exposed in this release. Continuous measurement mode (`Cmm_en`) is not implemented yet.
