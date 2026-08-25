#pragma once

#include <cstdint>

// Plain-data ViewModels — no hardware, no locks. Copied by value into render
// functions. See CLAUDE.md section 6.

struct WatchfaceVM {
    uint8_t hh = 0;
    uint8_t mm = 0;
    uint8_t ss = 0;
    bool gps_sync_blink = false;   // toggled ~1Hz while boot-time GPS time sync is in progress
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

struct HapticVM {
    uint16_t effect_id = 1;
};

struct SettingsVM {
    uint8_t brightness_pct = 100;   // 0-100, for display only
};

struct WifiApVM {
    char ssid[33] = {0};
    int8_t rssi = 0;
};

struct WifiVM {
    bool scanning = false;
    uint8_t count = 0;
    WifiApVM aps[10];
};

struct BleDevVM {
    char name[25] = {0};    // empty when the device advertises no name
    char addr[18] = {0};    // pre-formatted "AA:BB:CC:DD:EE:FF"
    int8_t rssi = 0;
};

struct BleVM {
    bool scanning = false;
    uint8_t count = 0;
    BleDevVM devs[10];
};
