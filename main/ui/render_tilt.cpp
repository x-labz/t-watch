#include "render.h"

#include <cmath>
#include <cstdio>

void render_tilt(LGFX_Sprite &fb, const TiltVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::top_center);
    fb.setFont(&fonts::Font0);
    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("TILT", fb.width() / 2, 6);

    const int32_t cx = fb.width() / 2;
    const int32_t cy = 132;
    const int32_t radius = 84;
    const int32_t ball_radius = 12;

    uint16_t disc_fill = lgfx::color565(30, 30, 30);
    uint16_t crosshair = lgfx::color565(70, 70, 70);
    fb.fillCircle(cx, cy, radius, disc_fill);
    fb.drawLine(cx - radius, cy, cx + radius, cy, crosshair);
    fb.drawLine(cx, cy - radius, cx, cy + radius, crosshair);
    fb.drawCircle(cx, cy, radius, TFT_WHITE);

    // Ball position follows tilt directly: gravity's in-plane component
    // (accel_x_g, accel_y_g) points toward the "downhill" direction.
    float px = vm.accel_x_g * radius;
    float py = vm.accel_y_g * radius;
    float max_dist = (float)(radius - ball_radius);
    float dist = sqrtf(px * px + py * py);
    if (dist > max_dist && dist > 0) {
        float scale = max_dist / dist;
        px *= scale;
        py *= scale;
    }
    fb.fillCircle(cx + (int32_t)px, cy + (int32_t)py, ball_radius, TFT_RED);

    fb.setTextSize(1);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    char buf[32];
    snprintf(buf, sizeof(buf), "X:%+.2fg  Y:%+.2fg", (double)vm.accel_x_g, (double)vm.accel_y_g);
    fb.drawString(buf, fb.width() / 2, 228);
}
