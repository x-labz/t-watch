#include "tilt.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include "SensorBMA423.hpp"
#include "esp_log.h"

static const char *TAG = "TILT";
static constexpr float kGravityMps2 = 9.80665f;

static SensorBMA423 s_bma;

esp_err_t tilt_init(void)
{
    if (!s_bma.begin(power_get_i2c_bus0(), TWATCH_ADDR_BMA423)) {
        ESP_LOGE(TAG, "BMA423 begin() failed");
        return ESP_FAIL;
    }
    if (!s_bma.setOperationMode(OperationMode::NORMAL)) {
        ESP_LOGE(TAG, "BMA423 setOperationMode(NORMAL) failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "BMA423 ready");
    return ESP_OK;
}

TiltReading tilt_read(void)
{
    TiltReading r;
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
