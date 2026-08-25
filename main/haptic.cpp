#include "haptic.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "HAPTIC";

// SensorLib's DRV2605 class declarations exist but its managed-component
// CMakeLists.txt never compiles haptic_drivers/HapticDriver_DRV2605.cpp
// (checked both published versions, 0.4.0 and 0.4.1 — neither lists that
// source dir), so linking against it fails. The register set is simple and
// well-documented, so drive it directly instead, same pattern as touch.cpp
// and gps.cpp.
static constexpr uint8_t kRegMode = 0x01;
static constexpr uint8_t kRegLibrary = 0x03;
static constexpr uint8_t kRegWaveSeq1 = 0x04;
static constexpr uint8_t kRegWaveSeq2 = 0x05;
static constexpr uint8_t kRegGo = 0x0C;

static i2c_master_dev_handle_t s_dev = nullptr;

static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50) == ESP_OK;
}

esp_err_t haptic_init(void)
{
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TWATCH_ADDR_DRV2605;
    dev_cfg.scl_speed_hz = TWATCH_I2C0_FREQ_HZ;

    esp_err_t err = i2c_master_bus_add_device(power_get_i2c_bus0(), &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!write_reg(kRegMode, 0x00)) {   // internal trigger mode, out of standby
        ESP_LOGE(TAG, "MODE write failed");
        return ESP_FAIL;
    }
    if (!write_reg(kRegLibrary, 1)) {   // ERM ROM library 1 (this board's motor is a small ERM, not LRA)
        ESP_LOGE(TAG, "LIBRARY write failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "DRV2605 ready");
    return ESP_OK;
}

bool haptic_play(uint16_t effect_id)
{
    if (effect_id < 1 || effect_id > 123) return false;
    if (!write_reg(kRegWaveSeq1, (uint8_t)effect_id)) return false;
    if (!write_reg(kRegWaveSeq2, 0)) return false;   // sequence terminator
    return write_reg(kRegGo, 1);
}
