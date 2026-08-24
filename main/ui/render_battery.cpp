#include "render.h"

#include <cstdio>

void render_battery(LGFX_Sprite &fb, const BatteryVM &vm)
{
    fb.fillScreen(TFT_BLACK);
    fb.setTextDatum(textdatum_t::middle_center);
    fb.setFont(&fonts::Font0);

    fb.setTextSize(2);
    fb.setTextColor(TFT_WHITE, TFT_BLACK);
    fb.drawString("BATTERY", fb.width() / 2, 40);

    char pct_buf[8];
    if (vm.battery_connected) {
        snprintf(pct_buf, sizeof(pct_buf), "%d%%", vm.percent);
    } else {
        snprintf(pct_buf, sizeof(pct_buf), "--");
    }
    fb.setTextSize(5);
    fb.drawString(pct_buf, fb.width() / 2, fb.height() / 2);

    char mv_buf[24];
    snprintf(mv_buf, sizeof(mv_buf), "%u mV", vm.voltage_mv);
    fb.setTextSize(2);
    fb.drawString(mv_buf, fb.width() / 2, fb.height() / 2 + 55);

    const char *status = vm.charging ? "CHARGING" : (vm.vbus_in ? "USB" : "ON BATTERY");
    fb.setTextColor(vm.charging ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    fb.drawString(status, fb.width() / 2, fb.height() / 2 + 85);
}
