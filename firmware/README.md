
## Differences from upstream

This fork tracks [m5stack/StackChan](https://github.com/m5stack/StackChan) (`upstream/main`) and currently diverges from it at commit `b72b3ed` with the following changes:

### Automatic OTA upgrade disabled

`CONFIG_OTA_URL` (`main/Kconfig.projbuild`) still points at the upstream xiaozhi-esp32 cloud OTA server, which does not serve StackChan-compatible firmware images. On boot, `Application::CheckNewVersion()` used to compare the local version against whatever that server reports and silently flash the result if it looked newer, so devices could end up running unrelated/incompatible firmware. That auto-upgrade step is now skipped (see `patches/xiaozhi-esp32.patch`); the underlying version/activation check itself still runs, since it's also how mqtt/websocket config is fetched.

### CO2 sensor support (M5Stack CO2L / Sensirion SCD41)

Adds voice-assistant support for the [M5Stack CO2L unit](https://docs.m5stack.com/en/unit/CO2L), wired to Grove **PORT.A** (SDA=G2, SCL=G1):

- `main/hal/drivers/scd41/` — minimal SCD41 I2C driver (low-power single-shot measurement, CRC8-verified)
- `main/hal/hal_co2.cpp` — HAL wrapper; a measurement is triggered on demand (single shot, ~5s) rather than polled continuously, to save power on battery
- `main/hal/hal_mcp.cpp` — exposes a `self.sensor.get_co2` MCP tool so the assistant can answer questions about CO2 concentration, temperature and humidity
- `main/hal/board/stackchan.cc` — adds a second, external I2C bus for Grove PORT.A, separate from the internal I2C bus used for the PMIC/touch/etc.

Note: this repurposes GPIO2, which upstream also wires up (unused on this hardware) as a laser-pointer output in `Hal::setLaserEnabled()` (`main/hal/hal_espnow.cpp`). The two features cannot be used at the same time.

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.5.4](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Host-side tests

The motion coordinate helpers can be tested without ESP-IDF hardware:

```bash
cmake -S tests -B build-host-tests
cmake --build build-host-tests
ctest --test-dir build-host-tests --output-on-failure
```

### Flash

```bash
idf.py flash
```
