idf_component_register(
    SRCS "main.cpp" "power.cpp" "touch.cpp" "gps.cpp" "tilt.cpp" "haptic.cpp" "settings.cpp" "time_sync.cpp" "powersave.cpp" "rtc.cpp" "wifiscan.cpp" "blescan.cpp" "debug_console.cpp" "ui_task.cpp" "ui/render_watchface.cpp" "ui/render_battery.cpp" "ui/render_gps.cpp" "ui/render_tilt.cpp" "ui/render_haptic.cpp" "ui/render_settings.cpp" "ui/render_wifi.cpp" "ui/render_ble.cpp"
    INCLUDE_DIRS "."
    REQUIRES LovyanGFX XPowersLib lewisxhe__sensorlib driver esp_timer esp_pm nvs_flash esp_wifi esp_netif esp_event bt
)
