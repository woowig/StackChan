#include "hal.h"
#include "board/hal_bridge.h"
#include "drivers/bme688/bme688.h"
#include <mooncake_log.h>
#include <memory>
#include <freertos/semphr.h>
#include <cmath>

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

// Wet-bulb temperature approximation from dry-bulb temperature and relative
// humidity (Stull, 2011, "Wet-Bulb Temperature from Relative Humidity and Air
// Temperature", J. Appl. Meteor. Climatol. 50). Valid for RH 5-99% and
// temperature -20 to 50C; mean absolute error < 0.3C over that range.
float computeWetBulbCelsius(float temperature_c, float humidity_percent)
{
    return temperature_c * atanf(0.151977f * sqrtf(humidity_percent + 8.313659f)) +
           atanf(temperature_c + humidity_percent) - atanf(humidity_percent - 1.676331f) +
           0.00391838f * powf(humidity_percent, 1.5f) * atanf(0.023101f * humidity_percent) - 4.686035f;
}

// Indoor WBGT (heat stress index) per Japan's Ministry of the Environment
// formula for indoor/no-solar-radiation conditions:
// WBGT = 0.7 * wet-bulb temperature + 0.3 * globe temperature. Without a
// physical black globe thermometer, globe temperature is approximated by
// air temperature, which is standard practice indoors away from direct heat
// sources but will read low near strong radiant heat (e.g. direct sun,
// stoves).
float computeIndoorWbgtCelsius(float temperature_c, float humidity_percent)
{
    float wet_bulb_c = computeWetBulbCelsius(temperature_c, humidity_percent);
    return 0.7f * wet_bulb_c + 0.3f * temperature_c;
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
        result.wbgt_celsius     = computeIndoorWbgtCelsius(data.temperature_c, data.humidity_percent);

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

        mclog::tagInfo(_tag, "ENV: {:.1f}hPa, {:.1f}C, {:.1f}%RH, gas={:.0f}ohm (valid={}), aq={:.0f}%, wbgt={:.1f}C",
                       data.pressure_hpa, data.temperature_c, data.humidity_percent, data.gas_resistance_ohm,
                       data.gas_valid, result.air_quality_percent, result.wbgt_celsius);
    } else {
        mclog::tagWarn(_tag, "measurement failed");
    }

    xSemaphoreGive(_env_mutex);
    return result;
}
