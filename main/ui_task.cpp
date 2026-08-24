#include "ui_task.h"

#include <atomic>
#include <utility>

#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "power.h"
#include "ui/render.h"
#include "ui/view.h"
#include "ui/viewmodels.h"

static const char *TAG = "UI";

static constexpr int32_t kScreenW = 240;
static constexpr int32_t kScreenH = 240;
static constexpr int32_t kStripH = 40;
static constexpr int32_t kSwipeThresholdPx = 40;
static constexpr int64_t kSwipeDurationUs = 200000;

static std::atomic<bool> s_tick_pending{false};
static std::atomic<uint32_t> s_seconds{0};

static void tick_cb(void *)
{
    s_seconds.fetch_add(1, std::memory_order_relaxed);
    s_tick_pending.store(true, std::memory_order_relaxed);
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

static void render_view(ViewId id, LGFX_Sprite &frame, const WatchfaceVM &wf, const BatteryVM &batt)
{
    if (id == ViewId::WATCHFACE) {
        render_watchface(frame, wf);
    } else {
        render_battery(frame, batt);
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

    LGFX_Sprite frame_a;
    frame_a.setColorDepth(16);
    frame_a.createSprite(kScreenW, kScreenH);

    LGFX_Sprite frame_b;
    frame_b.setColorDepth(16);
    frame_b.createSprite(kScreenW, kScreenH);

    LGFX_Sprite strips[2];
    for (auto &s : strips) {
        s.setColorDepth(16);
        s.setPsram(false);   // DMA cannot read PSRAM (CLAUDE.md section 8)
        s.createSprite(kScreenW, kStripH);
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

    LGFX_Sprite *frame_cur = &frame_a;
    LGFX_Sprite *frame_other = &frame_b;

    ViewId current = ViewId::WATCHFACE;
    WatchfaceVM wf{};
    BatteryVM batt = to_battery_vm(power_read_battery());

    render_view(current, *frame_cur, wf, batt);
    push_frame_locked(lcd, *frame_cur, pm_lock);

    bool touching = false;
    int32_t start_x = 0, start_y = 0, last_x = 0, last_y = 0;

    for (;;) {
        if (s_tick_pending.exchange(false, std::memory_order_relaxed)) {
            uint32_t secs = s_seconds.load(std::memory_order_relaxed);
            wf.hh = (secs / 3600) % 24;
            wf.mm = (secs / 60) % 60;
            wf.ss = secs % 60;
            batt = to_battery_vm(power_read_battery());

            render_view(current, *frame_cur, wf, batt);
            push_frame_locked(lcd, *frame_cur, pm_lock);
        }

        int32_t x = 0, y = 0;
        bool pressed = lcd.getTouch(&x, &y);
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
                ViewId next = current;
                // This unit's touch panel reports X mirrored relative to the
                // display (confirmed on hardware), so the sign is inverted
                // here rather than in the touch driver's coordinate pipeline.
                bool to_enters_from_right = dx > 0;
                if (to_enters_from_right && current == ViewId::WATCHFACE) {
                    next = ViewId::BATTERY;
                } else if (!to_enters_from_right && current == ViewId::BATTERY) {
                    next = ViewId::WATCHFACE;
                }

                if (next != current) {
                    render_view(next, *frame_other, wf, batt);
                    animate_swipe(lcd, *frame_cur, *frame_other, to_enters_from_right, strips, pm_lock);
                    current = next;
                    std::swap(frame_cur, frame_other);
                    ESP_LOGI(TAG, "view -> %s (dx=%d dy=%d) batt: connected=%d pct=%d mv=%u chg=%d vbus=%d wf=%02u:%02u:%02u",
                             current == ViewId::WATCHFACE ? "WATCHFACE" : "BATTERY", (int)dx, (int)dy,
                             batt.battery_connected, batt.percent, batt.voltage_mv, batt.charging, batt.vbus_in,
                             wf.hh, wf.mm, wf.ss);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void ui_task_start(LGFX_TWatch2020V2 &lcd)
{
    xTaskCreatePinnedToCore(ui_task_fn, "ui_task", 8192, &lcd, 5, nullptr, 1);
}
