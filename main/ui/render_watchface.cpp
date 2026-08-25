#include "render.h"

#include <cstdio>

void render_watchface(LGFX_Sprite &fb, const WatchfaceVM &vm)
{
    fb.fillScreen(TFT_BLACK);

    char buf[12];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", vm.hh, vm.mm, vm.ss);

    fb.setTextDatum(textdatum_t::middle_center);
    fb.setFont(&fonts::Font7);
    fb.setTextSize(1);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString(buf, fb.width() / 2, fb.height() / 2);

    if (vm.gps_sync_blink) {
        fb.setTextDatum(textdatum_t::top_left);
        fb.setFont(&fonts::Font0);
        fb.setTextSize(1);
        fb.setTextColor(TFT_YELLOW, TFT_BLACK);
        fb.drawString("GPS SYNC", 4, 4);
    }
}
