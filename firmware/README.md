
## Differences from upstream

This fork tracks [m5stack/StackChan](https://github.com/m5stack/StackChan) (`upstream/main`) and currently diverges from it at commit `b72b3ed` with the following changes:

### Automatic OTA upgrade disabled

`CONFIG_OTA_URL` (`main/Kconfig.projbuild`) still points at the upstream xiaozhi-esp32 cloud OTA server, which does not serve StackChan-compatible firmware images. On boot, `Application::CheckNewVersion()` used to compare the local version against whatever that server reports and silently flash the result if it looked newer, so devices could end up running unrelated/incompatible firmware. That auto-upgrade step is now skipped (see `patches/xiaozhi-esp32.patch`); the underlying version/activation check itself still runs, since it's also how mqtt/websocket config is fetched.

### CO2 sensor support (M5Stack CO2L / Sensirion SCD41)

Adds voice-assistant support for the [M5Stack CO2L unit](https://docs.m5stack.com/en/unit/CO2L), wired to Grove **PORT.A** (SDA=G2, SCL=G1):

- `main/hal/drivers/scd41/` — minimal SCD41 I2C driver (low-power single-shot measurement, CRC8-verified)
- `main/hal/hal_co2.cpp` — HAL wrapper; a measurement is triggered on demand (single shot, ~5s) rather than polled continuously, to save power on battery. It also runs a background task that takes a reading every 10 minutes and, if CO2 is at or above 1000ppm (Japan's Building Sanitation Law ventilation guidance), shows an on-screen alert and plays a notification sound — once per crossing, re-arming only after the level drops back down. Alerts are suppressed during quiet hours (23:00-07:00 local time, see `Hal::isQuietHours()` in `main/hal/hal_rtc.cpp`)
- `main/hal/hal_mcp.cpp` — exposes a `self.sensor.get_co2` MCP tool so the assistant can answer questions about CO2 concentration, temperature and humidity
- `main/hal/board/stackchan.cc` — adds a second, external I2C bus for Grove PORT.A, separate from the internal I2C bus used for the PMIC/touch/etc.

Note: this repurposes GPIO2, which upstream also wires up (unused on this hardware) as a laser-pointer output in `Hal::setLaserEnabled()` (`main/hal/hal_espnow.cpp`). The two features cannot be used at the same time.

### Environment sensor support (M5Stack ENV Pro Unit / Bosch BME688)

Adds voice-assistant support for the [M5Stack ENV Pro Unit](https://docs.m5stack.com/en/unit/ENV%20Pro%20Unit), on Grove **PORT.A**:

- `main/hal/drivers/bme688/BME68x_SensorAPI/` — Bosch's official BME68x sensor API (vendored, BSD-3-Clause), same approach as the BMI270 IMU driver
- `main/hal/drivers/bme688/` — thin I2C wrapper around the sensor API, forced-mode (on-demand) temperature/pressure/humidity/gas-resistance readout
- `main/hal/hal_env.cpp` — HAL wrapper; triggers a forced-mode measurement on demand, plus a rough "air quality score" (0-100%, higher is better) computed from humidity and the sensor's raw gas resistance against a self-adjusting baseline, and an estimated indoor WBGT heatstroke-risk index
- `main/hal/hal_mcp.cpp` — exposes a `self.sensor.get_environment` MCP tool so the assistant can answer questions about temperature, barometric pressure, humidity, air quality, and heatstroke risk

Note: this does **not** use Bosch's BSEC library for a calibrated IAQ index — BSEC is closed-source and its license restricts use to business users and prohibits redistributing the compiled library, which is incompatible with a public hobby-project repo. The air quality score is a simpler community-style heuristic instead: it starts from the first reading as a baseline and self-calibrates over subsequent queries (resets on reboot), so it's a relative/approximate indicator, not a certified measurement.

The gas heater (and with it, the air quality score) is currently **disabled** to save power on battery — it's the dominant power draw of a BME688 measurement (300C hotplate) and is otherwise only used for that heuristic. Temperature/pressure/humidity/WBGT are unaffected. With the heater disabled, `gas_valid` is always false and `air_quality_percent` is always omitted from the tool's response. Re-enable it in `main/hal/drivers/bme688/bme688.cpp` (`begin()`, `heatr_conf.enable`) if desired.

The WBGT (Wet-Bulb Globe Temperature, 暑さ指数) estimate follows Japan's Ministry of the Environment indoor formula, `WBGT = 0.7 * wet-bulb temp + 0.3 * globe temp` ([wbgt.env.go.jp](https://www.wbgt.env.go.jp/)), with wet-bulb temperature approximated from temperature/humidity via Stull's (2011) empirical formula and globe temperature approximated by air temperature (no physical black-globe sensor). The reported `wbgt_risk_level` (caution/warning/severe warning/danger) uses the Ministry's official thresholds (25/28/31°C).

`hal_env.cpp` also runs a background task that takes a reading every 10 minutes and, if WBGT is at or above 28°C (the Ministry's "severe warning" level), shows an on-screen alert and plays a notification sound — once per crossing, re-arming only after it drops back down, same as the CO2 ventilation alert above (and likewise suppressed during quiet hours).

### Idle screen dimming / quiet-hours screen-off

Adds automatic backlight dimming to the AI-agent avatar screen, to save power and cut down on light/distraction when nobody's interacting with it:

- `main/hal/board/stackchan_display.cc` (`StackChanAvatarDisplay::UpdateStatusBar`/`SetStatus`) — after 10 seconds of being idle (not listening or speaking), the backlight drops to 10% brightness; during quiet hours (23:00-07:00 local time, the same window `Hal::isQuietHours()` uses for the CO2/WBGT alerts above) it goes fully off instead. Brightness is restored automatically the moment a conversation starts again (wake word, tap on the avatar, etc).
- `main/hal/board/stackchan.cc` (`CustomBacklight::SetBrightnessImpl`) — fixes a pre-existing bug where every brightness change drove the PMIC over I2C with dozens of redundant writes spread across up to ~450ms (the base `Backlight` class's software fade steps its internal brightness by 1 every 5ms, but this board's `SetBrightnessImpl` ignored that stepped value and re-wrote the final target on every tick instead of once). Since that internal I2C bus is shared with the audio codec, touch controller and IMU, the redundant writes could stall audio playback and avatar animation whenever brightness changed during a live conversation. Brightness is now applied in a single write and the fade timer stopped immediately — matching the PMIC hardware anyway, which has no native fade, just a coarse 8-level register.

### Spoken CO2/WBGT alerts (AquesTalk ESP32)

Adds actual speech to the CO2 ventilation and WBGT heat-stress background alerts above (previously just an on-screen bubble + notification ding):

- `main/hal/hal_tts.cpp` — thin wrapper around AquesTalk ESP32's rule-synthesis engine (`Hal::speakSymbols()`): feeds an AquesTalk phonetic symbol string through `CAqTkPicoF_SetKoe`/`SyntheFrame`, upsamples the 8kHz output 3x (`AqResample_Conv`) to match this board's 24kHz audio pipeline, and writes it straight to the avatar's own speaker.
- `main/hal/board/hal_bridge.h`/`stackchan.cc` (`board_output_pcm`) — writes a raw PCM buffer directly to `Board::GetAudioCodec()`, bypassing `AudioService`'s decode/playback queue (which only speaks pre-encoded OGG assets). Not synchronized with the AI's own voice output, so callers must check `hal_bridge::is_xiaozhi_idle()` first — the alert handlers in `hal.cpp` skip the spoken part (keeping the visual+ding) if a conversation is in progress, rather than risk corrupting either audio stream.
- `main/hal/hal.cpp` (`onCo2VentilationAlert`/`onWbgtAlert` handlers) — speaks e.g. "二酸化炭素濃度は812ピーピーエムです。換気してください。" using AquesTalk's `<NUMK VAL=... COUNTER=...>` digit-reading tag, which renders arbitrary numbers with correct Japanese sound changes (rendaku, gemination, etc.) entirely offline and without needing AquesTalk's ~2MB kanji dictionary — the fixed part of each phrase is a hand-written phonetic template, only the number is generated at runtime.

Note: AquesTalk ESP32 is closed-source and its evaluation build has a fixed limitation (the な/ま row is pronounced "ヌ") until a paid license key is set — same category of restriction as the BSEC note above, so **the SDK itself is not committed to this repo** (`firmware/components/` is already gitignored). See "AquesTalk ESP32 SDK" under Build below for how to obtain and place it before building this fork.

## Build

### AquesTalk ESP32 SDK (required for spoken alerts)

The CO2/WBGT spoken-alert feature above links against [AquesTalk ESP32](https://www.a-quest.com/products/aquestalk_esp32.html), a proprietary speech-synthesis SDK. It isn't part of this repo (see the license note above), so it has to be placed manually:

1. Download the evaluation SDK zip (or a licensed build, once purchased) from the AquesTalk ESP32 product page.
2. Copy these two files out of the zip into `firmware/components/aquestalk/`:
   - `src/aquestalk.h` → `firmware/components/aquestalk/include/aquestalk.h`
   - `src/esp32s3/libaquestalk_s.a` → `firmware/components/aquestalk/lib/esp32s3/libaquestalk_s.a`
3. `firmware/components/aquestalk/CMakeLists.txt` (checked into this repo) wraps the prebuilt library as an ESP-IDF component; nothing else to configure.

The kanji dictionary (`aq_dic/aqdic_m.bin`) is **not** used or needed — see the note above about the `<NUMK>` tag avoiding it. If it's ever wired in later, be aware it's ~2MB and this board's 16MB flash only has ~60KB of headroom left after the existing partitions (nvs/ota_0/ota_1/assets/coredump), so it wouldn't fit without shrinking something (`ota_1`, most likely, since automatic OTA is disabled — see above).

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
