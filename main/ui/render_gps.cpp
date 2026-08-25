#include "render.h"

#include <cstdio>

void render_gps(LGFX_Sprite &fb, const GpsVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);

    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("GPS", fb.width() / 2, 6);

    fb.setTextSize(3);
    fb.setTextColor(vm.has_fix ? TFT_GREEN : TFT_RED, TFT_BLACK);
    fb.drawString(vm.has_fix ? "FIX" : "NO FIX", fb.width() / 2, 28);

    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.setTextSize(2);
    char buf[32];

    snprintf(buf, sizeof(buf), "SATS: %u/%u", vm.satellites_used, vm.satellites_in_view);
    fb.drawString(buf, fb.width() / 2, 62);

    snprintf(buf, sizeof(buf), "HDOP: %.1f", (double)vm.hdop);
    fb.drawString(buf, fb.width() / 2, 86);

    snprintf(buf, sizeof(buf), "ALT: %.0f m", (double)vm.altitude_m);
    fb.drawString(buf, fb.width() / 2, 110);

    snprintf(buf, sizeof(buf), "SPD: %.1f km/h", (double)vm.speed_kmh);
    fb.drawString(buf, fb.width() / 2, 134);

    fb.setTextSize(1);
    if (vm.has_fix) {
        snprintf(buf, sizeof(buf), "%.5f, %.5f", vm.latitude, vm.longitude);
    } else {
        snprintf(buf, sizeof(buf), "-- , --");
    }
    fb.drawString(buf, fb.width() / 2, 158);

    snprintf(buf, sizeof(buf), "UTC %02u:%02u:%02u", vm.utc_hh, vm.utc_mm, vm.utc_ss);
    fb.drawString(buf, fb.width() / 2, 172);

    // Live heartbeat: increments on every NMEA line seen, any type — the
    // only thing on this view that proves the UART link + module are alive
    // when there's no fix yet to look at.
    fb.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buf, sizeof(buf), "NMEA msgs: %lu", (unsigned long)vm.sentence_count);
    fb.drawString(buf, fb.width() / 2, 190);

    // Timezone is derived here rather than at boot, so this view is where the
    // user finds out whether that has happened yet.
    char tzbuf[48];
    int hrs = (int)(vm.utc_offset_sec / 3600);
    int mins = (int)((vm.utc_offset_sec % 3600) / 60);
    if (mins < 0) mins = -mins;
    switch (vm.tz_status) {
        case GpsTzVM::ACQUIRED:
            fb.setTextColor(TFT_GREEN, TFT_BLACK);
            snprintf(tzbuf, sizeof(tzbuf), "TZ ACQUIRED  UTC%+d:%02d", hrs, mins);
            break;
        case GpsTzVM::WAITING_FOR_FIX:
            fb.setTextColor(TFT_YELLOW, TFT_BLACK);
            snprintf(tzbuf, sizeof(tzbuf), "TZ: waiting for fix (UTC%+d:%02d)", hrs, mins);
            break;
        default:
            fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
            snprintf(tzbuf, sizeof(tzbuf), "TZ cached  UTC%+d:%02d", hrs, mins);
            break;
    }
    fb.drawString(tzbuf, fb.width() / 2, 204);

    fb.setTextColor(vm.clock_set ? TFT_DARKGREY : TFT_RED, TFT_BLACK);
    fb.drawString(vm.clock_set ? "clock: set (RTC)" : "clock: UNSET - needs fix",
                  fb.width() / 2, 218);
}
