#include "esp_log.h"
#include "power.h"
#include "lgfx_twatch_v2.hpp"
#include "touch.h"
#include "gps.h"
#include "tilt.h"
#include "ui_task.h"

static const char *TAG = "MAIN";
static LGFX_TWatch2020V2 lcd;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "twatch firmware boot stub");

    if (power_init() != ESP_OK) {
        ESP_LOGE(TAG, "power_init() failed — PMU not ready, aborting bring-up");
        return;
    }
    ESP_LOGI(TAG, "PMU ready — LDO2/LDO3/EXTEN sequenced per section 3");

    BatteryReading batt = power_read_battery();
    ESP_LOGI(TAG, "battery: connected=%d percent=%d voltage_mv=%u charging=%d vbus_in=%d",
             batt.battery_connected, batt.percent, batt.voltage_mv, batt.charging, batt.vbus_in);

    // lcd.init() only runs after power_init() has sequenced LDO3/LDO2/EXTEN.
    if (!lcd.init()) {
        ESP_LOGE(TAG, "lcd.init() failed");
        return;
    }
    lcd.setBrightness(255);
    ESP_LOGI(TAG, "lcd.init() OK");

    if (touch_init() != ESP_OK) {
        ESP_LOGE(TAG, "touch_init() failed");
        return;
    }
    ESP_LOGI(TAG, "touch_init() OK");

    if (gps_init() != ESP_OK) {
        ESP_LOGE(TAG, "gps_init() failed");
        return;
    }
    ESP_LOGI(TAG, "gps_init() OK");

    if (tilt_init() != ESP_OK) {
        ESP_LOGE(TAG, "tilt_init() failed");
        return;
    }
    ESP_LOGI(TAG, "tilt_init() OK — starting UI task");

    ui_task_start(lcd);
}
