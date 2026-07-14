#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "BME68x_SensorAPI/bme68x.h"

struct Bme688Data {
    float temperature_c;
    float pressure_hpa;
    float humidity_percent;
    float gas_resistance_ohm;
    bool gas_valid;  // true if the gas heater reached a stable temperature this cycle
};

class Bme688 {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = BME68X_I2C_ADDR_HIGH;  // 0x77 (M5Stack ENV Pro)

    Bme688(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr = DEFAULT_ADDRESS);
    ~Bme688();

    /**
     * @brief Initialize the device and configure temperature/pressure/humidity
     * oversampling, plus the gas heater (300C for 100ms) used for the raw gas
     * resistance reading.
     *
     * @return true if successful
     * @return false if failed
     */
    bool begin();

    /**
     * @brief Trigger a forced-mode measurement and block until it completes
     * (typically tens of milliseconds), then return the result.
     *
     * @return true if successful
     * @return false if failed
     */
    bool readMeasurement(Bme688Data& out);

private:
    i2c_master_dev_handle_t _i2c_dev;
    struct bme68x_dev _dev;
    struct bme68x_conf _conf;
    uint32_t _heater_dur_us;
    uint8_t _addr;
    bool _initialized;

    static BME68X_INTF_RET_TYPE bme68x_i2c_read(uint8_t reg_addr, uint8_t* reg_data, uint32_t len, void* intf_ptr);
    static BME68X_INTF_RET_TYPE bme68x_i2c_write(uint8_t reg_addr, const uint8_t* reg_data, uint32_t len,
                                                 void* intf_ptr);
    static void bme68x_delay_us(uint32_t period, void* intf_ptr);
};
