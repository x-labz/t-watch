#include "tilt.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TILT";

// Direct BMA4xx register driver — SensorLib is deliberately NOT used here.
// Its bma423_init() fails on this board with BMA4_E_COM_FAIL (-2) while raw
// reads of the very same registers on the very same bus succeed every time
// (chip id reads 0x13 as expected), so the fault is in the library's plumbing,
// not the chip. Same call as haptic.cpp, which drives the DRV2605 directly for
// a comparable reason.
//
// We only need raw acceleration for the tilt view. SensorLib's init also
// uploads a ~6 KB config blob, which exists solely to enable the on-chip step
// counter and gesture engine — features nothing here uses yet. Skipping it
// removes the failure mode rather than working around it. If the step counter
// or wake-on-tilt is wanted later (CLAUDE.md section 2 recommends using them
// instead of software equivalents), that blob upload is what has to be
// revisited.
static constexpr uint8_t kRegChipId = 0x00;
static constexpr uint8_t kRegAccData = 0x12;   // X_LSB, X_MSB, Y_LSB, Y_MSB, Z_LSB, Z_MSB
static constexpr uint8_t kRegAccConf = 0x40;
static constexpr uint8_t kRegAccRange = 0x41;
static constexpr uint8_t kRegPwrConf = 0x7C;
static constexpr uint8_t kRegPwrCtrl = 0x7D;
static constexpr uint8_t kRegCmd = 0x7E;

static constexpr uint8_t kChipIdBma423 = 0x13;
static constexpr uint8_t kCmdSoftReset = 0xB6;
static constexpr uint8_t kAccConfNormal100Hz = 0xA8;  // perf mode, norm_avg4, 100 Hz
static constexpr uint8_t kAccRange2g = 0x00;          // ±2g — best resolution for tilt
static constexpr uint8_t kPwrCtrlAccEn = 0x04;
static constexpr uint8_t kPwrConfActive = 0x00;       // advanced power save off

// ±2g over 12 signed bits: full scale 2048 counts = 2g, so 1g = 1024 counts.
static constexpr float kCountsPerG = 1024.0f;

static i2c_master_dev_handle_t s_dev = nullptr;
static bool s_ready = false;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    if (s_dev == nullptr) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 100);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    if (s_dev == nullptr) return ESP_ERR_INVALID_STATE;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t attach(void)
{
    if (s_dev != nullptr) return ESP_OK;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TWATCH_ADDR_BMA423;
    cfg.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(power_get_i2c_bus0(), &cfg, &s_dev);
}

esp_err_t tilt_init(void)
{
    esp_err_t err = attach();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "could not attach BMA423 to i2c bus0: %s", esp_err_to_name(err));
        return err;
    }

    // Soft reset, then confirm the part actually identifies itself before
    // trusting anything else. The datasheet wants a short settle afterwards.
    reg_write(kRegCmd, kCmdSoftReset);
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t id = 0;
    err = reg_read(kRegChipId, &id, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chip-id read failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }
    if (id != kChipIdBma423) {
        ESP_LOGE(TAG, "unexpected chip id 0x%02X (expected 0x%02X)", id, kChipIdBma423);
        return ESP_FAIL;
    }

    // Accelerometer only: leave advanced power save off so readings are
    // available continuously, 100 Hz is well above the ~15 Hz the tilt view
    // samples at, and ±2g gives the finest resolution for orientation.
    reg_write(kRegPwrConf, kPwrConfActive);
    vTaskDelay(pdMS_TO_TICKS(5));
    reg_write(kRegAccConf, kAccConfNormal100Hz);
    reg_write(kRegAccRange, kAccRange2g);
    err = reg_write(kRegPwrCtrl, kPwrCtrlAccEn);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enabling accelerometer failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    s_ready = true;
    ESP_LOGI(TAG, "BMA423 ready (direct driver, chip id 0x%02X)", id);
    return ESP_OK;
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

    uint8_t d[6] = {0};
    if (reg_read(kRegAccData, d, sizeof(d)) != ESP_OK) {
        return r;
    }

    // 12-bit signed, left-aligned in each 16-bit little-endian pair: combining
    // then dividing by 16 sign-extends correctly without manual bit fiddling.
    int16_t raw_x = (int16_t)((uint16_t)d[1] << 8 | d[0]) / 16;
    int16_t raw_y = (int16_t)((uint16_t)d[3] << 8 | d[2]) / 16;
    int16_t raw_z = (int16_t)((uint16_t)d[5] << 8 | d[4]) / 16;

    // The BMA423's X/Y axes are rotated 90° relative to the watch's screen
    // orientation on this unit (confirmed on hardware: tilting right moved
    // the on-screen ball down instead of right) — swapped here so callers
    // can treat accel_x_g/accel_y_g as screen-relative tilt directly.
    r.accel_x_g = raw_y / kCountsPerG;
    r.accel_y_g = raw_x / kCountsPerG;
    r.accel_z_g = raw_z / kCountsPerG;
    return r;
}

esp_err_t tilt_retry_init(void)
{
    ESP_LOGW(TAG, "re-running BMA423 init on demand ...");
    s_ready = false;
    esp_err_t err = tilt_init();
    ESP_LOGW(TAG, "on-demand init: %s", err == ESP_OK ? "SUCCEEDED" : "FAILED");
    return err;
}

void tilt_diagnose(void)
{
    esp_err_t probe = i2c_master_probe(power_get_i2c_bus0(), TWATCH_ADDR_BMA423, 100);
    ESP_LOGW(TAG, "BMA423 probe 0x%02X: %s", TWATCH_ADDR_BMA423, esp_err_to_name(probe));
    if (attach() != ESP_OK) return;

    uint8_t id = 0, pwr = 0, acc = 0, rng = 0, cfg = 0;
    ESP_LOGW(TAG, "chip-id: %s value=0x%02X (expect 0x%02X)",
             esp_err_to_name(reg_read(kRegChipId, &id, 1)), id, kChipIdBma423);
    reg_read(kRegPwrCtrl, &pwr, 1);
    reg_read(kRegPwrConf, &cfg, 1);
    reg_read(kRegAccConf, &acc, 1);
    reg_read(kRegAccRange, &rng, 1);
    ESP_LOGW(TAG, "PWR_CTRL=0x%02X PWR_CONF=0x%02X ACC_CONF=0x%02X ACC_RANGE=0x%02X ready=%d",
             pwr, cfg, acc, rng, (int)s_ready);

    TiltReading t = tilt_read();
    ESP_LOGW(TAG, "reading: x=%.3fg y=%.3fg z=%.3fg (z near ±1g when the watch is flat)",
             (double)t.accel_x_g, (double)t.accel_y_g, (double)t.accel_z_g);
}
