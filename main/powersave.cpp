#include "powersave.h"
#include "power.h"

#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "sdkconfig.h"

static const char *TAG = "PWRSAVE";

static esp_pm_lock_handle_t s_no_sleep_lock = nullptr;

// 40 MHz is the lowest the ESP32 can run with the 40 MHz crystal. WiFi needs
// at least 80 MHz while started, so wifiscan.cpp must raise this for the
// duration of a scan if it is ever seen to misbehave (CLAUDE.md section 9).
static constexpr int kMinFreqMhz = 40;
static constexpr int kMaxFreqMhz = 240;

esp_err_t powersave_init(void)
{
    esp_err_t err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_sleep", &s_no_sleep_lock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
        return err;
    }

    // The serial console is the only way to drive this watch when the screen
    // is off, and light sleep stops the UART clock — so incoming bytes would
    // be lost. Waking on UART activity keeps the debug console usable
    // (CLAUDE.md section 11) instead of trading it away for battery life.
    uart_set_wakeup_threshold((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 3);
    esp_sleep_enable_uart_wakeup((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM);

    esp_pm_config_t pm = {};
    pm.max_freq_mhz = kMaxFreqMhz;
    pm.min_freq_mhz = kMinFreqMhz;
    pm.light_sleep_enable = true;

    err = esp_pm_configure(&pm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DFS %d-%d MHz, automatic light sleep ON, UART wake enabled",
             kMinFreqMhz, kMaxFreqMhz);
    return ESP_OK;
}

void powersave_prevent_sleep(bool prevent)
{
    if (s_no_sleep_lock == nullptr) return;
    if (prevent) {
        esp_pm_lock_acquire(s_no_sleep_lock);
    } else {
        esp_pm_lock_release(s_no_sleep_lock);
    }
}

float powersave_battery_draw_ma(void)
{
    return power_battery_discharge_ma();
}

// --- power log -----------------------------------------------------------
// RTC memory, not NVS: this is a short-lived diagnostic trace that is cheap to
// re-take, exactly the "cache" tier from CLAUDE.md section 6, and it must not
// wear flash at one write per sample.
#define POWERLOG_MAGIC 0x504C4F47u   // "PLOG"
static constexpr int kPowerLogSlots = 96;

struct PowerSample {
    uint32_t uptime_s;
    uint16_t mv;
    int16_t ma;        // discharge current; ~0 while USB is attached
    uint8_t pct;
    uint8_t screen_on;
};

RTC_DATA_ATTR static uint32_t s_plog_magic;
RTC_DATA_ATTR static uint32_t s_plog_count;      // total ever written (may exceed slots)
RTC_DATA_ATTR static PowerSample s_plog[kPowerLogSlots];

void powersave_log_clear(void)
{
    s_plog_magic = POWERLOG_MAGIC;
    s_plog_count = 0;
    ESP_LOGW(TAG, "power log cleared — unplug USB now, use the watch, then replug and run `powerlog`");
}

void powersave_log_sample(bool screen_on)
{
    if (s_plog_magic != POWERLOG_MAGIC) {
        s_plog_magic = POWERLOG_MAGIC;
        s_plog_count = 0;
    }
    BatteryReading b = power_read_battery();
    PowerSample &slot = s_plog[s_plog_count % kPowerLogSlots];
    slot.uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    slot.mv = b.voltage_mv;
    slot.ma = (int16_t)power_battery_discharge_ma();
    slot.pct = (uint8_t)b.percent;
    slot.screen_on = screen_on ? 1 : 0;
    s_plog_count++;
}

void powersave_log_dump(void)
{
    if (s_plog_magic != POWERLOG_MAGIC || s_plog_count == 0) {
        ESP_LOGW(TAG, "power log empty — run `powerlog clear`, unplug USB, then come back");
        return;
    }
    uint32_t n = s_plog_count < kPowerLogSlots ? s_plog_count : kPowerLogSlots;
    uint32_t first = s_plog_count > kPowerLogSlots ? s_plog_count - kPowerLogSlots : 0;
    ESP_LOGW(TAG, "power log: %u samples (of %u taken)", (unsigned)n, (unsigned)s_plog_count);
    ESP_LOGW(TAG, "uptime_s,mV,mA,pct,screen");
    int32_t sum_on = 0, sum_off = 0;
    int n_on = 0, n_off = 0;
    for (uint32_t i = 0; i < n; i++) {
        const PowerSample &p = s_plog[(first + i) % kPowerLogSlots];
        ESP_LOGW(TAG, "%u,%u,%d,%u,%u", (unsigned)p.uptime_s, p.mv, p.ma, p.pct, p.screen_on);
        if (p.ma > 0) {
            if (p.screen_on) { sum_on += p.ma; n_on++; } else { sum_off += p.ma; n_off++; }
        }
    }
    // Only non-zero samples are averaged: zeros mean USB was attached, not
    // that the watch drew nothing.
    if (n_on)  ESP_LOGW(TAG, "avg screen ON : %.1f mA (%d samples)", (double)sum_on / n_on, n_on);
    if (n_off) ESP_LOGW(TAG, "avg screen OFF: %.1f mA (%d samples)", (double)sum_off / n_off, n_off);
    if (!n_on && !n_off) ESP_LOGW(TAG, "all samples read 0 mA — USB was attached the whole time");
}

void powersave_dump_locks(void)
{
    ESP_LOGW(TAG, "==== esp_pm locks ====");
    esp_pm_dump_locks(stdout);
}
