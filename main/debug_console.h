#pragma once

#include <cstdint>

// Serial command console for driving the UI without physical touch — needed
// during hardware bring-up when a bug (e.g. a view crash) is easier to
// reproduce from a scripted serial command than from a repeated physical
// swipe/tap. Shares the console UART with ESP_LOGx output (CLAUDE.md section
// 11: no separate debug UART on this board). Commands, one per line:
//   view <name>              - jump directly to a view (watchface, battery,
//                               gps, tilt, haptic, settings, wifi, ble)
//   next / prev               - swipe to the next/previous view
//   tap [left|right|center]   - simulate a tap (default center)
//   status                    - log the current view + key state
//   touch                     - report touch I2C health (error count, probe)
//   touchfix                  - EXTEN-reset the FT6336 and re-add it
//   touchsleep                - force FT6336 into DEEPSLEEP (reproduces dead touch)
//   touchdump                 - dump FT6336 mode/config/status registers
//   touchmon                  - poll raw touch status for 10s while you touch
//   treg <hex> <hex>          - write an FT6336 register (e.g. `treg A4 0`)
//   exten 0|1                 - drive the touch reset line; 0 reproduces dead touch
//   bma                       - diagnose the BMA423 (probe, raw chip id, soft reset)
//   bmainit                   - re-run the BMA423 init now
//   power                     - battery voltage + discharge mA (measure on battery!)
//   sleep                     - blank the screen now, without waiting for the timeout
//   timeout <sec>             - set the screen timeout (0 = never), persisted
//   pmlocks                   - dump esp_pm locks; verifies DFS is really engaging
//   powerlog [clear]          - dump / reset the RTC-memory power trace.
//                               To measure for real: `powerlog clear`, unplug
//                               USB, use the watch, replug, `powerlog`.

enum class DebugCmdType : uint8_t {
    NONE,
    GOTO_VIEW,
    NEXT_VIEW,
    PREV_VIEW,
    TAP,
    STATUS,
    TOUCH_INFO,
    TOUCH_FIX,
    LDO3_MODE,
    TOUCH_SLEEP,
    TOUCH_DUMP,
    TOUCH_MON,
    TOUCH_WRITE,
    EXTEN_SET,
    BMA_DIAG,
    BMA_RETRY,
    POWER_INFO,
    SCREEN_OFF,
    TIMEOUT_SET,
    POWERLOG_DUMP,
    POWERLOG_CLEAR,
    PM_LOCKS,
};

struct DebugCmd {
    DebugCmdType type = DebugCmdType::NONE;
    uint8_t view_index = 0;    // GOTO_VIEW: index into ViewId
    int32_t tap_x = 120;       // TAP: screen-space x, 0-239
    uint8_t reg = 0;           // TOUCH_WRITE: register
    uint8_t val = 0;           // TOUCH_WRITE: value
    uint16_t seconds = 0;      // TIMEOUT_SET: screen timeout
    bool flag = false;         // LDO3_MODE: true = DCIN, false = LDO
};

// Starts a background task that reads newline-terminated commands from
// stdin (the console UART) and queues them for the UI task to consume via
// debug_console_poll(). Safe to call once at boot, before or after the UI
// task starts.
void debug_console_start(void);

// Non-blocking: returns true and fills *cmd if a command is pending.
bool debug_console_poll(DebugCmd *cmd);

// Console traffic pins the chip awake for a few seconds so that a burst of
// commands is not swallowed by light sleep. Call periodically from the UI
// loop to drop that hold once the console has gone quiet.
void debug_console_release_if_idle(void);
