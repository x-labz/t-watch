#pragma once

#include <cstdint>
#include "esp_err.h"

// Direct FT6336 register reader — bypasses LovyanGFX's Touch_FT5x06 driver,
// whose _check_init() requires a CIPHER-register identification handshake
// that fails on this unit's chip and permanently locks out all touch reads
// (confirmed by comparison with LilyGO's own vendor driver, which never
// performs any such handshake and works fine). See CLAUDE.md section 2 for
// the FT6336 address/pins.
esp_err_t touch_init(void);

// Returns true if a finger is currently down, filling in panel-space x/y.
bool touch_read(int32_t *x, int32_t *y);
