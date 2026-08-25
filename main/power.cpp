#include "power.h"
#include "twatch_v2_pins.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define XPOWERS_CHIP_AXP202
#include "XPowersLib.h"

static const char *TAG = "PWR";

static i2c_master_bus_handle_t s_i2c_bus0 = nullptr;
static XPowersPMU s_pmu;

i2c_master_bus_handle_t power_get_i2c_bus0(void)
{
    return s_i2c_bus0;
}

esp_err_t power_init(void)
{
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = TWATCH_I2C0_PORT;
    bus_cfg.sda_io_num = (gpio_num_t)TWATCH_PIN_I2C0_SDA;
    bus_cfg.scl_io_num = (gpio_num_t)TWATCH_PIN_I2C0_SCL;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus0 init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_pmu.begin(s_i2c_bus0, TWATCH_ADDR_AXP202)) {
        ESP_LOGE(TAG, "AXP202 begin() FAILED — check I2C wiring / addr 0x%02x", TWATCH_ADDR_AXP202);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AXP202 chip id: 0x%02x", s_pmu.getChipID());

    // LDO3: panel + touch controller power — must be up before lcd.init().
    s_pmu.setLDO3Voltage(TWATCH_LDO3_PANEL_MV);
    s_pmu.enableLDO3();

    // LDO2: backlight power.
    s_pmu.setLDO2Voltage(TWATCH_LDO2_BACKLIGHT_MV);
    s_pmu.enableLDO2();

    // EXTEN off -> delay -> on resets the FT6336 touch controller.
    s_pmu.disableExternalPin();
    vTaskDelay(pdMS_TO_TICKS(20));
    s_pmu.enableExternalPin();

    // Panel power-up settle time before lcd.init().
    vTaskDelay(pdMS_TO_TICKS(150));

    // PEK (power key) short/long press IRQs — see CLAUDE.md section 14.
    s_pmu.setPowerKeyPressOnTime(XPOWERS_POWERON_128MS);
    s_pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    s_pmu.disableIRQ(XPOWERS_AXP202_ALL_IRQ);
    s_pmu.clearIrqStatus();
    s_pmu.enableIRQ(XPOWERS_AXP202_PKEY_SHORT_IRQ | XPOWERS_AXP202_PKEY_LONG_IRQ);

    // ADC channels read 0 unless explicitly enabled (CLAUDE.md section 2).
    s_pmu.enableBattDetection();
    s_pmu.enableBattVoltageMeasure();
    s_pmu.enableVbusVoltageMeasure();
    s_pmu.enableSystemVoltageMeasure();

    ESP_LOGI(TAG, "LDO2(backlight)=%s %umV  LDO3(panel/touch)=%s %umV  EXTEN(touch rst)=%s",
             s_pmu.isEnableLDO2() ? "ON" : "OFF", s_pmu.getLDO2Voltage(),
             s_pmu.isEnableLDO3() ? "ON" : "OFF", s_pmu.getLDO3Voltage(),
             s_pmu.isEnableExternalPin() ? "ON" : "OFF");

    return ESP_OK;
}

BatteryReading power_read_battery(void)
{
    BatteryReading r;
    r.battery_connected = s_pmu.isBatteryConnect();
    r.voltage_mv = s_pmu.getBattVoltage();
    r.charging = s_pmu.isCharging();
    r.vbus_in = s_pmu.isVbusIn();
    int pct = s_pmu.getBatteryPercent();
    r.percent = pct < 0 ? 0 : pct;
    return r;
}

void power_gps_power(bool on)
{
    if (on) {
        bool volt_ok = s_pmu.setLDO4Voltage(TWATCH_LDO4_GPS_MV);
        bool en_ok = s_pmu.enableLDO4();
        ESP_LOGI(TAG, "LDO4(gps) setVoltage=%d enable=%d -> isEnabled=%s %umV",
                 volt_ok, en_ok, s_pmu.isEnableLDO4() ? "ON" : "OFF", s_pmu.getLDO4Voltage());
    } else {
        s_pmu.disableLDO4();
        ESP_LOGI(TAG, "LDO4(gps) disabled -> isEnabled=%s", s_pmu.isEnableLDO4() ? "ON" : "OFF");
    }
}
