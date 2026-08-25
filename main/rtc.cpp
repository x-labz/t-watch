#include "rtc.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include <sys/time.h>

#include "SensorPCF8563.hpp"
#include "esp_log.h"

static const char *TAG = "RTC";

static SensorPCF8563 s_rtc;
static bool s_ready = false;

// Days since the Unix epoch for a civil date (Howard Hinnant's algorithm).
// Used instead of timegm(), which this toolchain's libc does not provide.
static int64_t days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

esp_err_t rtc_init(void)
{
    if (!s_rtc.begin(power_get_i2c_bus0())) {
        ESP_LOGE(TAG, "PCF8563 begin() failed");
        return ESP_FAIL;
    }
    s_ready = true;
    ESP_LOGI(TAG, "PCF8563 ready, clock integrity=%s",
             s_rtc.isClockIntegrityGuaranteed() ? "OK" : "LOST");
    return ESP_OK;
}

bool rtc_time_is_valid(void)
{
    if (!s_ready) return false;
    // The PCF8563's VL flag latches if the oscillator ever stopped, which is
    // the one honest signal that the held time cannot be trusted.
    if (!s_rtc.isClockIntegrityGuaranteed()) return false;
    RTC_DateTime dt = s_rtc.getDateTime();
    // A never-set chip reads back as year 2000; treat anything before the
    // firmware could plausibly exist as unset rather than "valid".
    return dt.getYear() >= 2024 &&
           dt.getMonth() >= 1 && dt.getMonth() <= 12 &&
           dt.getDay() >= 1 && dt.getDay() <= 31;
}

esp_err_t rtc_restore_system_time(void)
{
    if (!rtc_time_is_valid()) {
        ESP_LOGW(TAG, "held time not trustworthy — an external sync is needed");
        return ESP_ERR_INVALID_STATE;
    }
    RTC_DateTime dt = s_rtc.getDateTime();
    int64_t days = days_from_civil(dt.getYear(), dt.getMonth(), dt.getDay());
    time_t epoch = (time_t)(days * 86400 + dt.getHour() * 3600 +
                            dt.getMinute() * 60 + dt.getSecond());

    struct timeval tv = {};
    tv.tv_sec = epoch;
    settimeofday(&tv, nullptr);

    ESP_LOGI(TAG, "system clock restored from RTC: %04u-%02u-%02u %02u:%02u:%02u UTC",
             dt.getYear(), dt.getMonth(), dt.getDay(),
             dt.getHour(), dt.getMinute(), dt.getSecond());
    return ESP_OK;
}

esp_err_t rtc_store_utc(time_t utc)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    struct tm t;
    gmtime_r(&utc, &t);
    s_rtc.setDateTime(RTC_DateTime(t));
    ESP_LOGI(TAG, "RTC updated: %04d-%02d-%02d %02d:%02d:%02d UTC",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return ESP_OK;
}
