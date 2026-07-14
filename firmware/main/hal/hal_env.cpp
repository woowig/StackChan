#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/bme688/bme688.h"
#include <mooncake_log.h>
#include <memory>
#include <freertos/semphr.h>

static const std::string_view _tag = "HAL-ENV";

static std::unique_ptr<Bme688> _bme688;
static SemaphoreHandle_t _env_mutex = nullptr;

// Rough, community-style "air quality score" heuristic (0-100, higher =
// cleaner air), combining humidity and the sensor's raw gas resistance
// against a self-adjusting baseline. This is NOT Bosch's BSEC IAQ index —
// BSEC is a closed-source library licensed for business use only and
// cannot be redistributed in this repo. The gas baseline starts from the
// first reading and self-calibrates over subsequent queries; it resets on
// reboot, so accuracy improves the more often the sensor is queried.
namespace {
constexpr float kHumidityBaselinePercent = 40.0f;
constexpr float kHumidityWeighting       = 0.25f;
constexpr float kGasWeighting            = 0.75f;

float computeAirQualityPercent(float humidity_percent, float gas_resistance_ohm, float gas_baseline_ohm)
{
    float hum_offset = humidity_percent - kHumidityBaselinePercent;
    float hum_score;
    if (hum_offset > 0) {
        hum_score = (100.0f - kHumidityBaselinePercent - hum_offset) / (100.0f - kHumidityBaselinePercent) *
                    kHumidityWeighting * 100.0f;
    } else {
        hum_score = (kHumidityBaselinePercent + hum_offset) / kHumidityBaselinePercent * kHumidityWeighting * 100.0f;
    }
    if (hum_score < 0) {
        hum_score = 0;
    }

    float gas_score;
    if (gas_baseline_ohm > 0 && gas_resistance_ohm < gas_baseline_ohm) {
        gas_score = (gas_resistance_ohm / gas_baseline_ohm) * kGasWeighting * 100.0f;
    } else {
        gas_score = kGasWeighting * 100.0f;
    }

    float score = hum_score + gas_score;
    if (score > 100.0f) score = 100.0f;
    if (score < 0.0f) score = 0.0f;
    return score;
}
}  // namespace

static float _gas_baseline_ohm = 0;
static bool _gas_baseline_set  = false;

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

        if (data.gas_valid) {
            if (!_gas_baseline_set || data.gas_resistance_ohm > _gas_baseline_ohm) {
                _gas_baseline_ohm = data.gas_resistance_ohm;
            } else {
                _gas_baseline_ohm = _gas_baseline_ohm * 0.98f + data.gas_resistance_ohm * 0.02f;
            }
            _gas_baseline_set = true;

            result.air_quality_valid   = true;
            result.air_quality_percent = computeAirQualityPercent(data.humidity_percent, data.gas_resistance_ohm,
                                                                    _gas_baseline_ohm);
        }

        mclog::tagInfo(_tag, "ENV: {:.1f}hPa, {:.1f}C, {:.1f}%RH, gas={:.0f}ohm (valid={}), aq={:.0f}%",
                       data.pressure_hpa, data.temperature_c, data.humidity_percent, data.gas_resistance_ohm,
                       data.gas_valid, result.air_quality_percent);
    } else {
        mclog::tagWarn(_tag, "measurement failed");
    }

    xSemaphoreGive(_env_mutex);
    return result;
}
