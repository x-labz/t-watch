#pragma once

#include <cstdint>
#include "esp_err.h"

// Initializes NVS flash and loads any stored settings. Call once at boot,
// before settings_get_brightness()/settings_set_brightness() and before
// lcd.init() so the very first frame already uses the saved brightness.
// Flash (not RTC memory) is deliberate here: unlike the GPS-derived
// timezone cache, this is an explicit user choice, and it must survive a
// full power loss (battery pull), not just a reset that keeps the RTC
// domain powered.
esp_err_t settings_init(void);

// Backlight brightness, 0-255 (LovyanGFX's setBrightness scale). Defaults
// to 255 (full) the first time, before anything has ever been saved.
uint8_t settings_get_brightness(void);

// Updates the in-memory value and persists it to flash immediately.
void settings_set_brightness(uint8_t brightness);

// Seconds of no interaction before the screen blanks and the watch drops to
// its low-power idle. 0 disables the timeout (screen stays on). Persisted in
// flash for the same reason as brightness: it is an explicit user choice.
uint16_t settings_get_screen_timeout_s(void);
void settings_set_screen_timeout_s(uint16_t seconds);
