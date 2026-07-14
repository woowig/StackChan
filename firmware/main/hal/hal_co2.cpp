/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/scd41/scd41.h"
#include <mooncake_log.h>
#include <memory>
#include <freertos/semphr.h>

static const std::string_view _tag = "HAL-CO2";

static std::unique_ptr<Scd41> _scd41;
static SemaphoreHandle_t _co2_mutex = nullptr;
static Co2Reading _latest_reading;

static void _co2_task(void* param)
{
    // The SCD41 produces one new sample every 5 seconds in periodic
    // measurement mode.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));

        if (!_scd41 || !_scd41->isDataReady()) {
            continue;
        }

        Scd41Measurement m;
        if (!_scd41->readMeasurement(m)) {
            mclog::tagWarn(_tag, "failed to read measurement");
            continue;
        }

        xSemaphoreTake(_co2_mutex, portMAX_DELAY);
        _latest_reading = Co2Reading{
            .valid             = true,
            .co2_ppm           = m.co2_ppm,
            .temperature_c     = m.temperature_c,
            .humidity_percent  = m.humidity_percent,
        };
        xSemaphoreGive(_co2_mutex);

        mclog::tagInfo(_tag, "CO2: {} ppm, {:.1f}C, {:.1f}%RH", m.co2_ppm, m.temperature_c, m.humidity_percent);
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

    xTaskCreatePinnedToCoreWithCaps(_co2_task, "co2", 4096, NULL, 2, NULL, 1, MALLOC_CAP_SPIRAM);
}

Co2Reading Hal::getCo2Reading()
{
    Co2Reading copy{};
    if (_co2_mutex) {
        xSemaphoreTake(_co2_mutex, portMAX_DELAY);
        copy = _latest_reading;
        xSemaphoreGive(_co2_mutex);
    }
    return copy;
}
