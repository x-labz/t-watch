idf_component_register(
    SRCS "main.cpp" "power.cpp" "touch.cpp" "gps.cpp" "ui_task.cpp" "ui/render_watchface.cpp" "ui/render_battery.cpp" "ui/render_gps.cpp"
    INCLUDE_DIRS "."
    REQUIRES LovyanGFX XPowersLib driver esp_timer esp_pm nvs_flash
)
