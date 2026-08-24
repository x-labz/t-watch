#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "twatch_v2_pins.h"

class LGFX_TWatch2020V2 : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
    lgfx::Touch_FT5x06 _touch;   // FT6336 is FT6x36-family: this class covers it

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
        {
            auto c = _touch.config();
            c.i2c_port = TWATCH_I2C1_PORT;
            c.pin_sda = TWATCH_PIN_I2C1_SDA;
            c.pin_scl = TWATCH_PIN_I2C1_SCL;
            // Polling mode (not the IRQ pin): LovyanGFX's FT5x06 driver gates
            // reads behind pin_int's raw level, and disabling that gate made
            // no observed difference on this unit either way — but polling
            // matches our ~50Hz software loop in ui_task.cpp and removes one
            // more variable, so it's the safer default going forward.
            c.pin_int = -1;
            c.i2c_addr = TWATCH_ADDR_FT6336;
            c.freq = TWATCH_I2C1_FREQ_HZ;
            c.x_min = 0;
            c.x_max = 239;
            c.y_min = 0;
            c.y_max = 239;
            _touch.config(c);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};
