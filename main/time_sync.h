#pragma once

#include <cstdint>

// Spawns a background task that acquires GPS, waits for a valid $RMC fix
// with date+time (independent of position — see time_sync.cpp), sets the
// system clock from it, and estimates the local UTC offset from longitude
// once a position fix lands (a simple longitude/15 solar-zone approximation
// — not real IANA timezone/DST rules, which need a database this device
// doesn't carry). That estimate is cached in RTC memory, so a later reset
// starts from the last known-good timezone instead of the Hungary default
// while a fresh fix comes in. Gives up and releases GPS after a timeout if
// either phase never completes. Call once, after gps_init().
void time_sync_start(void);

// True while the boot-time sync is actively trying — drives the blinking
// "syncing" indicator on the watchface.
bool time_sync_in_progress(void);

// Current UTC offset in seconds. Before any timezone is known (this boot or
// cached from RTC memory), defaults to Hungary — DST-aware (UTC+1 CET /
// UTC+2 CEST by the EU rule) once the real date is known from phase 1.
int32_t time_sync_get_utc_offset_seconds(void);
