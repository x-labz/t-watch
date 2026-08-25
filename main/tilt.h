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

// False when the BMA423 never came up; tilt_read() then returns zeros rather
// than stale/garbage data, and the watch runs normally without it.
bool tilt_available(void);

// Diagnoses a BMA423 that ACKs on the bus but whose driver init fails: probes
// the address, then raw-reads the chip-ID register (0x00, expect 0x13) without
// going through SensorLib, to separate "the chip is not talking" from "the
// library's plumbing is broken". Retries a soft reset if the ID does not read.
void tilt_diagnose(void);

// Re-runs the full SensorLib init on demand. Lets us tell "the chip needs more
// time after power-up" apart from "the library cannot talk to it at all":
// if this succeeds seconds after boot when boot-time init failed, it is timing.
esp_err_t tilt_retry_init(void);
