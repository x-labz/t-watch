#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "viewmodels.h"

// Pure render functions: draw ONLY into the passed sprite. No hardware
// includes, no services, no locks, no blocking. Must compile unchanged in
// the PC simulator build (CLAUDE.md section 6 / section 10).
void render_watchface(LGFX_Sprite &fb, const WatchfaceVM &vm);
void render_battery(LGFX_Sprite &fb, const BatteryVM &vm);
void render_gps(LGFX_Sprite &fb, const GpsVM &vm);
void render_tilt(LGFX_Sprite &fb, const TiltVM &vm);
void render_haptic(LGFX_Sprite &fb, const HapticVM &vm);
void render_settings(LGFX_Sprite &fb, const SettingsVM &vm);
void render_wifi(LGFX_Sprite &fb, const WifiVM &vm);
