#pragma once

#include <cstdint>

// See CLAUDE.md section 6. Views laid out left-to-right, in this order:
// WATCHFACE, BATTERY, GPS, TILT, HAPTIC, SETTINGS, WIFI.
enum class ViewId : uint8_t {
    WATCHFACE = 0,
    BATTERY = 1,
    GPS = 2,
    TILT = 3,
    HAPTIC = 4,
    SETTINGS = 5,
    WIFI = 6,
    COUNT,
    NONE = 0xFF,
};
