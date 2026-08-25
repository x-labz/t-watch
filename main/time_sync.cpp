#include "time_sync.h"
#include "gps.h"

#include <atomic>
#include <cmath>
#include <ctime>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "TIMESYNC";

static constexpr int64_t kSyncTimeoutUs = 3LL * 60 * 1000000; // give up after 3 min

// Survives resets that keep the RTC domain powered (software resets,
// watchdog resets, deep sleep) but NOT a full power loss — same caveat as
// any RTC_DATA_ATTR variable. Only the timezone is cached here, not wall
// time itself: there's no battery-backed RTC chip (PCF8563) wired up yet
// per CLAUDE.md section 6, so actual time still needs a fresh GPS sync
// every boot, but the timezone rarely changes and is worth remembering.
#define RTC_TZ_MAGIC 0x545A4F46u   // "TZOF"
RTC_DATA_ATTR static uint32_t s_rtc_tz_magic;
RTC_DATA_ATTR static int32_t s_rtc_tz_offset_sec;

static std::atomic<bool> s_in_progress{false};
static std::atomic<int32_t> s_utc_offset_sec{3600};   // placeholder until phase 1 knows the real date

// Days-since-1970-01-01 for a proleptic Gregorian date (Howard Hinnant's
// well-known constant-time algorithm) — avoids depending on timegm(), which
// this toolchain's libc doesn't provide.
static int64_t days_from_civil(int y, int m, int d)
{
    y -= (m <= 2) ? 1 : 0;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int32_t timezone_from_longitude(double longitude_deg)
{
    int32_t hours = (int32_t)lround(longitude_deg / 15.0);
    if (hours > 14) hours = 14;
    if (hours < -12) hours = -12;
    return hours * 3600;
}

// EU DST rule (Hungary follows it): CEST (UTC+2) from the last Sunday of
// March to the last Sunday of October, CET (UTC+1) otherwise. Day-of-month
// granularity only — good enough for a default estimate, not exact to the
// 01:00 UTC transition instant.
static bool eu_dst_active(int year, int month, int day)
{
    if (month < 3 || month > 10) return false;
    if (month > 3 && month < 10) return true;
    static const int dim_tbl[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int dim = dim_tbl[month - 1];
    int64_t last_day = days_from_civil(year, month, dim);
    int weekday = (int)((last_day + 4) % 7);   // 1970-01-01 was a Thursday; 0=Sunday here
    int last_sunday = dim - weekday;
    return (month == 3) ? (day >= last_sunday) : (day < last_sunday);
}

static int32_t hungary_default_offset_now(void)
{
    time_t now = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now, &tm_utc);
    bool dst = eu_dst_active(tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday);
    return dst ? 7200 : 3600;
}

static void time_sync_task(void *)
{
    bool have_cached_tz = (s_rtc_tz_magic == RTC_TZ_MAGIC);
    if (have_cached_tz) {
        s_utc_offset_sec.store(s_rtc_tz_offset_sec, std::memory_order_relaxed);
        ESP_LOGI(TAG, "using timezone cached in RTC memory: utc_offset=%ld s",
                 (long)s_rtc_tz_offset_sec);
    }

    gps_acquire();
    int64_t t0 = esp_timer_get_time();
    bool time_set = false;
    bool tz_set = have_cached_tz;

    // Two independent phases: date/time from $RMC's navigation message
    // arrives well before a full position fix, so the clock gets set (and
    // the blinking indicator stops) as soon as phase 1 lands — timezone
    // refinement from longitude happens later, silently, once a fix
    // actually arrives, without holding the clock hostage to it.
    while ((!time_set || !tz_set) && (esp_timer_get_time() - t0) < kSyncTimeoutUs) {
        GpsReading r = gps_read();

        if (!time_set && r.has_date_time) {
            int64_t days = days_from_civil(2000 + r.date_year, r.date_month, r.date_day);
            time_t epoch = (time_t)(days * 86400 + r.utc_hh * 3600 + r.utc_mm * 60 + r.utc_ss);

            struct timeval tv;
            tv.tv_sec = epoch;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);

            ESP_LOGI(TAG, "phase 1 (time) synced: 20%02u-%02u-%02u %02u:%02u:%02u UTC",
                     r.date_year, r.date_month, r.date_day, r.utc_hh, r.utc_mm, r.utc_ss);
            time_set = true;
            s_in_progress.store(false, std::memory_order_relaxed);

            // No real fix yet to compute a real timezone from — refresh the
            // Hungary default now that the real date is known, so at least
            // DST is accounted for instead of a static UTC+1 guess.
            if (!have_cached_tz) {
                s_utc_offset_sec.store(hungary_default_offset_now(), std::memory_order_relaxed);
            }
        }

        if (!tz_set && r.has_fix && (r.latitude != 0 || r.longitude != 0)) {
            int32_t offset = timezone_from_longitude(r.longitude);
            s_utc_offset_sec.store(offset, std::memory_order_relaxed);
            s_rtc_tz_offset_sec = offset;
            s_rtc_tz_magic = RTC_TZ_MAGIC;
            ESP_LOGI(TAG, "phase 2 (timezone) synced: utc_offset=%ld s (lon=%.3f), cached to RTC memory",
                     (long)offset, r.longitude);
            tz_set = true;
        }

        if (!time_set || !tz_set) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    if (!time_set) {
        ESP_LOGW(TAG, "phase 1 (time) gave up after %lld s — no GPS date/time ever arrived",
                 (long long)(kSyncTimeoutUs / 1000000));
        s_in_progress.store(false, std::memory_order_relaxed);
    }
    if (!tz_set) {
        ESP_LOGW(TAG, "phase 2 (timezone) gave up after %lld s — no GPS fix ever arrived (keeping Hungary default)",
                 (long long)(kSyncTimeoutUs / 1000000));
    }

    gps_release();
    vTaskDelete(nullptr);
}

void time_sync_start(void)
{
    s_in_progress.store(true, std::memory_order_relaxed);
    xTaskCreate(time_sync_task, "time_sync", 4096, nullptr, 4, nullptr);
}

bool time_sync_in_progress(void)
{
    return s_in_progress.load(std::memory_order_relaxed);
}

int32_t time_sync_get_utc_offset_seconds(void)
{
    return s_utc_offset_sec.load(std::memory_order_relaxed);
}
