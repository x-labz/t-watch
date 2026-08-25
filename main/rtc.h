#pragma once

#include <ctime>

#include "esp_err.h"

// PCF8563 real-time clock on I2C bus 0. Battery-backed off the AXP202's LDO1
// backup domain, so it keeps time across resets, deep sleep and power-off —
// which is why the watch does NOT need a GPS fix just to know the time at
// boot (CLAUDE.md section 2: "sync system time from PCF8563 at every
// boot/wake; write back after NTP/GPS sync").
esp_err_t rtc_init(void);

// True when the PCF8563 reports its oscillator never lost power, i.e. the
// time it holds is trustworthy. False on a first-ever boot or after the
// backup rail was fully drained — that is the only case where an external
// time source is actually required.
bool rtc_time_is_valid(void);

// Reads the RTC and pushes it into the system clock (as UTC). No-op returning
// ESP_ERR_INVALID_STATE when the held time isn't trustworthy.
esp_err_t rtc_restore_system_time(void);

// Writes the given UTC epoch back to the PCF8563, so the next boot can skip
// the GPS entirely. Call after any authoritative sync (GPS, later NTP).
esp_err_t rtc_store_utc(time_t utc);
