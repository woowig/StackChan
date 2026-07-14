#include "bme688.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "BME688";

Bme688::Bme688(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr) : _addr(addr), _initialized(false)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _addr,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &_i2c_dev));

    memset(&_dev, 0, sizeof(_dev));
    _dev.intf     = BME68X_I2C_INTF;
    _dev.read     = bme68x_i2c_read;
    _dev.write    = bme68x_i2c_write;
    _dev.delay_us = bme68x_delay_us;
    _dev.intf_ptr = &_i2c_dev;
    _dev.amb_temp = 25;
}

Bme688::~Bme688()
{
    if (_i2c_dev) {
        i2c_master_bus_rm_device(_i2c_dev);
    }
}

BME68X_INTF_RET_TYPE Bme688::bme68x_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr)
{
    i2c_master_dev_handle_t dev = *(i2c_master_dev_handle_t*)intf_ptr;
    esp_err_t err                = i2c_master_transmit_receive(dev, &reg_addr, 1, reg_data, len, 1000);
    return (err == ESP_OK) ? 0 : -1;
}

BME68X_INTF_RET_TYPE Bme688::bme68x_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len,
                                              void* intf_ptr)
{
    i2c_master_dev_handle_t dev = *(i2c_master_dev_handle_t*)intf_ptr;

    uint8_t* buf = (uint8_t*)malloc(len + 1);
    if (!buf) return -1;

    buf[0] = reg_addr;
    memcpy(buf + 1, reg_data, len);

    esp_err_t err = i2c_master_transmit(dev, buf, len + 1, 1000);
    free(buf);

    return (err == ESP_OK) ? 0 : -1;
}

void Bme688::bme68x_delay_us(uint32_t period, void* intf_ptr)
{
    if (period >= 1000) {
        vTaskDelay(pdMS_TO_TICKS(period / 1000));
    } else if (period) {
        esp_rom_delay_us(period);
    }
}

bool Bme688::begin()
{
    int8_t rslt = bme68x_init(&_dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_init failed: %d", rslt);
        return false;
    }

    _conf.filter  = BME68X_FILTER_OFF;
    _conf.odr     = BME68X_ODR_NONE;
    _conf.os_hum  = BME68X_OS_2X;
    _conf.os_pres = BME68X_OS_4X;
    _conf.os_temp = BME68X_OS_2X;
    rslt          = bme68x_set_conf(&_conf, &_dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_conf failed: %d", rslt);
        return false;
    }

    // Gas heater: produces a raw gas resistance reading used for a rough,
    // uncalibrated air quality heuristic (see hal_env.cpp). This is NOT
    // Bosch's BSEC algorithm (closed-source, business-use-only license) -
    // just the raw resistance value the sensor API exposes without it.
    struct bme68x_heatr_conf heatr_conf = {};
    heatr_conf.enable                   = BME68X_ENABLE;
    heatr_conf.heatr_temp               = 300;
    heatr_conf.heatr_dur                = 100;
    rslt                                = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &_dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_heatr_conf failed: %d", rslt);
        return false;
    }
    _heater_dur_us = (uint32_t)heatr_conf.heatr_dur * 1000;

    _initialized = true;
    return true;
}

bool Bme688::readMeasurement(Bme688Data& out)
{
    if (!_initialized) {
        return false;
    }

    int8_t rslt = bme68x_set_op_mode(BME68X_FORCED_MODE, &_dev);
    if (rslt != BME68X_OK) {
        ESP_LOGE(TAG, "bme68x_set_op_mode failed: %d", rslt);
        return false;
    }

    uint32_t del_period = bme68x_get_meas_dur(BME68X_FORCED_MODE, &_conf, &_dev) + _heater_dur_us;
    _dev.delay_us(del_period, _dev.intf_ptr);

    struct bme68x_data data;
    uint8_t n_fields = 0;
    rslt             = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &_dev);
    if (rslt != BME68X_OK || n_fields == 0) {
        ESP_LOGW(TAG, "bme68x_get_data failed: %d, n_fields=%d", rslt, n_fields);
        return false;
    }

    out.temperature_c      = data.temperature;
    out.pressure_hpa       = data.pressure / 100.0f;
    out.humidity_percent   = data.humidity;
    out.gas_resistance_ohm = data.gas_resistance;
    out.gas_valid          = (data.status & BME68X_GASM_VALID_MSK) && (data.status & BME68X_HEAT_STAB_MSK);
    return true;
}
