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

// Raw AXP202 register read — XPowersLib keeps readRegister() protected, and
// for a state-comparison dump we want the untouched bytes anyway.
static int axp_read_raw(uint8_t reg)
{
    i2c_master_dev_handle_t dev = nullptr;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TWATCH_ADDR_AXP202;
    cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(s_i2c_bus0, &cfg, &dev) != ESP_OK) return -1;

    uint8_t val = 0;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, &val, 1, 100);
    i2c_master_bus_rm_device(dev);
    return err == ESP_OK ? (int)val : -1;
}

static esp_err_t axp_write_raw(uint8_t reg, uint8_t val)
{
    i2c_master_dev_handle_t dev = nullptr;
    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TWATCH_ADDR_AXP202;
    cfg.scl_speed_hz = 400000;
    if (i2c_master_bus_add_device(s_i2c_bus0, &cfg, &dev) != ESP_OK) return ESP_FAIL;

    uint8_t buf[2] = {reg, val};
    esp_err_t err = i2c_master_transmit(dev, buf, sizeof(buf), 100);
    i2c_master_bus_rm_device(dev);
    return err;
}

// AXP202 REG12H, power output control. XPowersLib gets EXTEN WRONG: its
// enableExternalPin()/disableExternalPin() write bit 6 — the very same bit as
// enableLDO3() — so calling them silently switched the panel+touch rail off
// and on and never drove the real EXTEN pin at all. On the AXP202, EXTEN is
// bit 0. Because EXTEN is this board's only touch-reset line, that bug meant
// the FT6336 could be left held in reset with no way for our firmware to
// release it: it stopped answering I2C, looked exactly like dead hardware, and
// only came back after LilyGO's firmware (which sets the bit correctly) ran.
// Written raw here rather than via the library so the two cannot collide again.
static constexpr uint8_t kAxpRegOutputCtl = 0x12;
static constexpr uint8_t kAxpExtenBit = 0;

static void axp_set_exten(bool on)
{
    int cur = axp_read_raw(kAxpRegOutputCtl);
    if (cur < 0) {
        ESP_LOGE(TAG, "EXTEN: could not read REG12");
        return;
    }
    uint8_t val = on ? (uint8_t)(cur | (1u << kAxpExtenBit))
                     : (uint8_t)(cur & ~(1u << kAxpExtenBit));
    axp_write_raw(kAxpRegOutputCtl, val);
}

