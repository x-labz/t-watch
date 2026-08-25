#pragma once

#include <cstdint>

struct WifiApInfo {
    char ssid[33] = {0};
    int8_t rssi = 0;
};

struct WifiScanResult {
    bool scanning = false;
    uint8_t count = 0;
    WifiApInfo aps[10];
};

// Starts a one-shot async scan: brings up WiFi STA, scans, stores the
// results, then fully tears WiFi back down (esp_wifi_stop + deinit) before
// returning — CLAUDE.md section 9: never leave STA idling. No-op if a scan
// is already running. Safe to call repeatedly (e.g. on view focus, or on a
// manual rescan tap).
void wifi_scan_start(void);

WifiScanResult wifi_scan_read(void);
