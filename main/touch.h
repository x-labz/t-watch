#pragma once

#include <cstdint>

#include "esp_err.h"

// FT6336 capacitive touch on I2C bus 1. Read directly rather than through
// LovyanGFX's Touch_FT5x06: that driver's chip-ID handshake fails on this
// unit, leaving touch permanently dead.
esp_err_t touch_init(void);

// True when a finger is down; fills *x/*y with raw panel coordinates.
// Note the panel reports X mirrored relative to the display on this unit —
// callers correct for it (see ui_task.cpp).
bool touch_read(int32_t *x, int32_t *y);

// --- diagnostics / recovery ---------------------------------------------
// touch_read() has to swallow I2C errors to stay cheap in the poll loop, so
// these expose what it saw. A controller that has wedged (e.g. its LDO3 rail
// dipped during a radio transmit) NACKs forever until EXTEN-reset.
uint32_t touch_error_count(void);          // consecutive failed reads
esp_err_t touch_probe(void);               // does 0x38 ACK right now?
esp_err_t touch_recover(void);             // EXTEN reset + re-add I2C device
void touch_scan_bus(void);                 // log every address that ACKs on I2C1
