#include "gps.h"
#include "power.h"
#include "twatch_v2_pins.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "GPS";

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static GpsReading s_reading;

static std::atomic<int> s_refcount{0};
static std::atomic<bool> s_active{false};

static float nmea_coord_to_decimal(const char *field, char hemi)
{
    if (!field || !*field) return 0;
    double val = atof(field);
    double deg_scale = (hemi == 'N' || hemi == 'S') ? 100.0 : 1000.0;
    double degrees = floor(val / deg_scale);
    double minutes = val - degrees * deg_scale;
    double dec = degrees + minutes / 60.0;
    if (hemi == 'S' || hemi == 'W') dec = -dec;
    return (float)dec;
}

static int split_fields(char *line, char *fields[], int max_fields)
{
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (*p && n < max_fields) {
        if (*p == ',') {
            *p = 0;
            fields[n++] = p + 1;
        } else if (*p == '*') {
            *p = 0;
            break;
        }
        p++;
    }
    return n;
}

static void parse_gga(char *fields[], int n)
{
    if (n < 10) return;
    // $--GGA,time,lat,N/S,lon,E/W,fixquality,numSV,HDOP,alt,M,...
    portENTER_CRITICAL(&s_lock);
    if (strlen(fields[1]) >= 6) {
        s_reading.utc_hh = (fields[1][0] - '0') * 10 + (fields[1][1] - '0');
        s_reading.utc_mm = (fields[1][2] - '0') * 10 + (fields[1][3] - '0');
        s_reading.utc_ss = (fields[1][4] - '0') * 10 + (fields[1][5] - '0');
    }
    int fixq = atoi(fields[6]);
    s_reading.fix_quality = (uint8_t)fixq;
    s_reading.has_fix = fixq > 0;
    s_reading.satellites_used = (uint8_t)atoi(fields[7]);
    s_reading.hdop = (float)atof(fields[8]);
    s_reading.altitude_m = (float)atof(fields[9]);
    if (fields[2][0] && fields[4][0]) {
        s_reading.latitude = nmea_coord_to_decimal(fields[2], fields[3][0]);
        s_reading.longitude = nmea_coord_to_decimal(fields[4], fields[5][0]);
    }
    portEXIT_CRITICAL(&s_lock);
}

