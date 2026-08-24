#pragma once

// LilyGO T-Watch 2020 V2 hardware map — see CLAUDE.md section 2.
// All GPIO/addr constants for this board live here; never inline magic numbers.

// --- Display (ST7789V, 240x240, SPI) ---
#define TWATCH_PIN_LCD_MOSI      19
#define TWATCH_PIN_LCD_SCLK      18
#define TWATCH_PIN_LCD_DC        27
#define TWATCH_PIN_LCD_CS        5
#define TWATCH_PIN_LCD_BL        25   // backlight, PWM-capable; power via AXP202 LDO2
// No LCD_RST pin — reset only via power-cycling LDO3. No MISO — panel has no readback.

// --- I2C bus 0: sensors (AXP202, BMA423, PCF8563, DRV2605) ---
#define TWATCH_I2C0_PORT         0
#define TWATCH_PIN_I2C0_SDA      21
#define TWATCH_PIN_I2C0_SCL      22
#define TWATCH_I2C0_FREQ_HZ      400000

#define TWATCH_ADDR_AXP202       0x35
#define TWATCH_ADDR_BMA423       0x19
#define TWATCH_ADDR_PCF8563      0x51
#define TWATCH_ADDR_DRV2605      0x5A

// --- I2C bus 1: touch only (FT6336) ---
#define TWATCH_I2C1_PORT         1
#define TWATCH_PIN_I2C1_SDA      23
#define TWATCH_PIN_I2C1_SCL      32
#define TWATCH_I2C1_FREQ_HZ      400000
#define TWATCH_ADDR_FT6336       0x38
// Touch reset is not a GPIO — it is the AXP202 EXTEN output.

// --- Interrupt GPIOs (inputs) ---
#define TWATCH_PIN_IRQ_BMA423    39
#define TWATCH_PIN_IRQ_FT6336    38
#define TWATCH_PIN_IRQ_PCF8563   37
#define TWATCH_PIN_IRQ_AXP202    35   // also the PEK (power key) IRQ line

// --- GPS: Quectel L76K on UART ---
#define TWATCH_PIN_GPS_RX        26   // GPS TX -> ESP RX
#define TWATCH_PIN_GPS_TX        36   // GPS RX <- ESP TX
#define TWATCH_PIN_GPS_WAKEUP    33
#define TWATCH_PIN_GPS_1PPS      34
#define TWATCH_GPS_BAUD          9600

// --- Misc ---
#define TWATCH_PIN_IR_LED        2    // transmit-only, drive via RMT

// --- AXP202 rail voltages used at init (see CLAUDE.md section 3) ---
#define TWATCH_LDO2_BACKLIGHT_MV 3300
#define TWATCH_LDO3_PANEL_MV     3300
