#include "touch.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include <atomic>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TOUCH";

static i2c_master_bus_handle_t s_bus = nullptr;
static i2c_master_dev_handle_t s_dev = nullptr;
static std::atomic<uint32_t> s_err_count{0};

static constexpr uint8_t kRegStatus = 0x02;   // TD_STATUS, XH, XL, YH, YL

// FocalTech registers (names/values from LilyGO's own focaltech driver).
static constexpr uint8_t kRegMonitorTime = 0x87;     // idle seconds before active->monitor
static constexpr uint8_t kRegMonitorPeriod = 0x89;   // report period while in monitor
static constexpr uint8_t kRegIntStatus = 0xA4;       // 1 = interrupt on touch
static constexpr uint8_t kRegPowerMode = 0xA5;

// FOCALTECH_PMODE_*: ACTIVE ~4mA, MONITOR ~3mA, DEEPSLEEP ~100uA. In DEEPSLEEP
// the chip does not answer I2C at all and, per the vendor header, "the reset
// pin must be pulled down to wake up" — that is the AXP202 EXTEN line here.
// A watch that boots with the controller left in DEEPSLEEP therefore looks
// exactly like dead touch hardware, which is why init below always resets
// first and then states the power mode explicitly instead of inheriting it.
static constexpr uint8_t kPmodeActive = 0x00;
static constexpr uint8_t kMonitorTimeDefault = 0x0A;
static constexpr uint8_t kMonitorPeriodDefault = 0x28;

static constexpr uint32_t kAutoRecoverAfterErrors = 250;   // ~5 s at the 50 Hz poll rate
// Bounded: if the controller is genuinely absent, retrying forever just spams
// the log and stalls the UI loop every few seconds for no benefit.
static constexpr int kMaxAutoRecoveries = 5;
static int s_recover_attempts = 0;

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

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 50);
}

// Puts the controller into a known state rather than trusting whatever mode it
// was left in. Only meaningful once the chip is answering — the caller resets
// it first if it is not.
static void configure_chip(void)
{
    // Deliberately minimal. An EXTEN reset already leaves the controller in a
    // working default configuration — the long-standing driver here wrote no
    // registers at all and touch worked. The ONLY thing worth forcing is the
    // power mode, because DEEPSLEEP is the one state a reset alone may not
    // clear and it makes the chip look like dead hardware. Writing the monitor
    // and INT-mode registers on top of that gained nothing and is exactly the
    // kind of change that can stop a working panel from reporting points, so
    // it is not done here.
    esp_err_t err = write_reg(kRegPowerMode, kPmodeActive);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "could not set power mode: %s", esp_err_to_name(err));
    }
}

static esp_err_t create_bus(void)
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
    }
    return err;
}

esp_err_t touch_init(void)
{
    esp_err_t err = create_bus();
    if (err != ESP_OK) {
        return err;
    }
    err = add_device();
    if (err != ESP_OK) {
        return err;
    }

    // If the controller is in DEEPSLEEP it will not ACK until EXTEN has pulsed
    // it, so probe first and reset only when needed. Init still succeeds if it
    // stays silent — touch_read()'s auto-recovery keeps retrying — because a
    // dead touch panel must not stop the rest of the watch from booting.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (touch_probe() == ESP_OK) {
            configure_chip();
            ESP_LOGI(TAG, "FT6336 ready (attempt %d)", attempt + 1);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "FT6336 not answering, EXTEN reset (attempt %d)", attempt + 1);
        power_touch_reset();
        vTaskDelay(pdMS_TO_TICKS(60));   // controller boot time after reset
    }
    ESP_LOGE(TAG, "FT6336 still not answering after 3 resets — continuing without touch");
    return ESP_OK;
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
        if (n == 1 || (n % kAutoRecoverAfterErrors) == 0) {
            ESP_LOGW(TAG, "i2c read failed (%s), %u consecutive", esp_err_to_name(err), (unsigned)n);
        }
        // Self-heal: a controller that dropped into DEEPSLEEP never comes back
        // on its own, and previously that left the watch looking permanently
        // frozen with no way back short of reflashing. Retry periodically
        // rather than once, since the first reset does not always take.
        if (n % kAutoRecoverAfterErrors == 0 && s_recover_attempts < kMaxAutoRecoveries) {
            s_recover_attempts++;
            touch_recover();
            if (s_recover_attempts == kMaxAutoRecoveries) {
                ESP_LOGE(TAG, "touch did not come back after %d recovery attempts — "
                              "giving up (use `touchfix` to retry manually)", kMaxAutoRecoveries);
            }
        }
        return false;
    }
    if (s_err_count.exchange(0, std::memory_order_relaxed) != 0) {
        ESP_LOGI(TAG, "i2c reads recovered");
        s_recover_attempts = 0;
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

esp_err_t touch_write_reg(uint8_t reg, uint8_t val)
{
    esp_err_t err = write_reg(reg, val);
    ESP_LOGW(TAG, "write reg 0x%02X = 0x%02X: %s", reg, val, esp_err_to_name(err));
    return err;
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50);
}

