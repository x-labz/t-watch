#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "SETTINGS";
static constexpr const char *kNamespace = "twatch";
static constexpr const char *kKeyBrightness = "brightness";
static constexpr const char *kKeyScreenTimeout = "scr_timeout";
static constexpr uint8_t kDefaultBrightness = 255;
static constexpr uint16_t kDefaultScreenTimeoutS = 15;

static uint8_t s_brightness = kDefaultBrightness;
static uint16_t s_screen_timeout_s = kDefaultScreenTimeoutS;

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase (%s) — erasing and retrying", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t stored = kDefaultBrightness;
        if (nvs_get_u8(handle, kKeyBrightness, &stored) == ESP_OK) {
            s_brightness = stored;
        }
        uint16_t timeout = kDefaultScreenTimeoutS;
        if (nvs_get_u16(handle, kKeyScreenTimeout, &timeout) == ESP_OK) {
            s_screen_timeout_s = timeout;
        }
        nvs_close(handle);
    }
    ESP_LOGI(TAG, "loaded brightness=%u screen_timeout=%us", s_brightness, s_screen_timeout_s);
    return ESP_OK;
}

uint8_t settings_get_brightness(void)
{
    return s_brightness;
}

void settings_set_brightness(uint8_t brightness)
{
    s_brightness = brightness;

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (write) failed");
        return;
    }
    nvs_set_u8(handle, kKeyBrightness, brightness);
    nvs_commit(handle);
    nvs_close(handle);
}

uint16_t settings_get_screen_timeout_s(void)
{
    return s_screen_timeout_s;
}

void settings_set_screen_timeout_s(uint16_t seconds)
{
    s_screen_timeout_s = seconds;

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open (write) failed");
        return;
    }
    nvs_set_u16(handle, kKeyScreenTimeout, seconds);
    nvs_commit(handle);
    nvs_close(handle);
}
