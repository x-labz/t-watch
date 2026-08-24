#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

// Runs the section-3 critical init order: brings up I2C bus 0, starts the
// AXP202 PMU, enables LDO3 (panel+touch) and LDO2 (backlight), and resets the
// touch controller via EXTEN. Must complete before lcd.init() is ever called.
esp_err_t power_init(void);

// I2C bus 0 (SDA/SCL from twatch_v2_pins.h) — shared by AXP202, BMA423,
// PCF8563, DRV2605. Valid only after power_init() returns ESP_OK.
i2c_master_bus_handle_t power_get_i2c_bus0(void);

struct BatteryReading {
    bool battery_connected = false;
    int percent = 0;          // 0-100, meaningless if !battery_connected
    uint16_t voltage_mv = 0;
    bool charging = false;
    bool vbus_in = false;
};

// Reads the AXP202 fuel gauge. Valid only after power_init() returns ESP_OK.
BatteryReading power_read_battery(void);
