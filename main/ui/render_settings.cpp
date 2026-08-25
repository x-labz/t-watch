#include "render.h"

#include <cstdio>

void render_settings(LGFX_Sprite &fb, const SettingsVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);

    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("SETTINGS", fb.width() / 2, 10);

    fb.setTextSize(1);
    fb.drawString("BACKLIGHT", fb.width() / 2, 45);

    fb.setTextDatum(textdatum_t::middle_center);
    fb.setFont(&fonts::Font7);
    fb.setTextSize(1);
    fb.setTextColor(TFT_YELLOW, TFT_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", vm.brightness_pct);
    fb.drawString(buf, fb.width() / 2, 95);

    const int32_t bar_x = 30, bar_y = 128, bar_w = 180, bar_h = 16;
    fb.drawRect(bar_x, bar_y, bar_w, bar_h, TFT_WHITE);
    int32_t fill_w = (bar_w - 2) * (int32_t)vm.brightness_pct / 100;
    if (fill_w > 0) {
        fb.fillRect(bar_x + 1, bar_y + 1, fill_w, bar_h - 2, TFT_YELLOW);
    }

    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);
    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("< DIM", fb.width() / 4, 185);
    fb.drawString("BRIGHT >", 3 * fb.width() / 4, 185);
    fb.setTextSize(1);
    fb.drawString("saved automatically", fb.width() / 2, 215);
}
