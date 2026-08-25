#pragma once

#include <cstdint>

// Spawns a background task that acquires GPS, waits for a valid $RMC fix
// with date+time, sets the system clock from it, and estimates the local
// UTC offset from longitude (a simple longitude/15 solar-zone approximation
// — not real IANA timezone/DST rules, which need a database this device
// doesn't carry). Gives up and releases GPS after a timeout if no fix with
// date/time ever arrives. Call once, after gps_init().
void time_sync_start(void);

// True while the boot-time sync is actively trying — drives the blinking
// "syncing" indicator on the watchface.
bool time_sync_in_progress(void);

// Current UTC offset in seconds. Defaults to Hungary (+3600, UTC+1,
// standard time — no DST) until a GPS fix updates it from longitude.
int32_t time_sync_get_utc_offset_seconds(void);