void touch_dump_registers(void)
{
    // 0x00 DEVICE_MODE (0 = normal working mode; 0x40 = factory/test mode, in
    // which the chip answers I2C but never reports touches), 0x01 gesture,
    // 0x02 points, 0x80 threshold, 0x86 control, 0x87/0x88/0x89 monitor
    // timings, 0xA1/0xA2 firmware version, 0xA3 chip id, 0xA5 power mode,
    // 0xA6 firmware id, 0xA8 vendor id, 0xA9 error status.
    static const uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                                   0x80, 0x86, 0x87, 0x88, 0x89,
                                   0xA1, 0xA2, 0xA3, 0xA5, 0xA6, 0xA8, 0xA9};
    for (uint8_t reg : regs) {
        uint8_t v = 0;
        esp_err_t err = read_reg(reg, &v);
        ESP_LOGW(TAG, "reg 0x%02X = 0x%02X%s", reg, v,
                 err == ESP_OK ? "" : " (READ FAILED)");
    }
}

void touch_monitor_raw(int seconds)
{
    ESP_LOGW(TAG, "raw touch monitor for %d s — TOUCH THE SCREEN NOW", seconds);
    uint8_t last = 0xFF;
    int hits = 0;
    for (int i = 0; i < seconds * 50; i++) {
        uint8_t reg = kRegStatus;
        uint8_t buf[5] = {0};
        if (i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), 50) == ESP_OK) {
            if (buf[0] != last) {
                last = buf[0];
                ESP_LOGW(TAG, "  status=0x%02X raw=%02X %02X %02X %02X", buf[0],
                         buf[1], buf[2], buf[3], buf[4]);
            }
            if (buf[0] != 0) hits++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGW(TAG, "raw monitor done: %d readings reported a touch point", hits);
}

esp_err_t touch_force_deepsleep(void)
{
    ESP_LOGW(TAG, "forcing FT6336 into DEEPSLEEP (test hook)");
    esp_err_t err = write_reg(kRegPowerMode, 0x03);
    ESP_LOGW(TAG, "write power mode DEEPSLEEP: %s", esp_err_to_name(err));
    return err;
}

esp_err_t touch_recover(void)
{
    // Deliberately does NOT power-cycle LDO3. That rail also feeds the panel,
    // which has no reset pin, so cutting it corrupts the display until a full
    // lcd.init() — unacceptable for something that runs automatically every
    // few seconds. EXTEN is the FT6336's documented reset and is enough on its
    // own; the I2C bus is rebuilt too because a controller that dropped off
    // mid-transaction can leave the master's state machine wedged, which is
    // what ESP_ERR_INVALID_STATE indicates.
    ESP_LOGW(TAG, "recovering FT6336: EXTEN reset + I2C bus rebuild");
    if (s_dev != nullptr) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = nullptr;
    }
    if (s_bus != nullptr) {
        i2c_del_master_bus(s_bus);
        s_bus = nullptr;
    }
    power_touch_reset();
    vTaskDelay(pdMS_TO_TICKS(60));   // controller boot time after reset

    esp_err_t err = create_bus();
    if (err != ESP_OK) {
        return err;
    }
    err = add_device();
    if (err != ESP_OK) {
        return err;
    }
    s_err_count.store(0, std::memory_order_relaxed);

    err = touch_probe();
    if (err != ESP_OK) {
        // The rail cycle alone can leave it asleep; EXTEN is the documented
        // wake for DEEPSLEEP, so pulse it again and give it time to boot.
        power_touch_reset();
        vTaskDelay(pdMS_TO_TICKS(60));
        err = touch_probe();
    }
    if (err == ESP_OK) {
        configure_chip();
    }
    ESP_LOGW(TAG, "after recovery, probe 0x%02x: %s", TWATCH_ADDR_FT6336, esp_err_to_name(err));
    return err;
}
