idf_component_register(
    SRCS "main.cpp" "power.cpp" "touch.cpp" "gps.cpp" "tilt.cpp" "haptic.cpp" "time_sync.cpp" "ui_task.cpp" "ui/render_watchface.cpp" "ui/render_battery.cpp" "ui/render_gps.cpp" "ui/render_tilt.cpp" "ui/render_haptic.cpp"
    INCLUDE_DIRS "."
    REQUIRES LovyanGFX XPowersLib lewisxhe__sensorlib driver esp_timer esp_pm nvs_flash
)
