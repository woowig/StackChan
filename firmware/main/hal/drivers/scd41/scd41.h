/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include "driver/i2c_master.h"
#include "esp_err.h"

struct Scd41Measurement {
    uint16_t co2_ppm;
    float temperature_c;
    float humidity_percent;
};

class Scd41 {
public:
    static constexpr uint8_t DEFAULT_ADDRESS = 0x62;

    explicit Scd41(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr = DEFAULT_ADDRESS);
    ~Scd41();

    /**
     * @brief Put the sensor into periodic measurement mode (one sample every 5s)
     *
     * @return true if successful
     * @return false if failed
     */
    bool begin();

    /**
     * @brief Check whether a new measurement sample is available
     */
    bool isDataReady();

    /**
     * @brief Read the latest measurement sample
     *
     * @return true if successful (CRC verified)
     * @return false if failed
     */
    bool readMeasurement(Scd41Measurement& out);

    void stopPeriodicMeasurement();

private:
    i2c_master_dev_handle_t _i2c_dev = nullptr;

    esp_err_t sendCommand(uint16_t cmd);
    esp_err_t readWords(uint8_t* data, size_t len);
    static uint8_t crc8(const uint8_t* data, size_t len);
};
