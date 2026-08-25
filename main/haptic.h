#pragma once

#include <cstdint>
#include "esp_err.h"

// Starts the DRV2605 on the shared I2C bus 0 (see power.h) with ERM ROM
// library 1 selected (this board's motor is a small ERM, not LRA).
esp_err_t haptic_init(void);

// Triggers ROM effect `effect_id` (1-123). Non-blocking — the chip plays it
// internally. Returns false if the driver isn't ready or the trigger failed.
bool haptic_play(uint16_t effect_id);
