#pragma once

#include <cstdint>

#include "gps.h"

// Time handling has two independent sources, and deliberately does NOT power
// the GPS on at boot (CLAUDE.md section 9 — GPS is the biggest consumer):
//
//   wall clock : the battery-backed PCF8563 (see rtc.h). It keeps time across
//                resets and power-off, so a normal boot needs no GPS at all.
//                GPS only ever sets the clock when the RTC's held time is not
//                trustworthy (first-ever boot / drained backup rail).
//   timezone   : estimated from GPS longitude, but only while the GPS view is
//                actually open and feeding readings in. Cached in RTC memory
//                so later boots reuse it instead of re-deriving it.

// Restores the system clock from the PCF8563 and loads any cached timezone.
// Call once at boot, after rtc_init(). Never touches the GPS.
void time_sync_init(void);

// True while the clock still has no trustworthy source — i.e. the RTC was
// unset and GPS has not yet supplied a time. Drives the blinking watchface
// indicator. Normally false immediately at boot.
bool time_sync_in_progress(void);

// Current UTC offset in seconds. Falls back to Hungary (DST-aware via the EU
// rule) until a GPS-derived timezone is available or restored from cache.
int32_t time_sync_get_utc_offset_seconds(void);

// Per-view status, for the GPS screen. Distinguishes "never tried" from
// "waiting for a fix" from "done", so the UI can say what is happening.
enum class TzStatus : uint8_t { CACHED, WAITING_FOR_FIX, ACQUIRED };
TzStatus time_sync_tz_status(void);

// Feed a GPS reading in while the GPS view is focused. Sets the clock if it
// had no trustworthy source (and writes it back to the PCF8563 so the next
// boot needs no GPS), and derives + caches the timezone once a position fix
// lands. Safe to call repeatedly; each step happens at most once.
void time_sync_feed_gps(const GpsReading &r);

// Called when the GPS view gains focus, so status reporting can distinguish
// "waiting for a fix right now" from "idle".
void time_sync_gps_session_begin(void);
void time_sync_gps_session_end(void);
