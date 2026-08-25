#include "debug_console.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "powersave.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "DBGCON";
static QueueHandle_t s_queue;

// How long the console keeps the chip awake after the last received line.
static constexpr int64_t kConsoleAwakeUs = 15LL * 1000000;
static bool s_console_awake = false;
static int64_t s_console_active_until_us = 0;

// Lets the chip sleep again once the console has been quiet. Called from the
// UI task's loop so no extra task is needed for it.
void debug_console_release_if_idle(void)
{
    if (s_console_awake && esp_timer_get_time() > s_console_active_until_us) {
        s_console_awake = false;
        powersave_prevent_sleep(false);
    }
}

// Order matches ViewId in ui/view.h — kept as plain strings here rather than
// pulling in ui/view.h, since this console has no other reason to know about
// view rendering.
static const char *kViewNames[] = {
    "watchface", "battery", "gps", "tilt", "haptic", "settings", "wifi", "ble",
};
static constexpr int kViewCount = sizeof(kViewNames) / sizeof(kViewNames[0]);

static void trim_trailing_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

static void lowercase_inplace(char *s)
{
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

static void debug_console_task(void *)
{
    char line[64];
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        trim_trailing_newline(line);
        lowercase_inplace(line);

        // Keep the chip awake for a while after ANY console traffic, including
        // the bare newline used to wake it. Light sleep stops the UART clock,
        // so without this only the first command of a burst lands and the rest
        // vanish silently — which reads exactly like "the firmware is broken"
        // and cost real debugging time. A blank line is therefore a useful
        // command in itself: it wakes the console and holds it open.
        if (!s_console_awake) {
            s_console_awake = true;
            powersave_prevent_sleep(true);
        }
        s_console_active_until_us = esp_timer_get_time() + kConsoleAwakeUs;

        if (line[0] == '\0') continue;

        DebugCmd cmd;
        if (strncmp(line, "view ", 5) == 0) {
            const char *name = line + 5;
            int found = -1;
            for (int i = 0; i < kViewCount; i++) {
                if (strcmp(name, kViewNames[i]) == 0) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                ESP_LOGW(TAG, "unknown view '%s'", name);
                continue;
            }
            cmd.type = DebugCmdType::GOTO_VIEW;
            cmd.view_index = (uint8_t)found;
        } else if (strcmp(line, "next") == 0) {
            cmd.type = DebugCmdType::NEXT_VIEW;
        } else if (strcmp(line, "prev") == 0) {
            cmd.type = DebugCmdType::PREV_VIEW;
        } else if (strncmp(line, "tap", 3) == 0) {
            const char *arg = line + 3;
            while (*arg == ' ') arg++;
            cmd.type = DebugCmdType::TAP;
            if (strcmp(arg, "left") == 0) cmd.tap_x = 40;
            else if (strcmp(arg, "right") == 0) cmd.tap_x = 200;
            else cmd.tap_x = 120;
        } else if (strcmp(line, "status") == 0) {
            cmd.type = DebugCmdType::STATUS;
        } else if (strcmp(line, "touch") == 0) {
            cmd.type = DebugCmdType::TOUCH_INFO;
        } else if (strcmp(line, "touchfix") == 0) {
            cmd.type = DebugCmdType::TOUCH_FIX;
        } else if (strcmp(line, "touchsleep") == 0) {
            cmd.type = DebugCmdType::TOUCH_SLEEP;
        } else if (strcmp(line, "touchdump") == 0) {
            cmd.type = DebugCmdType::TOUCH_DUMP;
        } else if (strcmp(line, "touchmon") == 0) {
            cmd.type = DebugCmdType::TOUCH_MON;
        } else if (strncmp(line, "treg ", 5) == 0) {
            unsigned r = 0, v = 0;
            if (sscanf(line + 5, "%x %x", &r, &v) != 2) {
                ESP_LOGW(TAG, "usage: treg <hex reg> <hex val>");
                continue;
            }
            cmd.type = DebugCmdType::TOUCH_WRITE;
            cmd.reg = (uint8_t)r;
            cmd.val = (uint8_t)v;
        } else if (strcmp(line, "pmlocks") == 0) {
            cmd.type = DebugCmdType::PM_LOCKS;
        } else if (strcmp(line, "powerlog clear") == 0) {
            cmd.type = DebugCmdType::POWERLOG_CLEAR;
        } else if (strcmp(line, "powerlog") == 0) {
            cmd.type = DebugCmdType::POWERLOG_DUMP;
        } else if (strcmp(line, "power") == 0) {
            cmd.type = DebugCmdType::POWER_INFO;
        } else if (strcmp(line, "sleep") == 0) {
            cmd.type = DebugCmdType::SCREEN_OFF;
        } else if (strncmp(line, "timeout ", 8) == 0) {
            cmd.type = DebugCmdType::TIMEOUT_SET;
            cmd.seconds = (uint16_t)atoi(line + 8);
        } else if (strcmp(line, "bmainit") == 0) {
            cmd.type = DebugCmdType::BMA_RETRY;
        } else if (strcmp(line, "bma") == 0) {
            cmd.type = DebugCmdType::BMA_DIAG;
        } else if (strncmp(line, "exten ", 6) == 0) {
            cmd.type = DebugCmdType::EXTEN_SET;
            cmd.flag = (line[6] == '1');
        } else if (strncmp(line, "ldo3 ", 5) == 0) {
            cmd.type = DebugCmdType::LDO3_MODE;
            cmd.flag = (line[5] == '1' || strcmp(line + 5, "dcin") == 0);
        } else {
            ESP_LOGW(TAG, "unknown command '%s' (try: view <name>, next, prev, tap [left|right], "
                          "status, touch, touchfix)", line);
            continue;
        }
        xQueueSend(s_queue, &cmd, 0);
    }
}

void debug_console_start(void)
{
    s_queue = xQueueCreate(4, sizeof(DebugCmd));
    xTaskCreate(debug_console_task, "dbg_console", 4096, nullptr, 3, nullptr);
    ESP_LOGI(TAG, "ready — commands: view <name>, next, prev, tap [left|right], status, touch, touchfix");
}

bool debug_console_poll(DebugCmd *cmd)
{
    return xQueueReceive(s_queue, cmd, 0) == pdTRUE;
}
