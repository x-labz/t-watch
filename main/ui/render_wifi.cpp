#include "render.h"

void render_wifi(LGFX_Sprite &fb, const WifiVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);
    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("WIFI", fb.width() / 2, 8);

    fb.setTextSize(1);
    if (vm.scanning) {
        fb.setTextColor(TFT_YELLOW, TFT_BLACK);
        fb.drawString("SCANNING...", fb.width() / 2, 32);
    } else if (vm.count == 0) {
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        fb.drawString("NO NETWORKS FOUND", fb.width() / 2, 32);
    } else {
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        fb.drawString("TAP TO RESCAN", fb.width() / 2, 32);
    }

    fb.setTextDatum(textdatum_t::top_left);
    const int32_t row_h = 18;
    const int32_t list_top = 48;
    for (uint8_t i = 0; i < vm.count && i < 10; i++) {
        int32_t y = list_top + i * row_h;
        const WifiApVM &ap = vm.aps[i];

        // Bar-graph signal indicator: 4 bars, thresholds roughly match the
        // usual RSSI bands (-55/-70/-85 dBm) so it reads like a phone's icon.
        int32_t bars = 1;
        if (ap.rssi >= -55) bars = 4;
        else if (ap.rssi >= -70) bars = 3;
        else if (ap.rssi >= -85) bars = 2;
        for (int b = 0; b < 4; b++) {
            int32_t bar_h = 4 + b * 3;
            int32_t bx = 6 + b * 5;
            int32_t by = y + 13 - bar_h;
            uint16_t color = (b < bars) ? TFT_GREEN : TFT_DARKGREY;
            fb.fillRect(bx, by, 3, bar_h, color);
        }

        fb.setTextColor(TFT_WHITE, TFT_BLACK);
        const char *ssid = ap.ssid[0] ? ap.ssid : "(hidden)";
        fb.drawString(ssid, 30, y);
    }

    if (vm.count == 0 && !vm.scanning) {
        fb.setTextDatum(textdatum_t::middle_center);
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        fb.drawString("tap to scan", fb.width() / 2, fb.height() / 2 + 10);
    }
}
