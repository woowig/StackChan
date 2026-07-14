#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/bme688/bme688.h"
#include <mooncake_log.h>
#include <memory>
#include <freertos/semphr.h>

static const std::string_view _tag = "HAL-ENV";

static std::unique_ptr<Bme688> _bme688;
static SemaphoreHandle_t _env_mutex = nullptr;

void Hal::env_init()
{
    mclog::tagInfo(_tag, "init");

    _env_mutex = xSemaphoreCreateMutex();

    auto i2c_bus = hal_bridge::board_get_external_i2c_bus();
    _bme688      = std::make_unique<Bme688>(i2c_bus, Bme688::DEFAULT_ADDRESS);
    if (!_bme688->begin()) {
        _bme688.reset();
        mclog::tagError(_tag, "BME688 init failed");
        return;
    }
    mclog::tagInfo(_tag, "BME688 init ok");
}

EnvReading Hal::getEnvReading()
{
    EnvReading result{};
    if (!_bme688) {
        return result;
    }

    xSemaphoreTake(_env_mutex, portMAX_DELAY);

    Bme688Data data;
    if (_bme688->readMeasurement(data)) {
        result.valid            = true;
        result.temperature_c    = data.temperature_c;
        result.pressure_hpa     = data.pressure_hpa;
        result.humidity_percent = data.humidity_percent;
        mclog::tagInfo(_tag, "ENV: {:.1f}hPa, {:.1f}C, {:.1f}%RH", data.pressure_hpa, data.temperature_c,
                       data.humidity_percent);
    } else {
        mclog::tagWarn(_tag, "measurement failed");
    }

    xSemaphoreGive(_env_mutex);
    return result;
}
