#include "ui_task.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "blescan.h"
#include "debug_console.h"
#include "gps.h"
#include "haptic.h"
#include "power.h"
#include "settings.h"
#include "tilt.h"
#include "time_sync.h"
#include "touch.h"
#include "ui/render.h"
#include "ui/view.h"
#include "ui/viewmodels.h"
#include "wifiscan.h"

static const char *TAG = "UI";

static constexpr int32_t kScreenW = 240;
static constexpr int32_t kScreenH = 240;
static constexpr int32_t kStripH = 40;
static constexpr int32_t kSwipeThresholdPx = 40;
static constexpr int64_t kSwipeDurationUs = 200000;
static constexpr uint32_t kTiltRefreshUs = 66000;   // ~15 Hz while TILT is focused
static constexpr uint16_t kHapticEffectMax = 123;   // DRV2605 ROM library size (CLAUDE.md section 2)
static constexpr int32_t kBrightnessStep = 26;       // ~10% of 255 per tap
static constexpr int32_t kBrightnessMin = 26;        // keep the screen from going fully dark

static std::atomic<bool> s_tick_pending{false};
static std::atomic<bool> s_tilt_tick_pending{false};

static void tick_cb(void *)
{
    s_tick_pending.store(true, std::memory_order_relaxed);
}

static void tilt_tick_cb(void *)
{
    s_tilt_tick_pending.store(true, std::memory_order_relaxed);
}

static inline int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

static BatteryVM to_battery_vm(const BatteryReading &r)
{
    BatteryVM vm;
    vm.battery_connected = r.battery_connected;
    vm.percent = r.percent;
    vm.voltage_mv = r.voltage_mv;
    vm.charging = r.charging;
    vm.vbus_in = r.vbus_in;
    return vm;
}

static GpsVM to_gps_vm(const GpsReading &r)
{
    GpsVM vm;
    vm.has_fix = r.has_fix;
    vm.satellites_used = r.satellites_used;
    vm.satellites_in_view = r.satellites_in_view;
    vm.hdop = r.hdop;
    vm.altitude_m = r.altitude_m;
    vm.speed_kmh = r.speed_kmh;
    vm.latitude = r.latitude;
    vm.longitude = r.longitude;
    vm.utc_hh = r.utc_hh;
    vm.utc_mm = r.utc_mm;
    vm.utc_ss = r.utc_ss;
    vm.sentence_count = r.sentence_count;
    vm.utc_offset_sec = time_sync_get_utc_offset_seconds();
    vm.clock_set = !time_sync_in_progress();
    switch (time_sync_tz_status()) {
        case TzStatus::ACQUIRED:        vm.tz_status = GpsTzVM::ACQUIRED; break;
        case TzStatus::WAITING_FOR_FIX: vm.tz_status = GpsTzVM::WAITING_FOR_FIX; break;
        default:                        vm.tz_status = GpsTzVM::CACHED; break;
    }
    return vm;
}

static TiltVM to_tilt_vm(const TiltReading &r)
{
    TiltVM vm;
    vm.accel_x_g = r.accel_x_g;
    vm.accel_y_g = r.accel_y_g;
    return vm;
}

static WifiVM to_wifi_vm(const WifiScanResult &r)
{
    WifiVM vm;
    vm.scanning = r.scanning;
    vm.count = r.count;
    for (uint8_t i = 0; i < r.count && i < 10; i++) {
        strncpy(vm.aps[i].ssid, r.aps[i].ssid, sizeof(vm.aps[i].ssid) - 1);
        vm.aps[i].rssi = r.aps[i].rssi;
    }
    return vm;
}

