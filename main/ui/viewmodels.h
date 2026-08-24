#pragma once

#include <cstdint>

// Plain-data ViewModels — no hardware, no locks. Copied by value into render
// functions. See CLAUDE.md section 6.

struct WatchfaceVM {
    uint8_t hh = 0;
    uint8_t mm = 0;
    uint8_t ss = 0;
};

struct BatteryVM {
    bool battery_connected = false;
    int percent = 0;          // 0-100, meaningless if !battery_connected
    uint16_t voltage_mv = 0;
    bool charging = false;
    bool vbus_in = false;
};
