#include "render.h"

#include <cstdio>

void render_haptic(LGFX_Sprite &fb, const HapticVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);

    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("HAPTIC", fb.width() / 2, 10);

    fb.setTextDatum(textdatum_t::middle_center);
    fb.setFont(&fonts::Font7);
    fb.setTextSize(1);
    fb.setTextColor(TFT_YELLOW, TFT_BLACK);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", vm.effect_id);
    fb.drawString(buf, fb.width() / 2, 105);

    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);
    fb.setTextSize(1);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("ROM EFFECT", fb.width() / 2, 148);

    fb.drawLine(fb.width() / 2, 170, fb.width() / 2, 220, lgfx::color565(60, 60, 60));

    fb.setTextSize(2);
    fb.drawString("< PREV", fb.width() / 4, 185);
    fb.drawString("NEXT >", 3 * fb.width() / 4, 185);
    fb.setTextSize(1);
    fb.drawString("tap plays it", fb.width() / 2, 210);
}
