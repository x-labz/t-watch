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
}
