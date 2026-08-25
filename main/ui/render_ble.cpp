#include "render.h"

#include <cstdio>

void render_ble(LGFX_Sprite &fb, const BleVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);
    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("BLE", fb.width() / 2, 8);

    fb.setTextSize(1);
    if (vm.scanning) {
        fb.setTextColor(TFT_CYAN, TFT_BLACK);
        fb.drawString("SCANNING...", fb.width() / 2, 32);
    } else if (vm.count == 0) {
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        fb.drawString("NO DEVICES FOUND", fb.width() / 2, 32);
    } else {
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        fb.drawString("TAP TO RESCAN", fb.width() / 2, 32);
    }

    // Two lines per device (name, then address) so unnamed beacons — most of
    // what a BLE scan actually turns up — are still identifiable.
    const int32_t row_h = 21;
    const int32_t list_top = 48;
    for (uint8_t i = 0; i < vm.count && i < 8; i++) {
        int32_t y = list_top + i * row_h;
        const BleDevVM &d = vm.devs[i];

        int32_t bars = 1;
        if (d.rssi >= -60) bars = 4;
        else if (d.rssi >= -75) bars = 3;
        else if (d.rssi >= -90) bars = 2;
        for (int b = 0; b < 4; b++) {
            int32_t bar_h = 4 + b * 3;
            int32_t bx = 6 + b * 5;
            int32_t by = y + 13 - bar_h;
            uint16_t color = (b < bars) ? TFT_CYAN : TFT_DARKGREY;
            fb.fillRect(bx, by, 3, bar_h, color);
        }

        fb.setTextDatum(textdatum_t::top_left);
        if (d.name[0]) {
            fb.setTextColor(TFT_WHITE, TFT_BLACK);
            fb.drawString(d.name, 30, y);
            fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
            fb.drawString(d.addr, 30, y + 9);
        } else {
            fb.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            fb.drawString(d.addr, 30, y + 4);
        }
    }

    if (vm.count > 8) {
        fb.setTextDatum(textdatum_t::top_center);
        fb.setTextColor(TFT_DARKGREY, TFT_BLACK);
        char more[24];
        snprintf(more, sizeof(more), "+%u more", (unsigned)(vm.count - 8));
        fb.drawString(more, fb.width() / 2, 224);
    }
}
