#pragma once

#include <cstdint>

// See CLAUDE.md section 6. Two views for now: WATCHFACE and BATTERY, laid
// out left-to-right (BATTERY is to the right of WATCHFACE).
enum class ViewId : uint8_t {
    WATCHFACE = 0,
    BATTERY = 1,
    COUNT,
    NONE = 0xFF,
};
