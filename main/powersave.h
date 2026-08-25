#pragma once

#include "esp_err.h"

// Power management, CLAUDE.md section 9. Two mechanisms, both opt-out rather
// than opt-in so that anything not actively working costs nothing:
//
//  * DFS — the CPU drops to 40 MHz whenever nothing needs speed, instead of
//    sitting at 240 MHz all day. Configured once here via esp_pm_configure().
//    Note CONFIG_PM_ENABLE alone does NOT do this: without the configure call
//    the chip stays pinned at max frequency, which is how this project ran
//    until now.
//  * Automatic light sleep — when every task is blocked and no lock is held,
//    the chip light-sleeps with RAM retained and resumes on any interrupt.
//
// A task that must not be interrupted holds a lock across the work. The UI
// task already does this around frame pushes (SPI needs a stable APB), and
// the same applies to any timing-sensitive or blocking peripheral work.
esp_err_t powersave_init(void);

// Holds/releases the "do not light sleep" lock. Nesting is handled by the
// underlying esp_pm lock, so acquire/release must be balanced. Prefer the
// RAII helper below in C++ scopes.
void powersave_prevent_sleep(bool prevent);

// Scope guard: keeps the chip awake for as long as it is alive.
class PowersaveHold {
public:
    PowersaveHold() { powersave_prevent_sleep(true); }
    ~PowersaveHold() { powersave_prevent_sleep(false); }
    PowersaveHold(const PowersaveHold &) = delete;
    PowersaveHold &operator=(const PowersaveHold &) = delete;
};

// Battery draw right now, in milliamps, from the AXP202's discharge-current
// ADC. Returns 0 while USB is plugged in — the system runs from VBUS then and
// the discharge path reads ~0 (CLAUDE.md section 2), so any before/after power
// comparison has to be made on battery, not over USB.
float powersave_battery_draw_ma(void);

// --- power log ----------------------------------------------------------
// Measuring this watch is awkward: the discharge ADC reads ~0 while USB is
// attached, but unplugging USB also takes the serial console away. So samples
// are buffered in RTC memory (which survives resets, and the watch stays
// powered from the battery throughout) and dumped once USB is back.
// Usage: `powerlog clear`, unplug USB, use the watch, replug, `powerlog`.
void powersave_log_sample(bool screen_on);
void powersave_log_dump(void);
void powersave_log_clear(void);

// Dumps every esp_pm lock and who holds it. CLAUDE.md section 9: "if some task
// holds a lock forever, DFS is dead" — this is how to check that claim rather
// than assume it.
void powersave_dump_locks(void);
