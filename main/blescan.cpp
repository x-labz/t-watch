#include "blescan.h"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

static const char *TAG = "BLESCAN";
static constexpr int kMaxDevs = 10;
static constexpr int32_t kScanDurationMs = 4000;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static BleScanResult s_result;
static std::atomic<bool> s_scan_in_progress{false};
static SemaphoreHandle_t s_done;

// Records one advertisement into s_result, deduplicated by address so a
// device that advertises repeatedly during the window occupies one row and
// just refreshes its RSSI. Called from the NimBLE host task; everything it
// does inside the critical section is plain-data copying — never a blocking
// or driver call (see CLAUDE.md section 13).
static void record_device(const uint8_t addr[6], int8_t rssi, const char (&name)[25])
{
    portENTER_CRITICAL(&s_lock);
    int slot = -1;
    for (int i = 0; i < s_result.count; i++) {
        if (memcmp(s_result.devs[i].addr, addr, 6) == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0 && s_result.count < kMaxDevs) {
        slot = s_result.count++;
        memcpy(s_result.devs[slot].addr, addr, 6);
        s_result.devs[slot].name[0] = '\0';
    }
    if (slot >= 0) {
        s_result.devs[slot].rssi = rssi;
        // Keep the first non-empty name seen: many devices alternate between
        // an ADV packet carrying the name and a SCAN_RSP that does not.
        if (name[0] && !s_result.devs[slot].name[0]) {
            // Same fixed width on both sides and the caller always NUL-
            // terminates within it, so copy the buffer whole.
            memcpy(s_result.devs[slot].name, name, sizeof(s_result.devs[slot].name));
            s_result.devs[slot].name[sizeof(s_result.devs[slot].name) - 1] = '\0';
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

static int gap_event_cb(struct ble_gap_event *event, void *)
{
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            // Parse outside the lock — only the plain result of parsing goes
            // into the critical section.
            struct ble_hs_adv_fields fields;
            char name[25] = {0};
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                if (fields.name != nullptr && fields.name_len > 0) {
                    size_t n = fields.name_len;
                    if (n > sizeof(name) - 1) n = sizeof(name) - 1;
                    memcpy(name, fields.name, n);
                    name[n] = '\0';
                }
            }
            record_device(event->disc.addr.val, event->disc.rssi, name);
            break;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            xSemaphoreGive(s_done);
            break;
        default:
            break;
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);

    uint8_t own_addr_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        xSemaphoreGive(s_done);
        return;
    }

    struct ble_gap_disc_params params = {};
    params.passive = 1;             // listen only; never send SCAN_REQ
    params.filter_duplicates = 0;   // we dedup ourselves, so RSSI stays fresh

    rc = ble_gap_disc(own_addr_type, kScanDurationMs, &params, gap_event_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        xSemaphoreGive(s_done);
    }
}

static void ble_host_task(void *)
{
    nimble_port_run();              // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

static void ble_scan_task(void *)
{
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        portENTER_CRITICAL(&s_lock);
        s_result.scanning = false;
        portEXIT_CRITICAL(&s_lock);
        s_scan_in_progress.store(false, std::memory_order_relaxed);
        vTaskDelete(nullptr);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(ble_host_task);

    // on_sync gives the semaphore on failure too, so this only actually waits
    // the full timeout if the host wedges.
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(kScanDurationMs + 5000)) != pdTRUE) {
        ESP_LOGW(TAG, "scan did not complete in time, cancelling");
        ble_gap_disc_cancel();
    }

    // Never leave the controller up between scans (CLAUDE.md section 9).
    nimble_port_stop();
    nimble_port_deinit();

    portENTER_CRITICAL(&s_lock);
    s_result.scanning = false;
    uint8_t found = s_result.count;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "scan complete: %u device(s)", (unsigned)found);
    s_scan_in_progress.store(false, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

void ble_scan_start(void)
{
    bool expected = false;
    if (!s_scan_in_progress.compare_exchange_strong(expected, true)) {
        return;   // already scanning
    }
    if (s_done == nullptr) {
        s_done = xSemaphoreCreateBinary();
    }
    portENTER_CRITICAL(&s_lock);
    s_result.scanning = true;
    s_result.count = 0;
    portEXIT_CRITICAL(&s_lock);
    xTaskCreate(ble_scan_task, "ble_scan", 4096, nullptr, 4, nullptr);
}

BleScanResult ble_scan_read(void)
{
    BleScanResult copy;
    portENTER_CRITICAL(&s_lock);
    copy = s_result;
    portEXIT_CRITICAL(&s_lock);
    return copy;
}
