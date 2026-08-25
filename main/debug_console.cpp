#include "debug_console.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "DBGCON";
static QueueHandle_t s_queue;

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
