#pragma once

#include <cstdint>
#include "esp_err.h"

struct GpsReading {
    bool has_fix = false;
    uint8_t fix_quality = 0;      // NMEA GGA: 0=invalid, 1=GPS, 2=DGPS, ...
    uint8_t satellites_used = 0;
    float hdop = 0;                // horizontal dilution of precision (lower = better)
    float altitude_m = 0;
    float speed_kmh = 0;
    float course_deg = 0;
    double latitude = 0;
    double longitude = 0;
    uint8_t utc_hh = 0, utc_mm = 0, utc_ss = 0;
    bool has_date_time = false;       // $RMC status=='A' and a date was parsed
    uint8_t date_day = 0, date_month = 0, date_year = 0;  // date_year is
                                      // years since 2000 (NMEA's 2-digit yy)
    uint8_t satellites_in_view = 0;   // from the most recent $GSV sentence
    uint32_t sentence_count = 0;      // every NMEA line seen, any type — proof
                                      // the UART link + module are alive even
                                      // before a fix (or before any sentence
                                      // we specifically parse)
};

// Installs the GPS UART, RX-only (GPIO36 is an input-only pin on classic
// ESP32, so ESP->GPS TX was never actually wired for output — we only ever
// read NMEA sentences, never send config commands). Does not power on the
// GPS module; call gps_acquire() for that.
esp_err_t gps_init(void);

// Refcounted power-on: 0->1 enables AXP202 LDO4 and starts the NMEA parse
// task; the matching gps_release() powers back down on 1->0. GPS is the
// biggest power draw on this board (CLAUDE.md section 9) — never leave it
// acquired when no view needs a fix.
void gps_acquire(void);
void gps_release(void);

// Latest parsed reading. Zero/no-fix until acquired and a sentence with a
// fix has been received (cold start can take 30s+ outdoors).
GpsReading gps_read(void);
