#include "touch.h"
#include "twatch_v2_pins.h"

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "TOUCH";

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;

static constexpr uint8_t kRegStatus = 0x02;   // TD_STATUS, XH, XL, YH, YL

esp_err_t touch_init(void)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TWATCH_I2C1_PORT;
    bus_cfg.sda_io_num = (gpio_num_t)TWATCH_PIN_I2C1_SDA;
    bus_cfg.scl_io_num = (gpio_num_t)TWATCH_PIN_I2C1_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus1 init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TWATCH_ADDR_FT6336;
    dev_cfg.scl_speed_hz = TWATCH_I2C1_FREQ_HZ;

    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FT6336 add device failed: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

bool touch_read(int32_t *x, int32_t *y)
{
    uint8_t reg = kRegStatus;
    uint8_t buf[5] = {0};
    if (i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), 50) != ESP_OK) {
        return false;
    }
    uint8_t points = buf[0];
    if (points == 0 || points > 2) {
        return false;
    }
    *x = ((int32_t)(buf[1] & 0x0F) << 8) | buf[2];
    *y = ((int32_t)(buf[3] & 0x0F) << 8) | buf[4];
    return true;
}
