#pragma once

#include <cstdint>

struct BleDeviceInfo {
    char name[25] = {0};    // advertised name, or "" when the device advertises none
    uint8_t addr[6] = {0};  // NimBLE byte order (addr[5] is printed first)
    int8_t rssi = 0;
};

struct BleScanResult {
    bool scanning = false;
    uint8_t count = 0;
    BleDeviceInfo devs[10];
};

// Starts a one-shot BLE scan: brings up the NimBLE host + controller, runs a
// bounded discovery window, stores the results, then fully tears the stack
// back down before returning — CLAUDE.md section 9: the radio is only on
// while a scan is actually running. No-op if a scan is already in progress.
// Safe to call repeatedly (on view focus, or on a manual rescan tap).
void ble_scan_start(void);

BleScanResult ble_scan_read(void);