void power_dump_axp_registers(const char *when)
{
    // 0x10 output control, 0x12 rail enables (bit6 = EXTEN), 0x23-0x2A rail
    // voltages/modes, 0x30-0x33 VBUS/charge, 0x40-0x4A IRQ enables,
    // 0x80-0x84 ADC enables, 0x90-0x93 GPIO config.
    static const uint8_t regs[] = {
        0x10, 0x12, 0x13, 0x23, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x40, 0x41, 0x42, 0x43, 0x44,
        0x80, 0x82, 0x83, 0x84, 0x85, 0x86,
        0x90, 0x91, 0x92, 0x93,
    };
    ESP_LOGW(TAG, "==== AXP202 register dump (%s) ====", when);
    for (uint8_t r : regs) {
        int v = axp_read_raw(r);
        if (v >= 0) ESP_LOGW(TAG, "  axp[0x%02X] = 0x%02X", r, (unsigned)v);
        else        ESP_LOGW(TAG, "  axp[0x%02X] = <read failed>", r);
    }
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

    // Before we change anything — this is the state the previous firmware left.
    power_dump_axp_registers("inherited, before our writes");

    // LDO3: panel + touch controller power — must be up before lcd.init().
    // Set the regulation mode explicitly: AXP202 registers are battery-backed
    // and survive reflashes, so without this we silently inherit whatever the
    // previously-flashed firmware left behind. DCIN mode drives this rail well
    // above 3.3 V (measured 5076 mV on this board), so pin it to LDO.
    s_pmu.setLDO3Mode(XPOWERS_AXP202_LDO3_MODE_LDO);
    s_pmu.setLDO3Voltage(TWATCH_LDO3_PANEL_MV);
    s_pmu.enableLDO3();

    // LDO2: backlight power.
    s_pmu.setLDO2Voltage(TWATCH_LDO2_BACKLIGHT_MV);
    s_pmu.enableLDO2();

    // EXTEN off -> delay -> on resets the FT6336 touch controller.
    power_touch_reset();

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
    // Note enableBattVoltageMeasure() above turns on MONITOR_BAT_CURRENT as
    // well as MONITOR_BAT_VOLTAGE, so the discharge-current ADC used by
    // power_battery_discharge_ma() is already covered — there is no separate
    // "enable current" call to make.
    s_pmu.enableSystemVoltageMeasure();

    ESP_LOGI(TAG, "LDO2(backlight)=%s %umV  LDO3(panel/touch)=%s %umV  EXTEN(touch rst)=%s",
             s_pmu.isEnableLDO2() ? "ON" : "OFF", s_pmu.getLDO2Voltage(),
             s_pmu.isEnableLDO3() ? "ON" : "OFF", s_pmu.getLDO3Voltage(),
             power_exten_is_on() ? "ON" : "OFF");

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

float power_battery_discharge_ma(void)
{
    return s_pmu.getBattDischargeCurrent();
}

void power_touch_reset(void)
{
    // Real reset pulse on EXTEN (REG12 bit 0). LilyGO's driver holds it low for
    // 8 ms ("Trst Min = 5ms"); 20 ms is comfortably past that. Crucially this
    // leaves LDO3 (bit 6) alone, so the panel is not disturbed — the previous
    // implementation was toggling LDO3 instead and never resetting the FT6336.
    axp_set_exten(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    axp_set_exten(true);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void power_log_rails(void)
{
    ESP_LOGI(TAG, "rails: LDO2(backlight)=%s %umV  LDO3(panel/touch)=%s %umV mode=%s  "
                  "LDO4(gps)=%s %umV  EXTEN(touch rst)=%s",
             s_pmu.isEnableLDO2() ? "ON" : "OFF", s_pmu.getLDO2Voltage(),
             s_pmu.isEnableLDO3() ? "ON" : "OFF", s_pmu.getLDO3Voltage(),
             power_get_ldo3_dcin_mode() ? "DCIN" : "LDO",
             s_pmu.isEnableLDO4() ? "ON" : "OFF", s_pmu.getLDO4Voltage(),
             power_exten_is_on() ? "ON" : "OFF");
}

void power_set_exten(bool on)
{
    axp_set_exten(on);
    ESP_LOGW(TAG, "EXTEN forced %s (REG12 bit0) -> reads %s",
             on ? "ON" : "OFF", power_exten_is_on() ? "ON" : "OFF");
}

bool power_exten_is_on(void)
{
    int cur = axp_read_raw(kAxpRegOutputCtl);
    return cur >= 0 && (cur & (1u << kAxpExtenBit)) != 0;
}

bool power_get_ldo3_dcin_mode(void)
{
    // XPowersLib names this isLDO3LDOMode(), but it returns REG29 bit7 raw,
    // and bit7 SET is DCIN mode (setLDO3Mode sets the bit for DCIN, clears it
    // for LDO). The library's name is simply backwards — don't "fix" this to
    // read the other way round.
    return s_pmu.isLDO3LDOMode();
}

void power_set_ldo3_dcin_mode(bool dcin)
{
    s_pmu.setLDO3Mode(dcin ? XPOWERS_AXP202_LDO3_MODE_DCIN : XPOWERS_AXP202_LDO3_MODE_LDO);
    ESP_LOGW(TAG, "LDO3 mode -> %s", power_get_ldo3_dcin_mode() ? "DCIN" : "LDO");
}

void power_scan_bus0(void)
{
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_i2c_bus0, addr, 50) == ESP_OK) {
            ESP_LOGW(TAG, "i2c0 device found at 0x%02x", addr);
            found++;
        }
    }
    ESP_LOGW(TAG, "i2c0 scan done: %d device(s)", found);
}

void power_cycle_ldo3(void)
{
    ESP_LOGW(TAG, "power-cycling LDO3 (panel + touch)");
    s_pmu.disableLDO3();
    vTaskDelay(pdMS_TO_TICKS(150));
    s_pmu.setLDO3Voltage(TWATCH_LDO3_PANEL_MV);
    s_pmu.enableLDO3();
    vTaskDelay(pdMS_TO_TICKS(150));
    power_touch_reset();
    vTaskDelay(pdMS_TO_TICKS(150));
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
