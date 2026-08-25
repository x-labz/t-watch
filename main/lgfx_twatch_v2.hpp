#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "twatch_v2_pins.h"

// Touch is NOT wired up via LovyanGFX's Touch_FT5x06 here — see main/touch.h.
// Its _check_init() requires a CIPHER-register handshake that fails on this
// unit's FT6336, permanently locking out all reads (confirmed dead touch
// across every LovyanGFX config tried). LilyGO's own vendor driver never
// performs that handshake and works fine, so we read the chip directly.
class LGFX_TWatch2020V2 : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_TWatch2020V2()
    {
        {
            auto c = _bus.config();
            c.spi_host = VSPI_HOST;
            c.spi_mode = 0;
            c.freq_write = 80000000;   // 30+ FPS requires 80 MHz (CLAUDE.md section 8)
            c.pin_sclk = TWATCH_PIN_LCD_SCLK;
            c.pin_mosi = TWATCH_PIN_LCD_MOSI;
            c.pin_miso = -1;
            c.pin_dc = TWATCH_PIN_LCD_DC;
            c.dma_channel = SPI_DMA_CH_AUTO;
            _bus.config(c);
            _panel.setBus(&_bus);
        }
        {
            auto c = _panel.config();
            c.pin_cs = TWATCH_PIN_LCD_CS;
            c.pin_rst = -1;
            c.panel_width = 240;
            c.panel_height = 240;
            c.invert = true;
            c.rgb_order = false;
            _panel.config(c);
        }
        {
            auto c = _light.config();
            c.pin_bl = TWATCH_PIN_LCD_BL;
            c.pwm_channel = 7;
            _light.config(c);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};
