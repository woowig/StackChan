#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/scd41/scd41.h"
#include <mooncake_log.h>
#include <memory>
#include <freertos/semphr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const std::string_view _tag = "HAL-CO2";

static constexpr int kSingleShotTimeoutMs = 5500;
static constexpr int kSingleShotPollMs    = 100;

// Ventilation guidance threshold (1000ppm, per Japan's Building Sanitation
// Law standard for indoor CO2). The background monitor alerts once per
// crossing and re-arms only after the reading drops back below it, so it
// doesn't repeat every cycle while the room stays stuffy.
static constexpr uint16_t kVentilationThresholdPpm = 1000;
static constexpr uint32_t kMonitorIntervalMs        = 10 * 60 * 1000;

static std::unique_ptr<Scd41> _scd41;
static SemaphoreHandle_t _co2_mutex = nullptr;

static void _co2_monitor_task(void* param)
{
    bool alerted = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(kMonitorIntervalMs));

        auto reading = GetHAL().getCo2Reading();
        if (!reading.valid) {
            continue;
        }

        if (reading.co2_ppm >= kVentilationThresholdPpm) {
            if (!alerted) {
                alerted = true;
                GetHAL().onCo2VentilationAlert.emit(reading.co2_ppm);
            }
        } else {
            alerted = false;
        }
    }
}

void Hal::co2_init()
{
    mclog::tagInfo(_tag, "init");

    _co2_mutex = xSemaphoreCreateMutex();

    auto i2c_bus = hal_bridge::board_get_external_i2c_bus();
    _scd41       = std::make_unique<Scd41>(i2c_bus, Scd41::DEFAULT_ADDRESS);
    if (!_scd41->begin()) {
        _scd41.reset();
        mclog::tagError(_tag, "SCD41 init failed");
        return;
    }
    mclog::tagInfo(_tag, "SCD41 init ok");

    xTaskCreatePinnedToCore(_co2_monitor_task, "co2_monitor", 4096, NULL, 2, NULL, 1);
}

Co2Reading Hal::getCo2Reading()
{
    Co2Reading result{};
    if (!_scd41) {
        return result;
    }

    xSemaphoreTake(_co2_mutex, portMAX_DELAY);

    if (!_scd41->measureSingleShot()) {
        xSemaphoreGive(_co2_mutex);
        return result;
    }

    bool ready = false;
    for (int waited = 0; waited < kSingleShotTimeoutMs && !ready; waited += kSingleShotPollMs) {
        vTaskDelay(pdMS_TO_TICKS(kSingleShotPollMs));
        ready = _scd41->isDataReady();
    }

    if (ready) {
        Scd41Measurement m;
        if (_scd41->readMeasurement(m)) {
            result.valid            = true;
            result.co2_ppm          = m.co2_ppm;
            result.temperature_c    = m.temperature_c;
            result.humidity_percent = m.humidity_percent;
            mclog::tagInfo(_tag, "CO2: {} ppm, {:.1f}C, {:.1f}%RH", m.co2_ppm, m.temperature_c, m.humidity_percent);
        }
    } else {
        mclog::tagWarn(_tag, "single shot measurement timed out");
    }

    xSemaphoreGive(_co2_mutex);
    return result;
}
