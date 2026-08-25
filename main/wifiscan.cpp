#include "wifiscan.h"

#include <atomic>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "WIFISCAN";
static constexpr int kMaxAps = 10;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static WifiScanResult s_result;
static std::atomic<bool> s_scan_in_progress{false};
static bool s_netif_ready = false;

static void wifi_scan_task(void *)
{
    if (!s_netif_ready) {
        esp_netif_init();
        esp_event_loop_create_default();
        esp_netif_create_default_wifi_sta();
        s_netif_ready = true;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        s_scan_in_progress.store(false, std::memory_order_relaxed);
        vTaskDelete(nullptr);
        return;
    }
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    wifi_scan_config_t scan_cfg = {};
    esp_err_t scan_err = esp_wifi_scan_start(&scan_cfg, true);   // blocking

    // Fetch the records BEFORE taking the lock. esp_wifi_scan_get_ap_records()
    // blocks on an internal semaphore, and blocking inside portENTER_CRITICAL
    // (interrupts off, spinlock held) corrupts scheduler state — it hung both
    // cores with an interrupt-watchdog panic until this was moved out.
    uint16_t num = 0;
    static wifi_ap_record_t records[kMaxAps];
    if (scan_err == ESP_OK) {
        num = kMaxAps;
        esp_err_t get_err = esp_wifi_scan_get_ap_records(&num, records);
        if (get_err != ESP_OK) {
            ESP_LOGE(TAG, "get_ap_records failed: %s", esp_err_to_name(get_err));
            num = 0;
        }
    } else {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(scan_err));
    }

    portENTER_CRITICAL(&s_lock);
    s_result.count = 0;
    for (int i = 0; i < num && i < kMaxAps; i++) {
        strncpy(s_result.aps[i].ssid, (const char *)records[i].ssid, sizeof(s_result.aps[i].ssid) - 1);
        s_result.aps[i].ssid[sizeof(s_result.aps[i].ssid) - 1] = '\0';
        s_result.aps[i].rssi = records[i].rssi;
        s_result.count++;
    }
    s_result.scanning = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "scan complete: %u AP(s)", (unsigned)s_result.count);

    // Never leave STA idling (CLAUDE.md section 9) — torn down every time,
    // not just on view exit, since a scan result is a static snapshot.
    esp_wifi_stop();
    esp_wifi_deinit();

    s_scan_in_progress.store(false, std::memory_order_relaxed);
    vTaskDelete(nullptr);
}

void wifi_scan_start(void)
{
    bool expected = false;
    if (!s_scan_in_progress.compare_exchange_strong(expected, true)) {
        return;   // already scanning
    }
    portENTER_CRITICAL(&s_lock);
    s_result.scanning = true;
    portEXIT_CRITICAL(&s_lock);
    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, nullptr, 4, nullptr);
}

WifiScanResult wifi_scan_read(void)
{
    WifiScanResult copy;
    portENTER_CRITICAL(&s_lock);
    copy = s_result;
    portEXIT_CRITICAL(&s_lock);
    return copy;
}
