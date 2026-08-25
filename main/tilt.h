#pragma once

#include "esp_err.h"

struct TiltReading {
    float accel_x_g = 0;
    float accel_y_g = 0;
    float accel_z_g = 0;
};

// Starts the BMA423 on the shared I2C bus 0 (see power.h) in NORMAL mode.
// The accelerometer stays on permanently (CLAUDE.md section 9: it's the
// always-on wake source, µA-range draw — never power-gate it).
esp_err_t tilt_init(void);

TiltReading tilt_read(void);
