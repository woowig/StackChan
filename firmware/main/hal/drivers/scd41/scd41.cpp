/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "scd41.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "SCD41";

namespace {
constexpr uint16_t CMD_STOP_PERIODIC_MEASUREMENT  = 0x3f86;
constexpr uint16_t CMD_START_PERIODIC_MEASUREMENT = 0x21b1;
constexpr uint16_t CMD_READ_MEASUREMENT           = 0xec05;
constexpr uint16_t CMD_GET_DATA_READY_STATUS      = 0xe4b8;
}  // namespace

Scd41::Scd41(i2c_master_bus_handle_t i2c_bus_handle, uint8_t addr)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,  // SCD4x is specified for standard mode (100kHz)
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &_i2c_dev));
}

Scd41::~Scd41()
{
    if (_i2c_dev) {
        i2c_master_bus_rm_device(_i2c_dev);
    }
}

uint8_t Scd41::crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xff;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

esp_err_t Scd41::sendCommand(uint16_t cmd)
{
    uint8_t buf[2] = {(uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xff)};
    return i2c_master_transmit(_i2c_dev, buf, sizeof(buf), 1000);
}

esp_err_t Scd41::readWords(uint8_t* data, size_t len)
{
    return i2c_master_receive(_i2c_dev, data, len, 1000);
}

bool Scd41::begin()
{
    // Force a known state: stop periodic measurement if it happens to be
    // running already (e.g. after a warm reboot), then start it fresh.
    stopPeriodicMeasurement();

    if (sendCommand(CMD_START_PERIODIC_MEASUREMENT) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start periodic measurement");
        return false;
    }
    return true;
}

bool Scd41::isDataReady()
{
    if (sendCommand(CMD_GET_DATA_READY_STATUS) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t raw[3];
    if (readWords(raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    if (crc8(raw, 2) != raw[2]) {
        return false;
    }

    uint16_t status = (raw[0] << 8) | raw[1];
    return (status & 0x07ff) != 0;
}

bool Scd41::readMeasurement(Scd41Measurement& out)
{
    if (sendCommand(CMD_READ_MEASUREMENT) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t raw[9];
    if (readWords(raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < 9; i += 3) {
        if (crc8(&raw[i], 2) != raw[i + 2]) {
            ESP_LOGW(TAG, "CRC mismatch reading measurement");
            return false;
        }
    }

    uint16_t co2_raw  = (raw[0] << 8) | raw[1];
    uint16_t temp_raw = (raw[3] << 8) | raw[4];
    uint16_t rh_raw   = (raw[6] << 8) | raw[7];

    out.co2_ppm          = co2_raw;
    out.temperature_c    = -45.0f + 175.0f * (temp_raw / 65536.0f);
    out.humidity_percent = 100.0f * (rh_raw / 65536.0f);
    return true;
}

void Scd41::stopPeriodicMeasurement()
{
    sendCommand(CMD_STOP_PERIODIC_MEASUREMENT);
    vTaskDelay(pdMS_TO_TICKS(500));
}
