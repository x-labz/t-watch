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

// AXP202 LDO4 — GPS module power. Off by default (CLAUDE.md section 9: GPS
// is the biggest power consumer, gate it to only when a fix is being
// acquired). See gps.h for the refcounted acquire/release wrapper.
void power_gps_power(bool on);

// Pulses the AXP202 EXTEN output (off -> delay -> on), which is the FT6336's
// reset line on this board — there is no GPIO for it (CLAUDE.md section 2).
// Used both during init and to recover a touch controller that has wedged.
void power_touch_reset(void);

// True when the AXP202's EXTEN output is asserted, i.e. the FT6336 is NOT held
// in reset. Reads REG12 bit 0 directly — do not use XPowersLib's
// isEnableExternalPin(), which reads bit 6 (that is LDO3) and will report the
// panel rail's state instead.
bool power_exten_is_on(void);

// Test hook: drive EXTEN directly. `false` holds the FT6336 in reset, which
// reproduces the "touch is dead and nothing brings it back" failure exactly.
void power_set_exten(bool on);

// Reports LDO2/LDO3 enable state + voltage, for diagnosing whether a dead
// peripheral is actually an unpowered one.
void power_log_rails(void);

// Dumps the AXP202's control/voltage registers over raw I2C. Called at the
// very start of power_init(), BEFORE any of our own writes, so the log shows
// the state inherited from whatever firmware ran last — PMU registers are
// battery-backed and survive reflashes (section 3). Diffing this between our
// firmware and LilyGO's is how to find what the vendor sets that we don't.
void power_dump_axp_registers(const char *when);

// Logs every address that ACKs on I2C bus 0 (expect AXP202 0x35, BMA423 0x19,
// PCF8563 0x51, DRV2605 0x5A). A healthy bus 0 next to a silent bus 1 says the
// fault is isolated to the touch side rather than I2C/power generally.
void power_scan_bus0(void);

// Full power-cycle of LDO3 (panel + touch). Harsher than the EXTEN reset and
// the only way to truly reset the panel, which has no RST pin (section 3).
// Caller must re-run lcd.init() afterwards if it cares about the display.
void power_cycle_ldo3(void);

// AXP202 REG29 bit7 selects how LDO3 regulates. LilyGO's own driver sets this
// to DCIN whenever it powers the panel/touch rail up, and back to LDO when it
// sleeps — we never set it at all, so we inherit whatever the last firmware
// left in the PMU (its registers survive reflashes and resets). true = DCIN.
bool power_get_ldo3_dcin_mode(void);
void power_set_ldo3_dcin_mode(bool dcin);
