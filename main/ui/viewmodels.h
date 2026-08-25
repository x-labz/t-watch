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

struct GpsVM {
    bool has_fix = false;
    uint8_t satellites_used = 0;
    uint8_t satellites_in_view = 0;
    float hdop = 0;
    float altitude_m = 0;
    float speed_kmh = 0;
    double latitude = 0;
    double longitude = 0;
    uint8_t utc_hh = 0, utc_mm = 0, utc_ss = 0;
    uint32_t sentence_count = 0;
};

struct TiltVM {
    float accel_x_g = 0;
    float accel_y_g = 0;
};
