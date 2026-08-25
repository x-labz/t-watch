#include "tilt.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include "SensorBMA423.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TILT";
static constexpr float kGravityMps2 = 9.80665f;

static SensorBMA423 s_bma;
static bool s_ready = false;

esp_err_t tilt_init(void)
{
    // bma423_init intermittently fails on this unit with code -2 — a transient
    // I2C/power flake, not a wiring or driver problem (CLAUDE.md section 13),
    // and it clears on a retry. Retry here rather than reporting failure, since
    // a flaky accelerometer must never be what stops the watch from starting.
    for (int attempt = 1; attempt <= 3; attempt++) {
        if (s_bma.begin(power_get_i2c_bus0(), TWATCH_ADDR_BMA423) &&
            s_bma.setOperationMode(OperationMode::NORMAL)) {
            s_ready = true;
            ESP_LOGI(TAG, "BMA423 ready (attempt %d)", attempt);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "BMA423 init failed (attempt %d/3)", attempt);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGE(TAG, "BMA423 unavailable — tilt readings will read zero");
    return ESP_FAIL;
}

bool tilt_available(void)
{
    return s_ready;
}

TiltReading tilt_read(void)
{
    TiltReading r;
    if (!s_ready) {
        return r;   // zeroed — the tilt view just shows a centred ball
    }
    AccelerometerData data;
    if (!s_bma.readData(data)) {
        return r;
    }
    // The BMA423's X/Y axes are rotated 90° relative to the watch's screen
    // orientation on this unit (confirmed on hardware: tilting right moved
    // the on-screen ball down instead of right) — swapped here so callers
    // can treat accel_x_g/accel_y_g as screen-relative tilt directly.
    r.accel_x_g = data.mps2.y / kGravityMps2;
    r.accel_y_g = data.mps2.x / kGravityMps2;
    r.accel_z_g = data.mps2.z / kGravityMps2;
    return r;
}
