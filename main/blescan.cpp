#include "blescan.h"

#include "sdkconfig.h"

#if !CONFIG_BT_ENABLED

// Bluetooth compiled out: keep the view harmless rather than failing the
// build, so BT can be toggled off (e.g. to A/B a suspected interaction with
// another peripheral) without touching the UI layer.
void ble_scan_start(void) {}
BleScanResult ble_scan_read(void) { return BleScanResult{}; }

#else

#include <atomic>
#include <cstdio>
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
// Tracks, per slot, whether the stored name came from a "complete local name"
// AD field rather than a shortened one, so a later complete name can upgrade
// an earlier abbreviated one. Kept alongside s_result rather than inside it so
// the public struct stays plain data for the UI.
static bool s_name_complete[kMaxDevs];

// Bluetooth SIG company identifiers, for the very common case where a device
// advertises no name at all. Phones and wearables using address privacy
// (random addresses — most of what a scan sees) deliberately omit their name,
// but nearly all still include manufacturer data, so the company ID is the
// only human-meaningful label available for them.
static const char *company_name(uint16_t id)
{
    switch (id) {
        case 0x004C: return "Apple";
        case 0x0006: return "Microsoft";
        case 0x0075: return "Samsung";
        case 0x00E0: return "Google";
        case 0x0087: return "Garmin";
        case 0x0059: return "Nordic";
        case 0x02E5: return "Espressif";
        case 0x0157: return "Amazfit";
        case 0x038F: return "Xiaomi";
        case 0x0171: return "Amazon";
        case 0x000F: return "Broadcom";
        case 0x0001: return "Ericsson";
        case 0x00D2: return "Logitech";
        case 0x0499: return "Ruuvi";
        case 0x0310: return "SGL Italia";
        case 0x0141: return "Fitbit";
        default: return nullptr;
    }
}

static void record_device(const uint8_t addr[6], int8_t rssi, const char (&name)[25],
                          bool name_complete)
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
        s_name_complete[slot] = false;
    }
    if (slot >= 0) {
        s_result.devs[slot].rssi = rssi;
        // A device typically sends its name in the SCAN_RSP rather than the
        // ADV, so entries start nameless and get filled in later in the scan.
        // Take a name when we have none, and upgrade a shortened name if the
        // complete one turns up.
        bool have_none = !s_result.devs[slot].name[0];
        // A leading '~' marks an inferred manufacturer label rather than a name
        // the device actually advertised, so any real name supersedes it.
        bool stored_is_placeholder = s_result.devs[slot].name[0] == '~';
        bool incoming_is_real = name[0] && name[0] != '~';
        bool upgrade = (name_complete && !s_name_complete[slot]) ||
                       (stored_is_placeholder && incoming_is_real);
        if (name[0] && (have_none || upgrade)) {
            // Same fixed width on both sides and the caller always NUL-
            // terminates within it, so copy the buffer whole.
            memcpy(s_result.devs[slot].name, name, sizeof(s_result.devs[slot].name));
            s_result.devs[slot].name[sizeof(s_result.devs[slot].name) - 1] = '\0';
            s_name_complete[slot] = name_complete;
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
            bool complete = false;
            if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) == 0) {
                if (fields.name != nullptr && fields.name_len > 0) {
                    size_t n = fields.name_len;
                    if (n > sizeof(name) - 1) n = sizeof(name) - 1;
                    memcpy(name, fields.name, n);
                    name[n] = '\0';
                    complete = fields.name_is_complete;
                } else if (fields.mfg_data != nullptr && fields.mfg_data_len >= 2) {
                    // No advertised name. Fall back to the manufacturer, marked
                    // with "~" so the display never implies the device actually
                    // called itself that. A real name from a later SCAN_RSP
                    // still wins, because `complete` stays false here.
                    uint16_t cid = (uint16_t)(fields.mfg_data[0] | (fields.mfg_data[1] << 8));
                    const char *vendor = company_name(cid);
                    if (vendor != nullptr) {
                        snprintf(name, sizeof(name), "~%s", vendor);
                    } else {
                        snprintf(name, sizeof(name), "~mfg %04X", cid);
                    }
                }
            }
            record_device(event->disc.addr.val, event->disc.rssi, name, complete);
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
    // ACTIVE scan. Most devices advertise only their service UUIDs and put the
    // name in the SCAN_RSP, which is only sent in reply to a SCAN_REQ — so a
    // passive scan lists almost everything by bare MAC address. Active costs a
    // brief transmit per device, but the radio is only up for the 4 s window
    // either way (CLAUDE.md section 9), and names are the point of this view.
    params.passive = 0;
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

    BleScanResult snapshot;
    portENTER_CRITICAL(&s_lock);
    s_result.scanning = false;
    snapshot = s_result;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "scan complete: %u device(s)", (unsigned)snapshot.count);
    for (uint8_t i = 0; i < snapshot.count; i++) {
        const uint8_t *a = snapshot.devs[i].addr;
        ESP_LOGI(TAG, "  %-24s %02X:%02X:%02X:%02X:%02X:%02X  %d dBm",
                 snapshot.devs[i].name[0] ? snapshot.devs[i].name : "(no name)",
                 a[5], a[4], a[3], a[2], a[1], a[0], snapshot.devs[i].rssi);
    }
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

#endif  // CONFIG_BT_ENABLED
