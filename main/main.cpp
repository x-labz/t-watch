#include "esp_log.h"
#include "debug_console.h"
#include "power.h"
#include "powersave.h"
#include "rtc.h"
#include "lgfx_twatch_v2.hpp"
#include "touch.h"
#include "gps.h"
#include "tilt.h"
#include "haptic.h"
#include "settings.h"
#include "time_sync.h"
#include "ui_task.h"

static const char *TAG = "MAIN";
static LGFX_TWatch2020V2 lcd;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "twatch firmware boot stub");

    debug_console_start();

    if (power_init() != ESP_OK) {
        ESP_LOGE(TAG, "power_init() failed — PMU not ready, aborting bring-up");
        return;
    }
    ESP_LOGI(TAG, "PMU ready — LDO2/LDO3/EXTEN sequenced per section 3");

    BatteryReading batt = power_read_battery();
    ESP_LOGI(TAG, "battery: connected=%d percent=%d voltage_mv=%u charging=%d vbus_in=%d",
             batt.battery_connected, batt.percent, batt.voltage_mv, batt.charging, batt.vbus_in);

    if (settings_init() != ESP_OK) {
        ESP_LOGE(TAG, "settings_init() failed — using default brightness");
    } else {
        ESP_LOGI(TAG, "settings_init() OK");
    }

    // lcd.init() only runs after power_init() has sequenced LDO3/LDO2/EXTEN.
    if (!lcd.init()) {
        ESP_LOGE(TAG, "lcd.init() failed");
        return;
    }
    lcd.setBrightness(settings_get_brightness());
    ESP_LOGI(TAG, "lcd.init() OK");

    // From here on, NOTHING aborts the boot. Only the PMU and the panel are
    // load-bearing: without them there is no watch. Every peripheral below is
    // optional, and a single transient I2C flake in one of them must never
    // stop the UI task from starting — that presents to the user as a frozen
    // watch with no way in, which is exactly what a BMA423 init failure did
    // (CLAUDE.md section 13). Degrade, log, carry on.
    if (touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch_init() failed — continuing without touch");
    } else {
        ESP_LOGI(TAG, "touch_init() OK");
    }

    if (gps_init() != ESP_OK) {
        ESP_LOGE(TAG, "gps_init() failed — continuing without GPS");
    } else {
        ESP_LOGI(TAG, "gps_init() OK");
    }

    // PCF8563 is the normal source of wall time; with it the watch no longer
    // powers the GPS at boot just to learn what time it is (CLAUDE.md s9).
    if (rtc_init() != ESP_OK) {
        ESP_LOGW(TAG, "rtc_init() failed — clock will need a GPS sync");
    } else {
        ESP_LOGI(TAG, "rtc_init() OK");
    }

    if (tilt_init() != ESP_OK) {
        ESP_LOGE(TAG, "tilt_init() failed — continuing without tilt");
    } else {
        ESP_LOGI(TAG, "tilt_init() OK");
    }

    if (haptic_init() != ESP_OK) {
        ESP_LOGE(TAG, "haptic_init() failed — continuing without haptics");
    } else {
        ESP_LOGI(TAG, "haptic_init() OK");
    }

    // Enable DFS + light sleep last, so nothing above races a frequency
    // change during bring-up.
    if (powersave_init() != ESP_OK) {
        ESP_LOGW(TAG, "powersave_init() failed — running without DFS/light sleep");
    }

    time_sync_init();
    ESP_LOGI(TAG, "time_sync_init() — starting UI task");

    ui_task_start(lcd);
}