static void parse_rmc(char *fields[], int n)
{
    if (n < 9) return;
    // $--RMC,time,status,lat,N/S,lon,E/W,speed_knots,course,date,...
    portENTER_CRITICAL(&s_lock);
    float knots = (float)atof(fields[7]);
    s_reading.speed_kmh = knots * 1.852f;
    s_reading.course_deg = (float)atof(fields[8]);

    // Date/time in $RMC comes from the satellite navigation message, decoded
    // independently of the position solution — don't gate it on the "A"
    // (valid fix) status flag, or the clock waits far longer than it needs
    // to. Just sanity-check the date is in range, not all-zero garbage.
    if (n >= 10 && strlen(fields[9]) >= 6) {
        uint8_t day = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
        uint8_t month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
        uint8_t year = (fields[9][4] - '0') * 10 + (fields[9][5] - '0');
        if (day >= 1 && day <= 31 && month >= 1 && month <= 12) {
            s_reading.date_day = day;
            s_reading.date_month = month;
            s_reading.date_year = year;
            s_reading.has_date_time = true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}

static void parse_gsv(char *fields[], int n)
{
    if (n < 4) return;
    // $--GSV,total_msgs,msg_num,sats_in_view,[prn,elev,azim,snr]x4,...
    // Only one talker's count (last GSV line wins) — an approximation, not a
    // true cross-constellation sum, but plenty for a status indicator.
    portENTER_CRITICAL(&s_lock);
    s_reading.satellites_in_view = (uint8_t)atoi(fields[3]);
    portEXIT_CRITICAL(&s_lock);
}

static void parse_nmea_line(char *line)
{
    if (line[0] != '$' || strlen(line) < 6) return;

    portENTER_CRITICAL(&s_lock);
    s_reading.sentence_count++;
    portEXIT_CRITICAL(&s_lock);

    char *fields[20];
    int n = split_fields(line, fields, 20);
    if (n < 1 || strlen(fields[0]) < 6) return;

    const char *sentence = &fields[0][3];
    if (strncmp(sentence, "GGA", 3) == 0) {
        parse_gga(fields, n);
    } else if (strncmp(sentence, "RMC", 3) == 0) {
        parse_rmc(fields, n);
    } else if (strncmp(sentence, "GSV", 3) == 0) {
        parse_gsv(fields, n);
    }
}

static void gps_task(void *)
{
    char line[128];
    int line_len = 0;
    uint8_t byte;
    uint32_t byte_count = 0;
    bool logged_first_byte = false;
    int64_t last_report_us = esp_timer_get_time();

    while (s_active.load(std::memory_order_relaxed)) {
        int n = uart_read_bytes((uart_port_t)TWATCH_GPS_UART_NUM, &byte, 1, pdMS_TO_TICKS(200));
        if (n < 0) {
            ESP_LOGE(TAG, "uart_read_bytes error: %d", n);
            continue;
        }
        if (n == 0) {
            int64_t now = esp_timer_get_time();
            if (now - last_report_us > 3000000) {
                ESP_LOGI(TAG, "%lu bytes received so far", (unsigned long)byte_count);
                last_report_us = now;
            }
            continue;
        }
        byte_count++;
        if (!logged_first_byte) {
            logged_first_byte = true;
            ESP_LOGI(TAG, "first byte received: 0x%02x ('%c')", byte, (byte >= 32 && byte < 127) ? (char)byte : '?');
        }
        if (byte == '\n') {
            line[line_len] = 0;
            parse_nmea_line(line);
            line_len = 0;
        } else if (byte != '\r') {
            if (line_len < (int)sizeof(line) - 1) {
                line[line_len++] = (char)byte;
            } else {
                line_len = 0;   // overflow guard: drop the malformed line
            }
        }
    }
    vTaskDelete(nullptr);
}

esp_err_t gps_init(void)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << TWATCH_PIN_GPS_WAKEUP;
    io.mode = GPIO_MODE_OUTPUT;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level((gpio_num_t)TWATCH_PIN_GPS_WAKEUP, 0);

    err = uart_driver_install((uart_port_t)TWATCH_GPS_UART_NUM, 1024, 0, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    uart_config_t cfg = {};
    cfg.baud_rate = TWATCH_GPS_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    err = uart_param_config((uart_port_t)TWATCH_GPS_UART_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    return uart_set_pin((uart_port_t)TWATCH_GPS_UART_NUM, UART_PIN_NO_CHANGE,
                         TWATCH_PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

void gps_acquire(void)
{
    if (s_refcount.fetch_add(1, std::memory_order_relaxed) == 0) {
        portENTER_CRITICAL(&s_lock);
        s_reading = GpsReading{};   // don't show a stale fix from last time
        portEXIT_CRITICAL(&s_lock);

        power_gps_power(true);
        gpio_set_level((gpio_num_t)TWATCH_PIN_GPS_WAKEUP, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        s_active.store(true, std::memory_order_relaxed);
        xTaskCreate(gps_task, "gps_uart", 4096, nullptr, 4, nullptr);
        ESP_LOGI(TAG, "acquired — LDO4 on, GPS task started");
    }
}

void gps_release(void)
{
    if (s_refcount.fetch_sub(1, std::memory_order_relaxed) == 1) {
        s_active.store(false, std::memory_order_relaxed);
        gpio_set_level((gpio_num_t)TWATCH_PIN_GPS_WAKEUP, 0);
        power_gps_power(false);
        ESP_LOGI(TAG, "released — LDO4 off");
    }
}

GpsReading gps_read(void)
{
    GpsReading copy;
    portENTER_CRITICAL(&s_lock);
    copy = s_reading;
    portEXIT_CRITICAL(&s_lock);
    return copy;
}
