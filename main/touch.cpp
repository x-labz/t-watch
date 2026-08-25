#include "touch.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include <atomic>

#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "TOUCH";

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;
static std::atomic<uint32_t> s_err_count{0};

static constexpr uint8_t kRegStatus = 0x02;   // TD_STATUS, XH, XL, YH, YL

static esp_err_t add_device(void)
{
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TWATCH_ADDR_FT6336;
    dev_cfg.scl_speed_hz = TWATCH_I2C1_FREQ_HZ;

    esp_err_t err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "FT6336 add device failed: %s", esp_err_to_name(err));
    }
    return err;
}

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
    return add_device();
}

bool touch_read(int32_t *x, int32_t *y)
{
    uint8_t reg = kRegStatus;
    uint8_t buf[5] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), 50);
    if (err != ESP_OK) {
        // Log the transition into failure and then every ~5 s of solid
        // failure, so a wedged controller is visible in the log without
        // spamming it at the 50 Hz poll rate.
        uint32_t n = s_err_count.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n % 250) == 0) {
            ESP_LOGW(TAG, "i2c read failed (%s), %u consecutive", esp_err_to_name(err), (unsigned)n);
        }
        return false;
    }
    if (s_err_count.exchange(0, std::memory_order_relaxed) != 0) {
        ESP_LOGI(TAG, "i2c reads recovered");
    }

    uint8_t points = buf[0];
    if (points == 0 || points > 2) {
        return false;
    }
    *x = ((int32_t)(buf[1] & 0x0F) << 8) | buf[2];
    *y = ((int32_t)(buf[3] & 0x0F) << 8) | buf[4];
    return true;
}

uint32_t touch_error_count(void)
{
    return s_err_count.load(std::memory_order_relaxed);
}

esp_err_t touch_probe(void)
{
    return i2c_master_probe(s_bus, TWATCH_ADDR_FT6336, 100);
}

void touch_scan_bus(void)
{
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK) {
            ESP_LOGW(TAG, "i2c1 device found at 0x%02x", addr);
            found++;
        }
    }
    ESP_LOGW(TAG, "i2c1 scan done: %d device(s) (expect FT6336 at 0x%02x)",
             found, TWATCH_ADDR_FT6336);
}

esp_err_t touch_recover(void)
{
    ESP_LOGW(TAG, "recovering FT6336: LDO3 power-cycle + EXTEN reset + re-add device");
    if (s_dev != nullptr) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = nullptr;
    }
    // EXTEN alone was not enough on this unit, so go all the way to a rail
    // power-cycle (which also resets the panel — it shares LDO3).
    power_cycle_ldo3();
    esp_err_t err = add_device();
    if (err == ESP_OK) {
        s_err_count.store(0, std::memory_order_relaxed);
        err = touch_probe();
        ESP_LOGW(TAG, "after recovery, probe 0x%02x: %s", TWATCH_ADDR_FT6336, esp_err_to_name(err));
    }
    return err;
}
