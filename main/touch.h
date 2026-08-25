#pragma once

#include <cstdint>

#include "esp_err.h"

// FT6336 capacitive touch on I2C bus 1. Read directly rather than through
// LovyanGFX's Touch_FT5x06: that driver's chip-ID handshake fails on this
// unit, leaving touch permanently dead.
esp_err_t touch_init(void);

// True when a finger is down; fills *x/*y with SCREEN coordinates — origin
// top-left as drawn, matching what render functions use. The raw-panel to
// screen mapping (this unit's X mirror, plus the 180° panel rotation set in
// lgfx_twatch_v2.hpp) lives in touch.cpp so callers never deal with it.
bool touch_read(int32_t *x, int32_t *y);

// --- diagnostics / recovery ---------------------------------------------
// touch_read() has to swallow I2C errors to stay cheap in the poll loop, so
// these expose what it saw. A controller that has wedged (e.g. its LDO3 rail
// dipped during a radio transmit) NACKs forever until EXTEN-reset.
uint32_t touch_error_count(void);          // consecutive failed reads
esp_err_t touch_probe(void);               // does 0x38 ACK right now?
esp_err_t touch_recover(void);             // EXTEN reset + re-add I2C device
void touch_scan_bus(void);                 // log every address that ACKs on I2C1

// Test hook: drops the controller into DEEPSLEEP, reproducing the "touch is
// dead and never comes back" failure so the recovery path can be verified.
esp_err_t touch_force_deepsleep(void);

// Dumps the FT6336's mode/config/status registers. For the case where the chip
// answers I2C fine but never reports a touch point — the registers say which
// mode it is actually sitting in.
void touch_dump_registers(void);

// Polls the touch status register for `seconds` and logs every non-zero /
// changed reading, so a human touching the panel produces visible evidence
// independent of the UI task's gesture handling.
void touch_monitor_raw(int seconds);

// Write an arbitrary FT6336 register, so configuration hypotheses can be
// tested from the console without a reflash (brownouts make each one costly).
esp_err_t touch_write_reg(uint8_t reg, uint8_t val);
