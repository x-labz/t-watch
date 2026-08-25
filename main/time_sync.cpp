#include "time_sync.h"
#include "rtc.h"

#include <atomic>
#include <cmath>
#include <ctime>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "TIMESYNC";

// Survives resets that keep the RTC domain powered (software resets, watchdog
// resets, deep sleep) but NOT a full power loss. Only the timezone lives here:
// it is cheap to re-derive from a fix, which is exactly what RTC memory is for
// (CLAUDE.md section 6). Wall time itself belongs in the PCF8563, which is
// battery-backed and survives everything.
#define RTC_TZ_MAGIC 0x545A4F46u   // "TZOF"
RTC_DATA_ATTR static uint32_t s_rtc_tz_magic;
RTC_DATA_ATTR static int32_t s_rtc_tz_offset_sec;

static std::atomic<bool> s_clock_unset{true};
static std::atomic<int32_t> s_utc_offset_sec{3600};
static std::atomic<bool> s_tz_known{false};
static std::atomic<bool> s_gps_session{false};

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

void time_sync_init(void)
{
    if (s_rtc_tz_magic == RTC_TZ_MAGIC) {
        s_utc_offset_sec.store(s_rtc_tz_offset_sec, std::memory_order_relaxed);
        s_tz_known.store(true, std::memory_order_relaxed);
        ESP_LOGI(TAG, "timezone from RTC-memory cache: utc_offset=%ld s", (long)s_rtc_tz_offset_sec);
    }

    // The PCF8563 is the normal source of wall time — no GPS, no waiting.
    if (rtc_restore_system_time() == ESP_OK) {
        s_clock_unset.store(false, std::memory_order_relaxed);
        if (!s_tz_known.load(std::memory_order_relaxed)) {
            // Now that the real date is known, the Hungary default can at
            // least get DST right instead of assuming a flat UTC+1.
            s_utc_offset_sec.store(hungary_default_offset_now(), std::memory_order_relaxed);
        }
        ESP_LOGI(TAG, "clock ready from RTC — GPS not needed at boot");
    } else {
        ESP_LOGW(TAG, "no trustworthy RTC time — open the GPS view to set the clock");
    }
}

void time_sync_gps_session_begin(void)
{
    s_gps_session.store(true, std::memory_order_relaxed);
}

void time_sync_gps_session_end(void)
{
    s_gps_session.store(false, std::memory_order_relaxed);
}

void time_sync_feed_gps(const GpsReading &r)
{
    // Only ever set the clock when nothing trustworthy has it. On a healthy
    // watch this never runs: the PCF8563 already supplied the time at boot.
    if (s_clock_unset.load(std::memory_order_relaxed) && r.has_date_time) {
        int64_t days = days_from_civil(2000 + r.date_year, r.date_month, r.date_day);
        time_t epoch = (time_t)(days * 86400 + r.utc_hh * 3600 + r.utc_mm * 60 + r.utc_ss);

        struct timeval tv = {};
        tv.tv_sec = epoch;
        settimeofday(&tv, nullptr);
        s_clock_unset.store(false, std::memory_order_relaxed);

        ESP_LOGI(TAG, "clock set from GPS: 20%02u-%02u-%02u %02u:%02u:%02u UTC",
                 r.date_year, r.date_month, r.date_day, r.utc_hh, r.utc_mm, r.utc_ss);

        // Persist it so no future boot has to wait for GPS again.
        rtc_store_utc(epoch);

        if (!s_tz_known.load(std::memory_order_relaxed)) {
            s_utc_offset_sec.store(hungary_default_offset_now(), std::memory_order_relaxed);
        }
    }

    // Timezone refinement needs an actual position, which is why it only
    // happens here — while the GPS view is open and the receiver is powered.
    if (!s_tz_known.load(std::memory_order_relaxed) && r.has_fix &&
        (r.latitude != 0 || r.longitude != 0)) {
        int32_t offset = timezone_from_longitude(r.longitude);
        s_utc_offset_sec.store(offset, std::memory_order_relaxed);
        s_rtc_tz_offset_sec = offset;
        s_rtc_tz_magic = RTC_TZ_MAGIC;
        s_tz_known.store(true, std::memory_order_relaxed);
        ESP_LOGI(TAG, "timezone acquired from fix: utc_offset=%+ld h (lat=%.4f lon=%.4f), cached to RTC memory",
                 (long)(offset / 3600), r.latitude, r.longitude);
    }
}

bool time_sync_in_progress(void)
{
    return s_clock_unset.load(std::memory_order_relaxed);
}

int32_t time_sync_get_utc_offset_seconds(void)
{
    return s_utc_offset_sec.load(std::memory_order_relaxed);
}

TzStatus time_sync_tz_status(void)
{
    if (s_tz_known.load(std::memory_order_relaxed)) {
        return TzStatus::ACQUIRED;
    }
    return s_gps_session.load(std::memory_order_relaxed) ? TzStatus::WAITING_FOR_FIX
                                                         : TzStatus::CACHED;
}