static BleVM to_ble_vm(const BleScanResult &r)
{
    BleVM vm;
    vm.scanning = r.scanning;
    vm.count = r.count;
    for (uint8_t i = 0; i < r.count && i < 10; i++) {
        strncpy(vm.devs[i].name, r.devs[i].name, sizeof(vm.devs[i].name) - 1);
        vm.devs[i].rssi = r.devs[i].rssi;
        // NimBLE stores the address little-endian; print it the conventional
        // way (most-significant octet first) so it matches what phone BLE
        // scanner apps show.
        const uint8_t *a = r.devs[i].addr;
        snprintf(vm.devs[i].addr, sizeof(vm.devs[i].addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 a[5], a[4], a[3], a[2], a[1], a[0]);
    }
    return vm;
}

static const char *view_name(ViewId id)
{
    switch (id) {
        case ViewId::WATCHFACE: return "WATCHFACE";
        case ViewId::BATTERY: return "BATTERY";
        case ViewId::GPS: return "GPS";
        case ViewId::TILT: return "TILT";
        case ViewId::HAPTIC: return "HAPTIC";
        case ViewId::SETTINGS: return "SETTINGS";
        case ViewId::WIFI: return "WIFI";
        case ViewId::BLE: return "BLE";
        default: return "?";
    }
}

static void render_view(ViewId id, LGFX_Sprite &frame, const WatchfaceVM &wf, const BatteryVM &batt,
                         const GpsVM &gps, const TiltVM &tilt, const HapticVM &haptic,
                         const SettingsVM &settings, const WifiVM &wifi, const BleVM &ble)
{
    switch (id) {
        case ViewId::WATCHFACE: render_watchface(frame, wf); break;
        case ViewId::BATTERY: render_battery(frame, batt); break;
        case ViewId::GPS: render_gps(frame, gps); break;
        case ViewId::TILT: render_tilt(frame, tilt); break;
        case ViewId::HAPTIC: render_haptic(frame, haptic); break;
        case ViewId::SETTINGS: render_settings(frame, settings); break;
        case ViewId::WIFI: render_wifi(frame, wifi); break;
        case ViewId::BLE: render_ble(frame, ble); break;
        default: break;
    }
}

static void push_frame_locked(LGFX_TWatch2020V2 &lcd, LGFX_Sprite &frame, esp_pm_lock_handle_t pm_lock)
{
    esp_pm_lock_acquire(pm_lock);
    lcd.startWrite();
    frame.pushSprite(&lcd, 0, 0);
    lcd.endWrite();
    esp_pm_lock_release(pm_lock);
}

// Slides `from` off-screen while `to` slides in, using the strip pipeline
// (CLAUDE.md section 8) so the animation stays smooth at 80 MHz SPI.
// to_enters_from_right selects swipe direction: true = swipe left (next
// view), false = swipe right (previous view).
static void animate_swipe(LGFX_TWatch2020V2 &lcd, LGFX_Sprite &from, LGFX_Sprite &to,
                           bool to_enters_from_right, LGFX_Sprite strips[2],
                           esp_pm_lock_handle_t pm_lock)
{
    int64_t t0 = esp_timer_get_time();
    int strip_idx = 0;

    esp_pm_lock_acquire(pm_lock);
    lcd.startWrite();
    for (;;) {
        int64_t elapsed = esp_timer_get_time() - t0;
        int32_t offset = (int32_t)((elapsed * kScreenW) / kSwipeDurationUs);
        if (offset > kScreenW) offset = kScreenW;

        int32_t from_x = to_enters_from_right ? -offset : offset;
        int32_t to_x = to_enters_from_right ? (kScreenW - offset) : (offset - kScreenW);

        for (int32_t y = 0; y < kScreenH; y += kStripH) {
            LGFX_Sprite &strip = strips[strip_idx & 1];
            strip.fillScreen(TFT_BLACK);
            from.pushSprite(&strip, from_x, -y);
            to.pushSprite(&strip, to_x, -y);
            lcd.pushImageDMA(0, y, kScreenW, kStripH, (uint16_t *)strip.getBuffer());
            strip_idx++;
        }
        if (offset >= kScreenW) break;
    }
    lcd.endWrite();
    esp_pm_lock_release(pm_lock);
}

static void ui_task_fn(void *arg)
{
    auto &lcd = *static_cast<LGFX_TWatch2020V2 *>(arg);

    // Full-frame sprites go in PSRAM: two of them (115.2KB each) don't both
    // fit in the DMA-capable internal RAM createSprite() defaults to, and
    // createSprite() fails silently on allocation failure (no crash, no log,
    // just a null backing buffer that renders as blank) — this was the cause
    // of the second view rendering empty. PSRAM is fine here since these are
    // single occasional redraws, not the animated strip pipeline below.
    LGFX_Sprite frame_a;
    frame_a.setColorDepth(16);
    frame_a.setPsram(true);
    if (!frame_a.createSprite(kScreenW, kScreenH)) {
        ESP_LOGE(TAG, "frame_a.createSprite() failed");
    }

    LGFX_Sprite frame_b;
    frame_b.setColorDepth(16);
    frame_b.setPsram(true);
    if (!frame_b.createSprite(kScreenW, kScreenH)) {
        ESP_LOGE(TAG, "frame_b.createSprite() failed");
    }

    LGFX_Sprite strips[2];
    for (auto &s : strips) {
        s.setColorDepth(16);
        s.setPsram(false);   // DMA cannot read PSRAM (CLAUDE.md section 8)
        if (!s.createSprite(kScreenW, kStripH)) {
            ESP_LOGE(TAG, "strip.createSprite() failed");
        }
    }

    esp_pm_lock_handle_t pm_lock;
    ESP_ERROR_CHECK(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "ui_render", &pm_lock));

    esp_timer_create_args_t tick_args = {};
    tick_args.callback = &tick_cb;
    tick_args.dispatch_method = ESP_TIMER_TASK;
    tick_args.name = "ui_tick";
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000000));

    // Faster tick so the tilt ball tracks motion smoothly while its view is
    // focused; a no-op the rest of the time (checked against `current`
    // below), so it costs nothing when the tilt view isn't visible.
    esp_timer_create_args_t tilt_tick_args = {};
    tilt_tick_args.callback = &tilt_tick_cb;
    tilt_tick_args.dispatch_method = ESP_TIMER_TASK;
    tilt_tick_args.name = "ui_tilt_tick";
    esp_timer_handle_t tilt_tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tilt_tick_args, &tilt_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tilt_tick_timer, kTiltRefreshUs));

    LGFX_Sprite *frame_cur = &frame_a;
    LGFX_Sprite *frame_other = &frame_b;

    ViewId current = ViewId::WATCHFACE;
    WatchfaceVM wf{};
    BatteryVM batt = to_battery_vm(power_read_battery());
    GpsVM gps = to_gps_vm(gps_read());
    TiltVM tilt = to_tilt_vm(tilt_read());
    HapticVM haptic{};
    int32_t brightness = settings_get_brightness();
    SettingsVM settings;
    settings.brightness_pct = (uint8_t)(((uint32_t)brightness * 100 + 127) / 255);
    WifiVM wifi = to_wifi_vm(wifi_scan_read());
    BleVM ble = to_ble_vm(ble_scan_read());

    auto redraw = [&]() {
        render_view(current, *frame_cur, wf, batt, gps, tilt, haptic, settings, wifi, ble);
        push_frame_locked(lcd, *frame_cur, pm_lock);
    };

    // Shared by physical swipes and the debug-console "view"/"next"/"prev"
    // commands, so both paths exercise identical view-transition logic.
    auto switch_view = [&](ViewId next, bool to_enters_from_right) {
        if (next == current) return;
        if (next == ViewId::TILT) {
            tilt = to_tilt_vm(tilt_read());
        }
        if (next == ViewId::WIFI) {
            wifi_scan_start();
            wifi = to_wifi_vm(wifi_scan_read());
        }
        if (next == ViewId::BLE) {
            ble_scan_start();
            ble = to_ble_vm(ble_scan_read());
        }
        render_view(next, *frame_other, wf, batt, gps, tilt, haptic, settings, wifi, ble);
        animate_swipe(lcd, *frame_cur, *frame_other, to_enters_from_right, strips, pm_lock);

        // GPS is the single biggest power draw on this board (CLAUDE.md
        // section 9) — only powered while its view is actually focused.
        if (current == ViewId::GPS) { gps_release(); time_sync_gps_session_end(); }
        if (next == ViewId::GPS) { gps_acquire(); time_sync_gps_session_begin(); }

        current = next;
        std::swap(frame_cur, frame_other);
        ESP_LOGI(TAG, "view -> %s", view_name(current));
    };

    // Shared by physical taps and the debug-console "tap" command.
    // screen_x is already mirror-corrected (see the swipe-direction comment
    // at the physical-touch call site).
    auto do_tap = [&](int32_t screen_x) {
        bool right_half = screen_x >= (kScreenW / 2);
        if (current == ViewId::HAPTIC) {
            if (right_half) {
                haptic.effect_id = (haptic.effect_id % kHapticEffectMax) + 1;
            } else {
                haptic.effect_id = (haptic.effect_id == 1) ? kHapticEffectMax : haptic.effect_id - 1;
            }
            haptic_play(haptic.effect_id);
            ESP_LOGI(TAG, "haptic effect -> %u", haptic.effect_id);
            redraw();
        } else if (current == ViewId::SETTINGS) {
            brightness += right_half ? kBrightnessStep : -kBrightnessStep;
            if (brightness < kBrightnessMin) brightness = kBrightnessMin;
            if (brightness > 255) brightness = 255;

            lcd.setBrightness((uint8_t)brightness);
            settings_set_brightness((uint8_t)brightness);
            settings.brightness_pct = (uint8_t)(((uint32_t)brightness * 100 + 127) / 255);
            ESP_LOGI(TAG, "brightness -> %ld (%u%%)", (long)brightness, settings.brightness_pct);
            redraw();
        } else if (current == ViewId::WIFI) {
            // Any tap on the WIFI view re-scans (CLAUDE.md section 9:
            // wifi_scan_start() tears STA fully down when it finishes, so
            // this never leaves the radio idling between taps).
            wifi_scan_start();
            wifi = to_wifi_vm(wifi_scan_read());
            ESP_LOGI(TAG, "wifi rescan requested");
            redraw();
        } else if (current == ViewId::BLE) {
            ble_scan_start();
            ble = to_ble_vm(ble_scan_read());
            ESP_LOGI(TAG, "ble rescan requested");
            redraw();
        }
    };

    redraw();

    bool touching = false;
    int32_t start_x = 0, start_y = 0, last_x = 0, last_y = 0;

    for (;;) {
        DebugCmd dbg;
        while (debug_console_poll(&dbg)) {
            switch (dbg.type) {
                case DebugCmdType::GOTO_VIEW: {
                    ViewId next = static_cast<ViewId>(dbg.view_index);
                    bool forward = dbg.view_index > static_cast<uint8_t>(current);
                    switch_view(next, forward);
                    break;
                }
                case DebugCmdType::NEXT_VIEW: {
                    uint8_t idx = static_cast<uint8_t>(current);
                    if (idx + 1 < static_cast<uint8_t>(ViewId::COUNT)) {
                        switch_view(static_cast<ViewId>(idx + 1), true);
                    }
                    break;
                }
                case DebugCmdType::PREV_VIEW: {
                    uint8_t idx = static_cast<uint8_t>(current);
                    if (idx > 0) {
                        switch_view(static_cast<ViewId>(idx - 1), false);
                    }
                    break;
                }
                case DebugCmdType::TAP:
                    do_tap(dbg.tap_x);
                    break;
                case DebugCmdType::STATUS:
                    // Free-heap is here mainly to catch leaks across repeated
                    // radio bring-up/teardown cycles (wifi + nimble both fully
                    // init and deinit per scan).
                    ESP_LOGI(TAG, "status: view=%s wifi(scanning=%d count=%u) ble(scanning=%d count=%u) "
                                  "brightness=%ld heap=%u internal=%u",
                             view_name(current), (int)wifi.scanning, wifi.count,
                             (int)ble.scanning, ble.count, (long)brightness,
                             (unsigned)esp_get_free_heap_size(),
                             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                    break;
                case DebugCmdType::TOUCH_INFO:
                    ESP_LOGI(TAG, "touch: consecutive_errors=%u probe(0x38)=%s",
                             (unsigned)touch_error_count(), esp_err_to_name(touch_probe()));
                    power_log_rails();
                    power_scan_bus0();
                    touch_scan_bus();
                    break;
                case DebugCmdType::TOUCH_FIX:
                    touch_recover();
                    break;
                case DebugCmdType::TOUCH_SLEEP:
                    touch_force_deepsleep();
                    break;
                case DebugCmdType::TOUCH_DUMP:
                    touch_dump_registers();
                    break;
                case DebugCmdType::TOUCH_MON:
                    touch_monitor_raw(10);
                    break;
                case DebugCmdType::TOUCH_WRITE:
                    touch_write_reg(dbg.reg, dbg.val);
                    break;
                case DebugCmdType::EXTEN_SET:
                    power_set_exten(dbg.flag);
                    break;
                case DebugCmdType::LDO3_MODE:
                    power_set_ldo3_dcin_mode(dbg.flag);
                    vTaskDelay(pdMS_TO_TICKS(200));
                    ESP_LOGW(TAG, "after LDO3 mode change: probe(0x38)=%s",
                             esp_err_to_name(touch_probe()));
                    break;
                default: break;
            }
        }

        if (s_tick_pending.exchange(false, std::memory_order_relaxed)) {
            // Before a GPS fix syncs the clock, time(nullptr) just counts up
            // from the epoch since boot — same visual behavior the old
            // free-running counter had, but it becomes real wall time the
            // moment settimeofday() is called, with no special-casing here.
            time_t local = time(nullptr) + time_sync_get_utc_offset_seconds();
            wf.hh = (local / 3600) % 24;
            wf.mm = (local / 60) % 60;
            wf.ss = local % 60;
            wf.gps_sync_blink = time_sync_in_progress() && ((local % 2) == 0);
            batt = to_battery_vm(power_read_battery());
            // Timezone/clock derivation happens only while the GPS view is
            // open — that is the only time the receiver is powered at all
            // (CLAUDE.md section 9), and it keeps boot free of GPS entirely.
            if (current == ViewId::GPS) {
                time_sync_feed_gps(gps_read());
            }
            gps = to_gps_vm(gps_read());
            tilt = to_tilt_vm(tilt_read());
            if (current == ViewId::WIFI) wifi = to_wifi_vm(wifi_scan_read());
            if (current == ViewId::BLE) ble = to_ble_vm(ble_scan_read());

            redraw();
        }

        if (s_tilt_tick_pending.exchange(false, std::memory_order_relaxed) && current == ViewId::TILT) {
            tilt = to_tilt_vm(tilt_read());
            redraw();
        }

        int32_t x = 0, y = 0;
        bool pressed = touch_read(&x, &y);
        if (pressed) {
            if (!touching) {
                touching = true;
                start_x = x;
                start_y = y;
                ESP_LOGI(TAG, "touch down x=%d y=%d", (int)x, (int)y);
            }
            last_x = x;
            last_y = y;
        } else if (touching) {
            touching = false;
            int32_t dx = last_x - start_x;
            int32_t dy = last_y - start_y;
            ESP_LOGI(TAG, "touch up start=(%d,%d) last=(%d,%d) dx=%d dy=%d",
                     (int)start_x, (int)start_y, (int)last_x, (int)last_y, (int)dx, (int)dy);

            if (iabs(dx) >= kSwipeThresholdPx && iabs(dx) > iabs(dy)) {
                // This unit's touch panel reports X mirrored relative to the
                // display (confirmed on hardware), so the sign is inverted
                // here rather than in the touch driver's coordinate pipeline.
                bool to_enters_from_right = dx > 0;
                uint8_t idx = static_cast<uint8_t>(current);
                uint8_t next_idx = idx;
                if (to_enters_from_right && idx + 1 < static_cast<uint8_t>(ViewId::COUNT)) {
                    next_idx = idx + 1;
                } else if (!to_enters_from_right && idx > 0) {
                    next_idx = idx - 1;
                }
                ViewId next = static_cast<ViewId>(next_idx);
                switch_view(next, to_enters_from_right);
            } else if (current == ViewId::HAPTIC || current == ViewId::SETTINGS ||
                       current == ViewId::WIFI || current == ViewId::BLE) {
                // Small displacement = a tap, not a swipe. This unit's touch
                // panel reports X mirrored relative to the display (see the
                // swipe-direction fix above), so undo that for absolute
                // hit-testing too, not just the relative dx sign.
                int32_t screen_x = (kScreenW - 1) - last_x;
                do_tap(screen_x);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ui_task_start(LGFX_TWatch2020V2 &lcd)
{
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 8192, &lcd, 5, nullptr, 1);
}
