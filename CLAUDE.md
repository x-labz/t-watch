# CLAUDE.md — LilyGO T-Watch 2020 V2 firmware (ESP-IDF + LovyanGFX, LVGL optional)

This file is the ground truth for this project. Read it fully before touching code.
The #1 class of bugs on this board is **invisible power problems** (PMU not configured),
not SPI/driver bugs. When the screen is black or touch is dead, suspect the AXP202 first.
The #2 class is **performance**: see the 30+ FPS CHECKLIST (section 8) — it is mandatory
for any rendering code. The #3 class is **battery drain**: section 9 (POWER CONSERVATION)
is mandatory for every feature — peripherals default OFF, CPU frequency scales via
locks, never pinned high.

---

## 1. Project goal

Bare ESP-IDF (5.x, latest stable) firmware for the **LilyGO T-Watch 2020 V2** smartwatch.

- Display driver: **LovyanGFX** (NOT TFT_eSPI, NOT esp_lcd)
- UI: **PATH A (default): pure LovyanGFX** — views + focus events + services (section 6)
- UI: **PATH B (optional): LVGL 9.x** — only if scrollable lists / complex menus are
  requested (section 7). Never mix LVGL 7/8 example code — APIs incompatible.
- PMU driver: **XPowersLib** (AXP202)
- Sensors/RTC/haptics: **SensorLib** (lewisxhe)
- The legacy Arduino `TTGO_TWatch_Library` is **reference documentation only**
  (pin maps, init sequences). Never compile it or port its Arduino code.

Language: C++ (the driver libs are C++). `main/main.cpp` must expose
`extern "C" void app_main(void)`.

All rendering code must be hardware-agnostic (draws into a sprite, takes state as
function arguments) so the same code compiles for the watch AND the PC simulator
(section 10). UI code never includes drivers, I2C, or XPowersLib headers.

---

## 2. Hardware map (T-Watch 2020 V2 — verify it's V2, not V1/V3!)

SoC: ESP32-D0WDQ6 (classic ESP32, dual core, 240 MHz, **no USB-JTAG**).

### Display — ST7789V, 240x240, 1.54", SPI
| Signal   | GPIO |
|----------|------|
| MOSI     | 19   |
| SCLK     | 18   |
| DC       | 27   |
| CS       | 5    |
| Backlight| 25 (PWM capable; power comes from AXP202 LDO2) |
| RST      | **none** (reset only via power-cycling LDO3) |
| MISO     | none |

Panel needs `invert = true`. Target SPI clock: **80 MHz** (see section 8); fall back
to 40 MHz only if 80 shows visual corruption on your unit.

**The panel is mounted upside down** relative to the ST7789's native scan order,
so it needs `offset_rotation = 2` (180°) — confirmed on hardware 2026-08-25.
Set it in the *panel config*, not via `setRotation()` after init, so the strip
pipeline (which pushes straight to the panel, section 8) stays consistent with
sprite-based draws.

**Touch coordinates are mapped to screen space in `touch.cpp`, once.** Two
corrections compose: this unit reports X mirrored relative to the panel, and
the 180° rotation flips both axes — which cancels on X and leaves Y flipped, so
`x = raw_x`, `y = 239 - raw_y`. `touch_read()` therefore returns true screen
coordinates and UI code does no correction of its own. Keep it that way: the
mirror used to be applied ad-hoc at each call site, which is easy to get
inconsistent between hit-testing and swipe direction.

### I2C bus 0 — "sensor bus" (SDA=21, SCL=22, 400 kHz)
| Device  | Addr | Role |
|---------|------|------|
| AXP202  | 0x35 | PMU (power to everything) |
| BMA423  | 0x19 | Accelerometer / step counter |
| PCF8563 | 0x51 | RTC |
| DRV2605 | 0x5A | Haptic driver (vibration effects) |

### I2C bus 1 — touch only (SDA=23, SCL=32)
| Device  | Addr | Role |
|---------|------|------|
| FT6336  | 0x38 | Capacitive touch (FT6x36 family) |

Touch **reset** line is NOT a GPIO — it is the AXP202 **EXTEN** output.
Touch reading: use LovyanGFX `Touch_FT5x06` (covers FT6x36) so `lcd.getTouch()` works.

### Interrupt GPIOs (all inputs)
| Source  | GPIO |
|---------|------|
| BMA423  | 39   |
| FT6336  | 38   |
| PCF8563 | 37   |
| AXP202  | 35   | (side button = AXP202 PEK, reported via this IRQ)

### GPS — Quectel L76K on UART
| Signal      | GPIO |
|-------------|------|
| GPS TX → ESP RX | 36 |
| GPS RX ← ESP TX | 26 |
| WAKEUP (drive high to wake) | 33 |
| 1PPS        | 34 |
Power: AXP202 **LDO4**. 9600 baud default, NMEA.

**Confirmed on hardware 2026-08-25:** the two GPIOs above were previously listed swapped in
this file (26/36 reversed) — no NMEA data was ever received until corrected. Verified against
LilyGO's own vendor board header (`src/board/twatch2020_v2.h`: `GPS_RX=36, GPS_TX=26`). This
also makes physical sense: GPIO36 is input-only on classic ESP32, so it has to be the ESP's RX
pin (listening to the GPS module's TX), not the other way around.

### Misc
- IR LED transmitter: GPIO 2 (use RMT peripheral)
- Vibration motor: via DRV2605 (I2C), not a GPIO
- No user GPIO buttons — the only button is the AXP202 power key (PEK)

### AXP202 power domains (memorize this)
| Channel | Powers | Rule |
|---------|--------|------|
| DC3     | ESP32 itself | never touch |
| LDO1    | RTC backup | not controllable |
| **LDO2**| **Backlight** | enable + 3.3 V before expecting light |
| **LDO3**| **TFT panel + touch controller** | enable + 3.3 V BEFORE lcd.init() |
| LDO4    | GPS module | enable only when GPS needed (power saving) |
| EXTEN   | Touch reset | toggle off→delay→on to reset FT6336 |
| DC2     | unused | keep off |

### Flash / PSRAM — CONFLICTING DOCS, VERIFY ON HARDWARE
LilyGO's docs contradict themselves (16MB flash/4MB PSRAM vs 4MB flash/16MB PSRAM).
Before setting sdkconfig flash size / partition table, run:
```bash
esptool.py --port /dev/ttyACM0 flash_id
```
and set `CONFIG_ESPTOOLPY_FLASHSIZE_*` + partition CSV to match reality.
PSRAM: quad mode. Note classic ESP32 maps at most 4 MB of PSRAM into address space,
and **DMA cannot read from PSRAM** — this drives the sprite placement rules in section 8.

**Confirmed on the primary dev unit (2026-08-24):** 16MB flash (Winbond `ef`/`6018`)
and an **8MB physical PSRAM chip** — only 4MB of it gets mapped into address space
(boot log: `Found 8MB PSRAM device` then `Virtual address not enough for PSRAM,
map as much as we can. 4MB is mapped`), which is the classic-ESP32 cap noted above,
not a property of the chip. Don't assume 16MB flash implies a 4MB PSRAM chip —
verify per-unit, since LilyGO's docs are inconsistent across batches.

### COMPLETE PERIPHERAL FEATURE INVENTORY (what each chip can actually do)
Use this when deciding how to implement a feature — the hardware often already does it
(step counting, wake gestures, haptic patterns, fuel gauging) and hardware beats
software for power (section 9).

**ESP32-D0WDQ6 (SoC)**
- 2× Xtensa LX6 @ 240 MHz, 520 KB SRAM (+ external PSRAM), ULP coprocessor
  (can run simple sensor polling during deep sleep)
- WiFi 802.11 b/g/n 2.4 GHz (STA/AP), Bluetooth 4.2: BR/EDR classic + BLE
- RTC domain: 8 KB slow + 8 KB fast RTC RAM (survives deep sleep — stash state here),
  ext0/ext1 GPIO wake, timer wake
- RMT peripheral (drives the IR LED), LEDC PWM (backlight), 3 UARTs (1 = console,
  1 = GPS, 1 spare), HW crypto (AES/SHA/RSA), esp_random TRNG
- NOT available on this board: most ADC/touch-capable pins are consumed by the
  wiring above; treat GPIO list in this file as exhaustive

**AXP202 (PMU) — more than a power switch**
- Rail control: 2 buck (DC2/DC3) + 4 LDO + EXTEN (table above)
- Li-ion charger: USB-powered, configurable charge current/target voltage —
  battery is ~380 mAh, keep charge current ≤ 300 mA; charging status + IRQs
  (charge start/done, VBUS insert/remove)
- Fuel gauge: coulomb counter + battery %, ADCs for battery V (~1.1 mV res),
  charge/discharge current (~0.5 mA res), VBUS V/I, internal temperature.
  ADC channels must be explicitly ENABLED at init or all reads return 0.
  Discharge current reads ~0 while USB is plugged (system runs from VBUS).
  Tip: if charging refuses to start, check the battery-temp (TS) sensing config.
- PEK button: short/long-press IRQs (GPIO35), 6 s force-poweroff in hardware
- All measurements are battery-total — per-rail attribution only by toggle-and-diff

**BMA423 (accelerometer)** — driven DIRECTLY, not via SensorLib.
`SensorBMA423`'s `bma423_init()` fails on this board with `BMA4_E_COM_FAIL`
(-2) every time, at boot and on demand, while raw reads of the same registers
on the same bus succeed (chip id `0x00` reads `0x13`). The fault is in the
library's plumbing, not the chip, so `main/tilt.cpp` talks to it over raw I2C —
the same call already made for the DRV2605 in `haptic.cpp`.
That path configures the accelerometer only: `0x7C`=0 (power save off),
`0x40`=0xA8 (100 Hz), `0x41`=0 (±2g), `0x7D`=0x04 (accel on), then reads 6
bytes from `0x12`. Data is 12-bit signed left-aligned in each 16-bit LE pair,
so `(int16_t)(msb<<8|lsb)/16` sign-extends it; ±2g means 1g = 1024 counts.
Sanity check: the vector magnitude must be ~1g at rest.
**The features below still need SensorLib's ~6 KB config-file upload**, which
is exactly what fails — so they are NOT available today. Revisit that upload
before planning anything that depends on them:

**BMA423 hardware features — do NOT reimplement these in software (once available)**
- 3-axis accel, µA-level operation (that's why it's the always-on wake source)
- Hardware step counter (pedometer with on-chip algorithm)
- Wrist-tilt / raise-to-wake detection, single & double tap detection
- Activity classification (still / walking / running), any-motion & no-motion
- All of the above as interrupts on GPIO39 — screen wake costs ~zero CPU

**PCF8563 (RTC)** — **this is the watch's clock; GPS is not**
- Battery-backed date/time (survives deep sleep and poweroff via AXP LDO1)
- `isClockIntegrityGuaranteed()` (the VL flag) is the honest "is this time
  trustworthy?" test — it latches false only if the oscillator ever stopped.
  `main/rtc.cpp` restores the system clock from it at boot, so a normal boot
  needs **no GPS at all**; GPS sets the clock only when this returns false
  (first-ever boot / drained backup rail), and any GPS sync is written back
- Alarm (min/hour/day/weekday) + countdown timer, both → IRQ on GPIO37
  (= deep-sleep wake source for scheduled events, e.g. hourly sensor sync)
- CLKOUT output exists but is unused on this board
- Sync system time from PCF8563 at every boot/wake; write back after NTP/GPS sync

**FT6336 (touch)**
- Capacitive, up to 2 simultaneous points (multi-touch), IRQ on GPIO38
- On-chip gesture registers exist but are unreliable — do gesture detection in
  software from point deltas (section 6)
- **Power modes (reg 0xA5) — the #1 cause of "touch hardware is dead":**
  `0 = ACTIVE (~4mA)`, `1 = MONITOR (~3mA)`, `3 = DEEPSLEEP (~100µA)`.
  **In DEEPSLEEP the chip does not ACK on I2C at all** — an address scan of
  bus 1 comes back completely empty and reads fail with `ESP_ERR_INVALID_STATE`,
  which looks exactly like a failed/disconnected controller. Per LilyGO's own
  driver, the only documented way out is pulling the reset line (AXP202 EXTEN)
  low. Never diagnose "the FT6336 is dead" without first resetting it and
  explicitly writing ACTIVE to 0xA5 — see section 13.
- **Its reset line is AXP202 EXTEN, and EXTEN low = chip completely silent on
  I2C.** That is the first thing to check when touch "dies" — far more likely
  than DEEPSLEEP and identical in symptoms. Beware the XPowersLib bug that
  makes EXTEN unreachable (section 3).
- **Do NOT "helpfully" configure the rest of the chip.** An EXTEN reset already
  leaves it in a working default state (`0x00`=0 working mode, `0x80`=0x1C
  threshold, `0x86`=1, `0x87`=0x1E, `0x89`=0x28 on this unit). Writing the
  INT-mode (`0xA4`) and monitor-timing (`0x87`) registers at init — which
  LilyGO's driver does for its own interrupt-driven design — produced a chip
  that answered I2C perfectly, reported valid IDs and mode, and **silently
  reported zero touch points forever** (2026-08-25). We poll; we do not need
  them. `touch_init()` therefore forces only the power mode.
- Sanity values on this unit: `0xA3` (chip id) = `0x64` (FT6236U), `0xA8`
  (vendor id) = `0x11`, `0xA1/0xA2` (fw version) = `0x30 0x0A`. `0xA9` reads
  `0x0F` even when everything is healthy — do not read that as an error.

**DRV2605 (haptics)**
- 123 pre-programmed ROM effects (clicks, ticks, buzzes, ramps, alerts)
- 8-slot sequencer: queue effect chains (tick–pause–buzz) in one I2C transaction
- Real-time playback (RTP) mode for custom continuous vibration levels
- Overdrive + active braking = crisp phone-like taps; standby mode between effects

**ST7789V (display controller)**
- 240×240 IPS, driven as RGB565; needs invert=true on this panel
- Partial-window addressing (dirty-rect pushes — section 8), HW scrolling registers
- Sleep-in/sleep-out commands (`lcd.sleep()`), low-power idle mode (8-color)
- NO readback (MISO absent) and NO reset pin (power-cycle LDO3 instead)

**Quectel L76K (GNSS)**
- Multi-constellation: GPS + GLONASS + BDS; NMEA 0183 over UART @ 9600 baud
- 1PPS timing pulse on GPIO34 (precise second edges — great for clock discipline)
- Standby via WAKEUP pin (GPIO33) for short pauses; LDO4 off for real off
- Low-power periodic/standby modes configurable via serial commands
- Cold-start fix takes ~30 s+ outdoors; keep LDO4 on until fix, then decide

**BLE scanning — what you can actually expect to see**
- Scan **ACTIVE** (`params.passive = 0`), not passive: most devices advertise
  only service UUIDs and put their name in the SCAN_RSP, which is sent only in
  reply to a SCAN_REQ. A passive scan therefore lists nearly everything as a
  bare MAC.
- Even then, **most nearby devices legitimately have no name.** Phones and
  wearables use address privacy (random addresses — a first octet whose top
  bits are not `11`) and deliberately omit their name to resist tracking. This
  is correct BLE behaviour, not a bug in the scanner.
- They usually still send manufacturer data, so `blescan.cpp` falls back to the
  Bluetooth SIG company ID (`~Apple`, `~Samsung`, …). The leading `~` marks an
  inferred label, never a name the device actually advertised; a real name from
  a later SCAN_RSP always supersedes it.

**IR emitter (GPIO2)**
- Transmit-only (no receiver): NEC/RC5/raw remote-control codes via RMT carrier

**Battery / charging**
- ~380 mAh LiPo, charges from the same USB port used for flashing (via AXP202)

**Explicitly ABSENT on the V2 (do not plan features around these)**
- No speaker, no microphone, no I2S audio at all (V1/V3 have audio, V2 traded it for GPS)
- No SD card, no camera, no gyroscope, no magnetometer/compass, no barometer,
  no heart-rate sensor, no Qi charging
- No user GPIO buttons (only the AXP202 PEK) and no exposed spare GPIOs

---

## 3. THE CRITICAL INIT ORDER (source of all "black screen" bugs)

Nothing on this board works until the AXP202 is programmed. LovyanGFX init before
PMU init = black screen + dead touch, with zero error messages.

```
1. i2c bus 0 init (SDA 21, SCL 22)
2. XPowersLib begin (XPOWERS_CHIP_AXP202, addr 0x35)
3. LDO3 = 3300 mV, enable          // panel + touch power
4. LDO2 = 3300 mV, enable          // backlight power
5. EXTEN off → vTaskDelay(20ms) → EXTEN on   // touch reset
6. vTaskDelay(~150 ms)             // panel power-up time
7. lcd.init()                      // LovyanGFX — only NOW
8. everything else (BMA423, PCF8563, DRV2605, ...)
```

If the display ever wedges: there is no RST pin — disable LDO3, delay 100 ms,
re-enable, re-run lcd.init().

### ⚠️ XPowersLib BUG: EXTEN and LDO3 collide — do not use enableExternalPin()
`XPowersAXP202.hpp` maps **both** `enableLDO3()/disableLDO3()` **and**
`enableExternalPin()/disableExternalPin()` to **REG12 bit 6**. It is a
copy-paste error in the library. On the AXP202, bit 6 is LDO3 and **EXTEN is
bit 0**.

This matters more here than on most boards, because EXTEN is this board's only
touch-reset line (section 2). The consequences, all confirmed on hardware
2026-08-25:
- `disableExternalPin()` **switches the panel+touch rail off**, not the reset
  line. Our "EXTEN reset" was a 20 ms LDO3 power cut that never reset anything.
- EXTEN could therefore sit low with **no way for our firmware to raise it**,
  holding the FT6336 in reset. It stops answering I2C entirely — a bus scan
  finds zero devices — which is indistinguishable from dead hardware.
- Only LilyGO's firmware recovered it, because it writes the bit correctly.
  That made the bug look like it lived in our code when it lived in the library.
- `isEnableExternalPin()` lies the same way: it reports LDO3's state.

`power.cpp` therefore drives EXTEN with a raw read-modify-write of REG12 bit 0
(`power_touch_reset()`, `power_set_exten()`, `power_exten_is_on()`) and never
calls the library's EXTEN accessors. **Do not "simplify" that back to
XPowersLib.** Verify with `touch`: it prints the real EXTEN bit.

### NEVER INHERIT DEVICE STATE — always set it explicitly (learned the hard way)
Flashing does **not** reset the peripherals. LDO3 stays powered right through a
reflash, so the panel and FT6336 never cold-boot; AXP202 registers are
battery-backed and survive resets, reflashes, and even a battery pull while USB
is attached. **Anything you do not explicitly program, you inherit from whatever
firmware ran last** — including LilyGO's vendor firmware.

This produced a genuinely nasty bug on 2026-08-25: the FT6336 was sitting in
DEEPSLEEP (section 2), ignoring I2C completely. It presented as dead touch
hardware that survived reboots and a battery pull, while vendor firmware
"fixed" it — because vendor firmware programs the chip explicitly and we did
not. Hours went into chasing a hardware fault that did not exist.

Rules that follow:
- `power_init()` sets **LDO3 mode** explicitly (`XPOWERS_AXP202_LDO3_MODE_LDO`),
  not just voltage/enable. Inheriting DCIN mode drives this rail to ~5.08 V.
- `touch_init()` resets via EXTEN and writes power mode ACTIVE rather than
  assuming the controller is awake.
- When a peripheral looks dead, ask "what state could it be stuck in?" before
  "is it broken?" — and reproduce the suspected state deliberately to confirm
  (there is a `touchsleep` console command for exactly this).

---

## 4. Repo layout & dependencies

```
.
├── CLAUDE.md                  ← this file
├── CMakeLists.txt             ← standard IDF top-level
├── sdkconfig.defaults
├── partitions.csv
├── components/
│   ├── LovyanGFX/             ← git submodule: https://github.com/lovyan03/LovyanGFX (master)
│   └── XPowersLib/            ← git submodule: https://github.com/lewisxhe/XPowersLib (master)
├── main/
│   ├── CMakeLists.txt         ← idf_component_register(... REQUIRES LovyanGFX XPowersLib lewisxhe__sensorlib driver esp_timer esp_pm nvs_flash esp_wifi esp_netif esp_event)
│   ├── idf_component.yml      ← managed deps (SensorLib; + lvgl ONLY if PATH B)
│   ├── main.cpp               ← ESP-IDF entry: PMU, lcd, tasks     (watch only)
│   ├── twatch_v2_pins.h       ← all constants from section 2
│   ├── lgfx_twatch_v2.hpp     ← LGFX_Device subclass (section 5)   (watch only)
│   ├── power.cpp/.h           ← AXP202 wrapper implementing section 3 (watch only)
│   ├── ui_task.cpp/.h         ← the one UI/compositor task: input, nav, redraw
│   ├── debug_console.cpp/.h   ← serial command console (section 11, watch only)
│   ├── services/              ← refcounted peripheral owners (section 6, watch only)
│   │   ├── gps_service.cpp/.h        ├── power_service.cpp/.h
│   │   ├── motion_service.cpp/.h     ├── time_service.cpp/.h
│   │   └── haptics_service.cpp/.h    └── storage_service.cpp/.h
│   └── ui/                    ← SHARED, hardware-free: VMs + render + view grid + pure logic
│       ├── view.h             ← ViewId, ViewDef, ViewEvent, RenderFn
│       ├── view_grid.cpp      ← the navigation grid table (authoritative)
│       ├── viewmodels.h       ← WatchfaceVM, GpsVM, StepsVM, ...
│       ├── render_watchface.cpp / render_gps.cpp / ...
│       └── logic_*.cpp        ← pure compute_* functions (sim-testable)

**As built (2026-08-25):** peripheral owners are currently flat modules in `main/`
(`gps`, `tilt`, `haptic`, `touch`, `settings`, `wifiscan`, `time_sync`) rather than a
`services/` directory with the refcount base class — `gps` is the only one that
actually refcounts so far. `main/ui/` holds `view.h` + one `render_*.cpp` per view;
`view_grid.cpp` and `logic_*.cpp` do not exist yet (nav order lives in `ViewId`).
The sim (`sim/`) has not been built yet either — visual checks are tier-2/3 of
section 12 for now.
├── sim/                       ← PC simulator (section 10)
│   ├── CMakeLists.txt         ← desktop build: SDL2 + LovyanGFX + ../main/ui/
│   └── main.cpp               ← LGFX_SDL device, fake services→VMs, --shot mode
└── tools/
    └── decode_snapshot.py     ← serial framebuffer blob → PNG
```

### main/idf_component.yml
```yaml
dependencies:
  idf: ">=5.0"
  lewisxhe/sensorlib: "*"     # PCF8563, BMA423, DRV2605
  # lvgl/lvgl: "^9"           # PATH B ONLY. Uncomment only when LVGL is requested.
```
XPowersLib: git submodule (upstream, esp-idf CI-tested).
Define `XPOWERS_CHIP_AXP202` before including `XPowersLib.h`.

### Submodule setup (first-time)
```bash
git submodule add https://github.com/lovyan03/LovyanGFX components/LovyanGFX
git submodule add https://github.com/lewisxhe/XPowersLib components/XPowersLib
git submodule update --init --recursive
```

---

## 5. Dev environment + LovyanGFX device config

```bash
# 1. ESP-IDF (latest stable v5.x)
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout <latest v5.x release tag> && git submodule update --init --recursive
./install.sh esp32

# 2. In EVERY shell used for this project (also the shell running `claude`):
. ~/esp/esp-idf/export.sh

# 3. Serial permission (Linux, once, then re-login):
sudo usermod -aG dialout $USER

# 4. Project target
idf.py set-target esp32
```

Watch enumerates as CH340 → `/dev/ttyUSB0` (kernel driver built-in) on most units.
**Confirmed on the primary dev unit:** its adapter is a CH9102 variant (`lsusb` shows
`1a86:55d4` "USB Single Serial") that identifies as CDC-ACM, so it's claimed by the
kernel's built-in `cdc_acm` driver and shows up as **`/dev/ttyACM0`** instead — check
`ls /dev/ttyACM* /dev/ttyUSB*` if a `-p` port guess doesn't connect. The watch must
be POWERED ON (long-press side button). Dead battery → charge over USB first.

### sdkconfig.defaults (performance-tuned starting point — see section 8)
```
CONFIG_IDF_TARGET="esp32"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_QUAD=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y   # NOT 80M — see section 13; 80M boot-loops this unit
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y   # note: IDF downgrades this to DIO when PSRAM is on
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_COMPILER_CXX_EXCEPTIONS=n
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
# Set flash size AFTER running esptool.py flash_id — do not guess:
# CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y  (or 4MB)
```

### LovyanGFX config (known-good for V2) — main/lgfx_twatch_v2.hpp
```cpp
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_TWatch2020V2 : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
  lgfx::Touch_FT5x06 _touch;   // FT6336 is FT6x36-family: this class covers it
public:
  LGFX_TWatch2020V2() {
    { auto c = _bus.config();
      c.spi_host = VSPI_HOST; c.spi_mode = 0;
      c.freq_write = 80000000;            // 30+ FPS REQUIRES 80 MHz (section 8)
      c.pin_sclk = 18; c.pin_mosi = 19; c.pin_miso = -1; c.pin_dc = 27;
      c.dma_channel = SPI_DMA_CH_AUTO;
      _bus.config(c); _panel.setBus(&_bus); }
    { auto c = _panel.config();
      c.pin_cs = 5; c.pin_rst = -1;
      c.panel_width = 240; c.panel_height = 240;
      c.invert = true; c.rgb_order = false;
      _panel.config(c); }
    { auto c = _light.config();
      c.pin_bl = 25; c.pwm_channel = 7;
      _light.config(c); _panel.setLight(&_light); }
    { auto c = _touch.config();
      c.i2c_port = 1; c.pin_sda = 23; c.pin_scl = 32; c.pin_int = 38;
      c.i2c_addr = 0x38; c.freq = 400000;
      c.x_min = 0; c.x_max = 239; c.y_min = 0; c.y_max = 239;
      _touch.config(c); _panel.setTouch(&_touch); }
    setPanel(&_panel);
  }
};
```
If PWM dimming on GPIO25 misbehaves, control brightness via AXP202 LDO2 voltage
(2400–3300 mV) instead.

---

## 6. UI ARCHITECTURE (PATH A, DEFAULT): views, focus, services — pure LovyanGFX

No LVGL. The UI is: **views** (full-screen), navigated by swipes/PEK, rendered by
**pure render functions**, fed by **ViewModels**, updated by **view logic** (tasks
only where justified), on top of **refcounted services** that own all peripherals.

### LAW #1: tasks think, ONLY the UI task draws
LovyanGFX is not thread-safe and the strip pipeline (section 8) assumes one owner.
No task other than the UI/compositor task (pinned to core 1) may touch the panel,
sprites, or strips. Ever.

Data flow:
```
[view logic / service] --update--> ViewModel --DIRTY(view_id)--> [UI task queue]
    UI task: snapshot VM by value (short mutex) → pure render fn → strip pipeline
```

### ViewModels + render functions
- One small plain struct per view: `WatchfaceVM { hh,mm,ss, batt_pct, steps }`,
  `GpsVM { has_fix, sats, lat, lon }`, ... Defined in `main/ui/`.
- Render signature: `void render_gps(LGFX_Sprite& fb, const GpsVM& vm);`
  Renders draw ONLY into the passed sprite. No hardware includes, no services,
  no locks, no blocking — they must compile unchanged in the sim build.
- The UI task copies the VM **by value** under a per-VM mutex held microseconds;
  render works on the copy. Never render while holding any lock.
- Redraw is event-driven ONLY: a view changes its VM → posts DIRTY → one redraw.
  No free-running render loop except during transitions/animations.

### Views + navigation grid
```cpp
enum class ViewId : uint8_t { WATCHFACE, STEPS, GPS, NOTIF, SETTINGS, COUNT, NONE };

struct ViewDef {
  ViewId id;
  RenderFn render;                       // pure fn from ui/
  ViewId left, right, up, down;          // NONE = swipe ignored
  QueueHandle_t logic_q;                 // nullptr = passive view (no task)
  void (*on_event)(const ViewMsg&);      // passive views: handled in UI task
};
```
**As built (2026-08-25):** the grid above was the original sketch; the implementation
is currently a flat left-to-right strip navigated by horizontal swipes only, with
`ViewId` order in `main/ui/view.h` authoritative (no `view_grid.cpp` yet):
```
[WATCHFACE] ↔ [BATTERY] ↔ [GPS] ↔ [TILT] ↔ [HAPTIC] ↔ [SETTINGS] ↔ [WIFI] ↔ [BLE]
```
Ends clamp (no wraparound). Per-view tap behavior, where a view has any:
| View | Tap behavior | Notes |
|---|---|---|
| HAPTIC | left/right = prev/next DRV2605 ROM effect (1–123), plays it | |
| SETTINGS | left/right = dim/brighten backlight ~10% per tap | persisted to NVS (flash) |
| WIFI | anywhere = rescan | scan also fires on focus |
| BLE | anywhere = rescan | scan also fires on focus |
Views that own a peripheral acquire it on focus and release on blur — currently GPS
(LDO4) does this. WIFI and BLE instead scan one-shot and tear their radio fully down
each time, so there is nothing to release on blur.

- PEK short-press: global "back to WATCHFACE" from anywhere.
- Swipe transition: ~200 ms slide rendering BOTH views into the strips with offset.
  Acquire `ESP_PM_CPU_FREQ_MAX` lock at transition start, release at end (section 9).

### Input pipeline (owned by the UI task)
- FT6336 IRQ (GPIO38) wakes the UI task; poll `lcd.getTouch()` at ~50 Hz ONLY while
  a finger is down.
- Gesture classify on release: displacement > 40 px → SWIPE_{L,R,U,D}; else TAP(x,y).
- Swipes are consumed by the ViewManager (navigation). TAPs are forwarded to the
  focused view (its queue, or `on_event` for passive views).

### Focus is a lifecycle EVENT, never a polled flag
```cpp
enum class ViewEvent : uint8_t { FOCUS_GAINED, FOCUS_LOST, TAP, TICK_1HZ, TICK_1MIN };
```
View-with-task pattern — a single blocking loop, zero CPU when idle (DFS-friendly):
```cpp
void gps_view_task(void*) {
  for (;;) {
    ViewMsg m; xQueueReceive(q, &m, portMAX_DELAY);   // blocked = free
    switch (m.ev) {
      case FOCUS_GAINED: services.gps.acquire(); start_1hz(); break;
      case FOCUS_LOST:   services.gps.release(); stop_1hz();  break;
      case TICK_1HZ:     vm_update(services.gps.snapshot()); post_dirty(ViewId::GPS); break;
      case TAP:          /* view-local hit testing */ break;
    }
  }
}
```
Blurred views choose their own reduced duty (e.g. STEPS: 1/min blurred, 1 Hz focused).

### Tasks are OPT-IN (RAM rule)
Each task costs 2–4 KB internal-RAM stack permanently — the same RAM section 8 needs.
- A view gets a task ONLY for genuinely continuous/blocking work (GPS: yes).
- Simple views (WATCHFACE, SETTINGS) are **passive**: their logic is an `on_event`
  handler run inside the UI task. Same event model, zero stack cost.
- No C++20 coroutines — not idiomatic in ESP-IDF; the event-queue task IS the pattern.

### Services — refcounted peripheral owners (this is how section 9 gets enforced)
Views/logic NEVER touch AXP202, I2C devices, UART, or radios directly. Services do.
- `acquire()` / `release()` with refcount: 0→1 powers the peripheral up (e.g. GPS:
  LDO4 on, WAKEUP high, UART parser running), →0 powers it down. Multiple consumers
  (GPS view + background geotagger) safely share; last one out turns off the lights.
- Service list: `PowerService` (AXP202 rails, fuel gauge, PEK IRQs, charge events),
  `MotionService` (BMA423: steps, tilt/tap wake events), `TimeService` (PCF8563 ↔
  system clock sync both ways), `HapticsService` (effect QUEUE → one task owns
  DRV2605 I2C; callers just post effect ids), `GpsService` (LDO4 + UART + NMEA →
  snapshot struct), `StorageService` (NVS settings).
- **I2C bus 0 mutex is owned by the bus wrapper**, shared by PMU/BMA423/RTC/DRV2605
  services — the one truly dangerous shared resource. Everything else prefers
  message passing over locks.
- Services communicate upward by POSTING EVENTS (MotionService → WRIST_TILT → UI task
  → screen wake). Services never call into views.

### Where state lives — RTC memory vs PCF8563 vs NVS
Three persistence tiers, and picking the wrong one is a silent bug:
| | RTC memory (`RTC_DATA_ATTR`) | PCF8563 (RTC chip) | NVS (flash) |
|---|---|---|---|
| Survives | deep sleep, SW reset | everything (battery-backed via LDO1) | everything, incl. battery pull |
| Cost | free (8 KB slow domain) | an I2C write | flash wear, ~ms writes |
| Use for | **caches** cheap to re-derive | **wall-clock time**, and only that | **user settings** that must never be lost |
| In use | GPS-derived timezone offset (`time_sync.cpp`) — re-derivable from a fix | system time (`rtc.cpp`) | backlight brightness (`settings.cpp`) |
Rule: if losing it merely costs a re-derivation, RTC memory; if losing it would
surprise the user, NVS. Never put a user-visible setting in RTC memory only —
it comes back as the default after the battery dies.

### Sim-shareability rule
Interesting view logic goes into pure functions (`WatchfaceVM compute_watchface(...)`)
with the task/handler as a thin event pump. `main/ui/` (VMs, render fns, view grid,
pure logic) compiles in the sim; tasks/services are watch-only and get fake
counterparts in `sim/`.

### Full picture
```
core 0                          core 1
──────                          ──────
GpsService (UART task)          UI/compositor task
MotionService (IRQ→events)        ├─ input: FT6336 IRQ + gesture recognizer
PowerService (PEK, fuel gauge)    ├─ ViewManager (grid nav, transitions, focus events)
HapticsService (effect queue)     ├─ passive-view handlers, VM snapshots
view logic tasks (opt-in)         ├─ pure render fns (ui/)
        │                         └─ strip pipeline → pushImageDMA (section 8)
        └── ViewModels + DIRTY events ──────────▲
```

Rendering mechanics (unchanged from before):
- NEVER draw directly to the panel piecemeal (flicker/tearing): always sprite →
  single `pushSprite`/`pushImageDMA` blit, via the section-8 strip pipeline for
  animated content.
```cpp
frame.fillScreen(TFT_BLACK);
viewdefs[current].render(frame, vm_copy);
frame.pushSprite(0, 0);              // DMA blit — see section 8 for buffer placement
```

---

## 7. UI PATH B (OPTIONAL): LVGL 9

Only add when scrollable lists / complex menus / text input are actually requested.
Uncomment `lvgl/lvgl: "^9"` in idf_component.yml. Rules if enabled:

- Buffers: two partial buffers of 240 × 40 px, RGB565, in **internal DMA-capable RAM**
  (`heap_caps_malloc(..., MALLOC_CAP_DMA)`), NOT PSRAM.
- ST7789 wants big-endian RGB565: either `lv_draw_sw_rgb565_swap()` in the flush cb
  OR LovyanGFX `setSwapBytes(true)` — exactly ONE of them. 0 or 2 swaps = blue/orange.
- Flush cb: `lcd.pushImageDMA(area..., px_map)`; call `lv_display_flush_ready()` after.
- Tick: `esp_timer` periodic 1–5 ms calling `lv_tick_inc()`.
- One dedicated FreeRTOS task calls `lv_timer_handler()`; ALL lv_* calls from that task.
- Only LVGL 9 APIs (`lv_display_create`, `lv_display_set_buffers`, `lv_indev_create`).
  If code contains `lv_disp_drv_t` it's v8 code — reject it.
- Register touch through an LVGL indev read-cb wrapping `lcd.getTouch()`; in that case
  do NOT also do PATH-A hit testing.

---

## 8. PERFORMANCE — 30+ FPS CHECKLIST (MANDATORY FOR RENDER CODE)

**The math that rules everything (240×240, RGB565):**
- One full frame = 240 × 240 × 2 B = **115,200 B (~112.5 KB)**
- SPI @ 40 MHz ≈ 5 MB/s → **~23 ms per frame transfer → ~43 fps absolute ceiling**
  (leaves almost no time to draw → real-world ~20–25 fps)
- SPI @ 80 MHz ≈ 10 MB/s → **~11.5 ms per frame → ~86 fps ceiling** → comfortable 30–60 fps
- ⇒ **`freq_write = 80000000` is required for 30+ fps.** Only if a specific unit shows
  corruption at 80 MHz, drop to 40 and compensate with partial redraws.

**Non-negotiable settings (already in sdkconfig.defaults above):**
- CPU 240 MHz (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`)
- PSRAM and Flash at **40 MHz** (`CONFIG_SPIRAM_SPEED_40M`, `CONFIG_ESPTOOLPY_FLASHFREQ_40M`).
  80 MHz flash makes this unit boot-loop (section 13) and PSRAM cannot exceed the
  flash clock. This does NOT affect the display: `freq_write = 80000000` is a
  different peripheral bus and stays at 80 MHz, so the fps maths below is unchanged.
  PSRAM only holds the two full-frame sprites (occasional redraws); the 30+fps
  path is the internal-RAM strip pipeline
- Compiler optimization: `-O2` (`CONFIG_COMPILER_OPTIMIZATION_PERF=y`)

**Buffer placement — the classic-ESP32 trap:**
- **DMA cannot read from PSRAM.** A full-frame sprite (112.5 KB) does not fit internal
  RAM alongside WiFi/BT, so it lands in PSRAM → `pushSprite` silently falls back to
  CPU-fed/bounce-buffer transfer AND drawing into PSRAM is slower. Acceptable for a
  1 Hz clock face; NOT acceptable for 30+ fps animation.
- **For 30+ fps use the strip pipeline:** two 240×40 sprites (19.2 KB each) in internal
  DMA RAM; render strip N+1 while strip N transfers via DMA:
```cpp
static LGFX_Sprite strip[2];  // both: setColorDepth(16); createSprite(240,40) AFTER
                              // ensuring MALLOC_CAP_DMA (setPsram(false) on the sprite)
lcd.startWrite();                       // hold CS for the whole frame
for (int y = 0, i = 0; y < 240; y += 40, i ^= 1) {
  render_strip(strip[i], state, y);     // CPU draws strip i
  lcd.pushImageDMA(0, y, 240, 40, (uint16_t*)strip[i].getBuffer());
}                                        // DMA sends strip i while CPU draws i^1
lcd.endWrite();                          // implicit waitDMA
```
- `startWrite()/endWrite()` around every frame (batches CS/transactions — measurable win).
- Dirty-rect shortcut: when only a region changed (e.g. seconds digits), render + push
  only that region. Cheapest fps of all is pixels you don't send.

**Hot-path code rules:**
- No `float` in per-pixel/per-frame loops (ESP32 FPU is OK-ish, but int math is faster;
  use fixed-point for animation interpolation).
- Use LovyanGFX built-in fonts / pre-converted fonts in animations; runtime-decoded TTF
  and PNG decode are for static screens only. Pre-render static backgrounds into a
  PSRAM sprite ONCE, then `pushSprite` regions of it / copy into strips.
- Measure, don't guess: wrap frame with `esp_timer_get_time()` and ESP_LOGD a rolling
  fps counter behind a `DEBUG_FPS` flag. A change without a before/after number is
  not a performance change.
- UI task pinned to core 1; WiFi/BT/sensor tasks on core 0 (keeps render jitter down).

**PATH B (LVGL) note:** same physics — 80 MHz SPI + both LVGL buffers in internal DMA
RAM + `pushImageDMA`. Never point LVGL buffers at PSRAM.

---

## 9. POWER CONSERVATION (MANDATORY — battery is tiny, ~380 mAh)

Principle: **everything defaults OFF.** A peripheral is powered only while a feature
actively needs it, and the CPU runs at the lowest frequency the current workload
tolerates. Every new feature must state its power story (what it turns on, when it
turns it back off).

### Peripheral gating — who turns what off, and when
| Peripheral | Off mechanism | Off when |
|---|---|---|
| **GPS (biggest consumer)** | AXP202 **LDO4 off**; for short pauses hold WAKEUP (GPIO33) low = standby | any time a fix is not actively being acquired. **Notably NOT at boot** — the PCF8563 supplies the time, so the receiver is powered only while the GPS view is focused. Timezone derivation likewise happens only during such a session (`time_sync_feed_gps`), never on the boot path |
| Backlight | LDO2 off; dim first by lowering LDO2 voltage | screen timeout (default 10 s without touch) |
| Panel | `lcd.sleep()` (ST7789 sleep-in); for long idle also **LDO3 off** | after backlight off; LDO3 off ⇒ full section-3 re-init on wake. **Never cut LDO3 as a side effect of recovering something else** — it also feeds the panel, which has no reset pin, so the display stays corrupt until `lcd.init()` re-runs. An early version of the touch auto-recovery did exactly this every ~5 s |
| Touch | powered by LDO3 → dies with panel (that's fine: no screen = no touch) | with display |
| WiFi | `esp_wifi_stop()` + `esp_wifi_deinit()` | immediately after each sync/fetch — never leave STA idling. Implemented pattern (`main/wifiscan.cpp`): a background task does init → `WIFI_MODE_STA` → start → blocking scan → read records → stop → deinit, so the radio is up only for the ~2 s the scan takes. Results are a static snapshot; refreshing means running the whole cycle again |
| BT/BLE | controller disable + deinit | whenever no active BLE session. **NimBLE, BLE-only mode** (`CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_BTDM_CTRL_MODE_BLE_ONLY`) — no classic BR/EDR is ever needed here, and NimBLE costs far less RAM/flash than Bluedroid. Implemented pattern (`main/blescan.cpp`): `nimble_port_init()` → host task → bounded `ble_gap_disc()` window (4 s, ACTIVE — see below) → `nimble_port_stop()` + `nimble_port_deinit()`. The `esp_bt_controller_mem_release()`-at-boot trick NO LONGER APPLIES — the controller is used, just kept down between scans. Verified 2026-08-25: repeated init/deinit cycles do not leak (internal heap 180263 B after 1 cycle vs 180131 B after 3) |
| DRV2605 | standby bit via SensorLib | between effects, always |
| IR LED / RMT | RMT channel disable | after send |
| **BMA423** | **stays ON** (µA-range) — it is the wake source (tilt / double-tap) | never |
| PCF8563 | always on (backup domain) — timekeeping | never |

### CPU frequency — DFS with locks, not hardcoded speeds
The CPU must NOT sit at 240 MHz all day. Enable IDF power management: the chip idles
low automatically and code *temporarily* locks high frequency only while needed.

```
CONFIG_PM_ENABLE=y
CONFIG_FREERTOS_USE_TICKLESS_IDLE=y     # automatic light sleep when all tasks idle
```
```cpp
esp_pm_config_t pm = {
  .max_freq_mhz = 240,
  .min_freq_mhz = 40,          // raise to 80 while WiFi is started
  .light_sleep_enable = true,
};
ESP_ERROR_CHECK(esp_pm_configure(&pm));
```

Rules:
- **Rendering / animation:** acquire an `ESP_PM_CPU_FREQ_MAX` lock at animation start,
  release when the animation ends / UI goes idle. Never pin 240 MHz globally.
  (This is how sections 8 and 9 coexist: full speed *during* frames, minimum between.)
- **SPI pushes need stable APB:** hold the CPU_FREQ_MAX (or `ESP_PM_APB_FREQ_MAX`)
  lock around every frame push — with DFS active and no lock, the 80 MHz SPI clock
  degrades silently.
- **WiFi sessions:** WiFi requires min_freq ≥ 80 MHz while started — bump `min_freq_mhz`
  to 80 for the session, restore 40 after `esp_wifi_stop()`. Leave modem sleep
  (default) enabled; it duty-cycles the radio between beacons.
- **UART console under DFS:** frequency switches can distort the default UART clock —
  keep the console UART on the REF_TICK/fixed clock source (sdkconfig UART console
  options) or accept that clean logs need the max-freq lock held during bring-up.
- Verify DFS actually engages: `CONFIG_PM_PROFILING=y` and dump `esp_pm_dump_locks()`
  behind a debug command — if some task holds a lock forever, DFS is dead.

### As built (2026-08-25): DFS + automatic light sleep + screen timeout
`main/powersave.cpp` calls `esp_pm_configure()` (40–240 MHz, `light_sleep_enable`).
**This call is what actually turns DFS and light sleep on — `CONFIG_PM_ENABLE=y`
alone does nothing**, and this project ran pinned at 240 MHz until it was added.
Tasks that must not be interrupted hold the `ESP_PM_NO_LIGHT_SLEEP` lock via
`powersave_prevent_sleep()` / the `PowersaveHold` RAII guard; the UI task holds
it whenever the screen is on, and releases it when the screen blanks.

Screen timeout (`settings_get_screen_timeout_s()`, default 15 s, 0 = never, in
NVS beside brightness). On timeout the UI task: backlight to 0 → `lcd.sleep()`
→ `touch_sleep()` (FT6336 into DEEPSLEEP, ~3 mA → ~100 µA) → releases the
no-sleep lock → stops rendering entirely. LDO3 stays up so waking does not need
the full section-3 re-init.

**Wake is the accelerometer, not touch.** Keeping the FT6336 alive to wake on
touch costs ~3 mA — more than the entire screen-off budget — so instead the UI
task polls the BMA423 while dark (~4 Hz) and wakes when the gravity vector
moves by more than ~0.35 g. Hardware tilt/tap interrupts would be cheaper again
but need the BMA423 feature engine's config-file upload, which we do not
currently do (section 2). Waking pulses EXTEN to bring touch back — which only
works because the EXTEN bit is now driven correctly (section 3).

**Verified 2026-08-25** with `pmlocks` (`CONFIG_PM_PROFILING=y` + `esp_pm_dump_locks`):
66% of time in light sleep, 23% awake at 40 MHz, 10% at 240 MHz,
`light_sleep_reject_counts:0` — against 100% pinned at 240 MHz before.

**RAII + `vTaskDelete` is a trap here.** A `PowersaveHold` living in a task
function leaks the lock forever, because `vTaskDelete(nullptr)` never returns
and the destructor never runs — which silently disables light sleep for the
whole system. `pmlocks` showed `no_sleep Active=2` and `light_sleep_counts:0`.
The radio scan tasks therefore put the guarded work in a separate body function
and call `vTaskDelete` only after it returns. Check `Active` is 0 at idle
whenever adding a new lock holder.

**Measuring it:** the discharge ADC reads ~0 while USB is attached, and
unplugging removes the console — so `powersave_log_sample()` writes a trace
into RTC memory once a second. Procedure: `powerlog clear` → unplug USB → use
the watch → replug → `powerlog`, which prints CSV plus screen-on/screen-off
averages. RTC memory (not NVS) because this is a cheap-to-retake diagnostic
and must not wear flash at one write per second.

### Sleep ladder (increasing savings)
1. **Idle, screen on:** DFS 40 MHz floor + tickless light sleep between events.
2. **Screen off:** LDO2 off → `lcd.sleep()` → suspend UI task. Wake sources: BMA423
   tilt/double-tap IRQ (GPIO39), PEK IRQ (GPIO35), RTC alarm (GPIO37) — all via
   normal GPIO interrupts under light sleep.
3. **Long idle / pocket mode:** LDO3 off too, then **deep sleep** with ext1 wake on
   GPIO35 | GPIO37 | GPIO39 (all RTC-capable). Time survives in the PCF8563.
   On wake: chip reboots — run the FULL section-3 init order from scratch.

Deep-sleep prep sequence: LDO2/LDO3/LDO4 + EXTEN off → DRV2605 standby → wifi/bt
deinit → `esp_deep_sleep_start()`.

### Measure, don't guess (power edition)
The AXP202 has a fuel gauge and current ADCs. Log battery voltage + charge/discharge
current (XPowersLib battery APIs) once per minute behind a `DEBUG_POWER` flag.
A power change without before/after mA numbers is not a power change.
Sanity targets: screen-on idle < ~45 mA · screen-off light sleep < ~4 mA ·
deep sleep < ~0.5 mA · GPS acquiring adds ~30–40 mA (why LDO4 gating matters most).

---

## 10. PC SIMULATOR — LovyanGFX SDL build (develop UI without the watch)

LovyanGFX itself runs on desktop: the repo ships an SDL platform
(`lgfx::Panel_sdl`, see LovyanGFX/examples_for_PC — CMake + SDL2). The same
`main/ui/` render code compiles unchanged; only the device class differs.

`sim/main.cpp` sketch:
```cpp
#define LGFX_USE_V1
#include <LGFX_AUTODETECT.hpp>   // or explicit Panel_sdl setup, 240x240 window
static LGFX lcd(240, 240);       // SDL window standing in for the ST7789
static LGFX_Sprite frame(&lcd);
// fake services fill the same ViewModels: system clock for time, ramping battery,
// mouse press/drag/release fed into the same gesture recognizer as touch
// same loop as watch: viewdefs[current].render(frame, vm); frame.pushSprite(0,0);
```

Build & run:
```bash
cd sim && cmake -B build && cmake --build build -j
./build/twatch_sim                 # interactive: mouse = finger
./build/twatch_sim --screen watchface --shot out.png   # headless screenshot, exit
```

Rules:
- `--shot` mode must exist: set ViewModels deterministically (fixed fake time 10:08,
  battery 67 %), render N frames, dump `frame.getBuffer()` to PNG (stb_image_write),
  exit 0. This is the agent's primary visual verification tool. Accept `--screen <view>`
  to select any ViewId.
- Golden images in `sim/golden/*.png`; compare with ImageMagick
  `compare -metric AE render.png golden.png diff.png`.
- Mouse click/drag/release = touch press/swipe, fed through the SAME gesture
  recognizer and ViewManager as on-watch input.
- SDL2 dev package required on host: `sudo apt install libsdl2-dev`.

---

## 11. Build / flash / serial debug workflow (agent-friendly)

Port is `/dev/ttyUSB0` on most units, `/dev/ttyACM0` on the primary dev unit — see
section 5. Substitute whichever `ls /dev/ttyUSB* /dev/ttyACM*` shows on your unit.

**The port number is NOT stable.** A reset re-enumerates the USB device, and
it can come back as `ttyACM1`, `ttyACM2`, … Symptoms are confusing: flash fails with a
bare `ninja: build stopped`, or a serial capture silently produces an empty log while
the watch is plainly running. Resolve the port per-invocation rather than hardcoding it:
```bash
PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
```

```bash
idf.py build
idf.py -p "$PORT" -b 921600 flash          # drop to 460800/115200 on sync errors
```

### Boot-loop retry loop (mostly historical since the 40 MHz flash fix)
Flashing used to boot-loop roughly half the time (`rst:0x10`, `csum err`,
garbage `load:` addresses). **That was the 80 MHz flash clock and is fixed —
see section 13; 40 MHz measured 30/30 clean boots.** Keep the retry loop for
unattended scripts anyway: it costs nothing, and it re-resolves the port each
pass, which still matters because a reset can renumber the USB device.
```bash
for a in 1 2 3 4 5; do
  PORT=$(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -1)
  [ -z "$PORT" ] && { sleep 3; continue; }              # watch dropped off USB
  idf.py -p "$PORT" -b 460800 flash > flash.log 2>&1
  grep -q "Hash of data verified" flash.log || continue
  stty -F "$PORT" 115200 raw -echo && timeout 12 cat "$PORT" > boot.log
  grep -q "<a line your app prints late in boot>" boot.log && break
done
```

### DECODE `rst:0x..` BEFORE NAMING A FAILURE
Naming a failure from its symptom pattern instead of its reset code cost hours
here: the boot loops were called "brownouts" for a whole session when the code
said RTC watchdog, which sent the investigation at batteries and USB cables
instead of the flash clock. The ESP32 codes (`esp_rom/esp32/include/esp32/rom/rtc.h`):
| Code | Meaning |
|---|---|
| `0x01` | POWERON_RESET — real power-on |
| `0x03` | SW_RESET |
| `0x05` | DEEPSLEEP_RESET |
| `0x07`/`0x08` | TG0/TG1 watchdog |
| `0x0c` | SW_CPU_RESET — e.g. after a panic |
| **`0x0f`** | **RTCWDT_BROWN_OUT_RESET — the actual brownout**, "vdd not stable" |
| **`0x10`** | **RTCWDT_RTC_RESET — RTC watchdog**, boot did not complete in time |
Corollary: if a failure reproduces during a **read-only** operation
(`esptool.py flash_id`) or before your code runs, it is not your firmware —
stop reading application code and look at flash/clock/board settings.

### A serial capture is only EVIDENCE if the firmware was alive for it
This is the single easiest way to reach a confidently wrong conclusion here.
An empty log looks identical whether the watch is idle, boot-looping, or
off USB entirely — all three happened while "testing" touch on 2026-08-25, and
each empty log was briefly mistaken for "the feature is broken". Before drawing
*any* negative conclusion from a capture, confirm all three:
1. `ls /dev/ttyACM* /dev/ttyUSB*` still shows a port (it can vanish mid-test),
2. `grep -c 'rst:0x' capture.log` is **0** (no resets at all in the window),
3. the log contains periodic output proving the app was running — GPS lines
   during time-sync, or send `status` over the console and see it answer.
Builds that print nothing while idle need (3) explicitly; for a human-in-the-
loop test also ask the user to confirm something live on screen (a ticking
clock), because they may simply not have been touching the watch.

### Vendor firmware — the ground-truth "is it really the hardware?" test
When a peripheral looks physically dead, LilyGO's stock firmware settles it,
because it drives every chip on the board:
```bash
curl -fsSL -o vendor.bin \
  https://raw.githubusercontent.com/Xinyuan-LilyGO/TTGO_TWatch_Library/master/bin/2020-v2/twatch-2020-v2-220531.bin
esptool.py --chip esp32 -p "$PORT" -b 460800 write_flash 0x0 vendor.bin
```
It is a **merged full-flash image**: offset `0x0` is `0xff` padding (so
`esptool.py image_info` on it fails — that is normal), bootloader at `0x1000`,
partition table `0x8000`, app `0x10000`. Plain esptool, not idf.py — it is an
Arduino/PlatformIO build. Restoring ours afterwards is just `idf.py flash`.
**Read the result carefully:** vendor firmware working proves the hardware is
fine, but does NOT prove our code is buggy — it may simply be programming
device state we never set (see the section-3 rule). That exact mis-reading
cost hours on 2026-08-25.

If a fresh `dialout` group membership hasn't propagated to the current shell yet
(`groups` doesn't list it, commands below get `Permission denied`), wrap the
command with `sg dialout -c "..."` rather than re-running `usermod` — the
membership is likely already correct at the system level (`getent group dialout`),
just stale in that shell's process.

### Serial monitoring — DO NOT use interactive `idf.py monitor` directly
`idf.py monitor` requires an attached TTY and fails in a non-interactive/agent
shell ("Monitor requires standard input to be attached to TTY"). Use raw capture
instead, chained directly onto the flash command in the same shell invocation —
the board hard-resets and starts printing immediately after flashing, so a
separate later `cat` misses the boot log:
```bash
# capture a boot log
idf.py -p /dev/ttyACM0 flash && stty -F /dev/ttyACM0 115200 raw -echo && \
  timeout 15 cat /dev/ttyACM0 | tee boot.log

# background monitor with file output (then read monitor.log)
idf.py -p /dev/ttyACM0 monitor 2>&1 | tee monitor.log &
```

### Serial debug console — drive the UI without touching the watch
`main/debug_console.cpp` reads newline-terminated commands from the console UART
(shared with ESP_LOGx; there is no second UART free for a dedicated debug link).
Started from `app_main()` via `debug_console_start()`; the UI task drains the
queue with `debug_console_poll()` each loop, routing commands through the SAME
`switch_view()` / `do_tap()` paths physical gestures use — so a console repro
exercises the real code path, not a parallel one.

| Command | Effect |
|---|---|
| `view <name>` | jump to a view: watchface, battery, gps, tilt, haptic, settings, wifi |
| `next` / `prev` | swipe to the next/previous view |
| `tap [left\|right]` | simulate a tap (default center) |
| `status` | log current view + key state + free heap (catches radio-cycle leaks) |
| `touch` | touch health: consecutive I2C errors, probe of 0x38, rail states, scan of BOTH I2C buses |
| `touchfix` | force touch recovery (LDO3 cycle + EXTEN reset + reconfigure) |
| `touchsleep` | put the FT6336 into DEEPSLEEP — reproduces the "dead touch" failure on demand |
| `touchdump` | dump FT6336 mode/config/status registers (compare against the sanity values in section 2) |
| `touchmon` | poll raw touch status for 10 s — proves whether the sensor reports points at all |
| `treg <hex> <hex>` | write any FT6336 register, so config hypotheses can be tested without a reflash |
| `exten 0\|1` | drive the touch reset line (REG12 bit 0). `exten 0` reproduces "dead touch" exactly |
| `power` | battery mV / % / discharge mA — reads 0 mA on USB, see section 9 |
| `powerlog [clear]` | dump / reset the RTC-memory power trace (the only way to measure off-USB) |
| `sleep` | blank the screen now instead of waiting for the timeout |
| `timeout <sec>` | set the screen timeout, 0 = never; persisted to NVS |
| `bma` | BMA423 health: probe, raw chip id, config registers, live reading (~1g at rest) |
| `bmainit` | re-run the BMA423 init now |
| `ldo3 0\|1` | set AXP202 LDO3 mode (0 = LDO, 1 = DCIN). Diagnostic only — **leave it at 0** |

**Diagnosing touch is a two-question problem, and conflating them wasted hours
on 2026-08-25.** Ask them in order: (1) *Does the chip answer I2C?* — `touch`
scans both buses; silence on bus 1 means DEEPSLEEP, not dead hardware.
(2) *Does it report points?* — `touchmon` while a finger is actually held on
the glass. A chip can pass (1) perfectly and still fail (2). Never infer (2)
from an empty log without confirming a human was really touching the screen.

This exists because physical-gesture testing races the serial capture window
(the agent cannot time a swipe against a `timeout N cat`). Scripted repro:
```bash
sg dialout -c '
stty -F /dev/ttyACM0 115200 raw -echo
exec 3<>/dev/ttyACM0
cat <&3 > session.log &
sleep 2; printf "view wifi\n" >&3; sleep 6; printf "tap\n" >&3; sleep 6
kill %1; exec 3<&-'
```
Add a command here rather than adding a one-off test build whenever a bug needs
a repeatable trigger.

**Light sleep eats the first command.** With automatic light sleep enabled
(section 9), the bytes that wake the chip are consumed, so the first command
sent after an idle period is silently lost — no "unknown command" warning, just
nothing. Send a bare newline, pause ~1 s, then the real command:
```bash
printf "\n" >&3; sleep 1; printf "view gps\n" >&3
```
Once the screen is on the UI holds the no-sleep lock and the console is
reliable again, so this only bites the first command of a session.

### Crash decoding
```bash
xtensa-esp32-elf-addr2line -pfiaC -e build/<project>.elf <addr1> <addr2> ...
```
(`idf.py monitor` auto-decodes; raw `cat` does not.)
Decode BOTH cores' backtraces on a dual-core hang — the stalled core names the
culprit, the other usually just shows the tick spinlock it is stuck behind.

### Flash recovery
No BOOT/EN buttons. If auto-download fails (e.g. firmware deep-sleeps immediately):
start `idf.py flash` FIRST, then power the watch on. During development keep
`vTaskDelay(pdMS_TO_TICKS(3000))` at the top of app_main before any sleep logic.

No JTAG. All debugging is ESP_LOGx + serial. `CONFIG_LOG_DEFAULT_LEVEL_INFO=y`
(DEBUG when hunting), per-tag levels.

---

## 12. VISUAL VERIFICATION (how the agent checks what the UI looks like)

Never claim a UI change looks correct without looking at a render.

1. **Primary (fast, every UI iteration):** build sim, run
   `./build/twatch_sim --shot out.png`, VIEW the PNG. Diff against `sim/golden/` when
   a golden exists. Update goldens only on intentional visual changes.
2. **On-device (verifies the firmware pipeline: DMA path, strip seams, fonts):**
   serial command `snap` dumps the composed frame (or panel-bound strips) as base64
   RGB565 between `---SNAP-BEGIN---`/`---SNAP-END---` markers;
   `python3 tools/decode_snapshot.py monitor.log out.png` converts; VIEW the PNG.
   (Panel GRAM is NOT readable back — no MISO — so snapshot the sprite, not the panel.)
3. **Physical panel truth (inversion, offsets, backlight, real colors):** ask the user
   for a phone photo. Needed only at bring-up or when symptoms smell panel-level.

Tier 1 answers "is my UI code right?", tier 2 "is my render pipeline right?",
tier 3 "is my panel config right?".

---

## 13. Known failure modes → first suspect

| Symptom | Cause |
|---------|-------|
| Black screen, code "runs" per logs | LDO2/LDO3 not enabled, or lcd.init() before PMU |
| Screen shows garbage/noise | SPI 80 MHz marginal on this unit → try 40 MHz; or missing `invert=true` |
| Colors swapped (blue↔orange) | RGB565 byte-swap applied 0 or 2 times (need exactly 1; PATH B trap) |
| Horizontal seam lines during animation | Strip pushed before render finished — check DMA/render interleave order |
| FPS far below target | Sprite silently in PSRAM (DMA can't read PSRAM), SPI at 40 MHz, or missing startWrite/endWrite — check all three, measure with DEBUG_FPS |
| Touch dead, I2C addr 0x38 NACKs | EXTEN not toggled (touch stuck in reset), or LDO3 off |
| Touch works then dies after sleep | EXTEN disabled during sleep, not re-toggled on wake |
| **Touch totally dead: I2C1 scan finds ZERO devices, reads fail `ESP_ERR_INVALID_STATE`, survives reboot AND battery pull, but bus 0 is fine and the panel renders** | **FT6336 held in reset because AXP202 EXTEN (REG12 bit 0) is low** — root-caused 2026-08-25. Our code could not raise it because XPowersLib writes bit 6 for EXTEN (see the warning in section 3), so the chip stayed in reset indefinitely and only LilyGO's firmware could revive it. Fixed: EXTEN is now driven raw, and `touch_read()` self-heals after ~5 s. Reproduce exactly with `exten 0`; inspect with `touch`; force recovery with `touchfix`. A DEEPSLEEP'd chip (`0xA5`=3) presents identically, so `touch_init()` also forces power mode ACTIVE — but reset was the actual cause here, not sleep |
| Touch dead only in OUR firmware but fine in vendor firmware | Same DEEPSLEEP cause as above. **Key lesson: state persists across reflashes.** LDO3 stays powered through a flash, so the FT6336 never cold-boots, and AXP202 registers are battery-backed too. Flashing vendor firmware "fixes" it because that firmware explicitly programs the chip — which is why the bug looked like it lived in our code when it lived in inherited *device state* |
| **Touch reads SUCCEED (0 I2C errors, 0x38 probes OK, registers read sane values) but `0x02` is always 0 — no finger is ever reported** | **Over-configuration, not a fault.** Writing the INT-mode (`0xA4`) / monitor-timing (`0x87`) registers at init leaves the chip healthy-looking but not reporting points to a polling driver (hit 2026-08-25 while "fixing" the DEEPSLEEP bug above — the fix caused this). An EXTEN reset already gives working defaults: force ONLY power mode `0xA5`=0 and write nothing else. Use `touchdump` to compare against the sanity values in section 2 |
| Watch won't enumerate on USB | Watch powered off (long-press PEK), or dead battery. **Also happens spontaneously mid-session** on this unit — the port simply disappears, and any capture then reads nothing while the watch looks fine. Check `lsusb` for `1a86:55d4` before blaming firmware |
| Flash fails "Failed to write to target RAM" | Lower baud (460800/115200); check CH340 driver |
| Board boot-loops: `rst:0x10 (RTCWDT_RTC_RESET)` / `csum err` / `invalid header` / garbage `load:` addresses | **80 MHz flash clock — FIXED 2026-08-25 by dropping to 40 MHz.** Despite months of calling this a "brownout", `rst:0x10` is `RTCWDT_RTC_RESET` (the RTC **watchdog**); the brownout reset is `rst:0x0f` (`RTCWDT_BROWN_OUT_RESET`) and was NEVER seen. The failure is the **ROM bootloader** failing to read flash — `load:` segment 1 succeeds, segment 2 returns `0xffffffff`, boot never completes, watchdog resets. That stage runs before PSRAM and before our code, so no firmware change could ever have caused or fixed it. `clock div:1` in the boot banner = 80 MHz, `div:2` = 40 MHz. Measured: 80 MHz failed roughly half of all attempts; **40 MHz gave 30/30 clean boots**. Espressif's flash-mode docs warn 80 MHz "may cause crashing if the flash or board design is not capable of this speed". Note LilyGO's own firmware ships the same 80 MHz header and boot-looped identically, so this is the unit, not our build |
| A serial capture is empty / a feature "does nothing" | Do not conclude anything yet — verify the firmware was actually alive for that window (section 11). Port vanished, boot loop, and idle-silent builds all produce identical empty logs |
| Permission denied /dev/ttyUSB0 | User not in dialout group |
| Random reboots when GPS on | Brownout: enable LDO4 only when needed, check battery |
| Everything dies after `esp_deep_sleep_start` | Expected: only RTC domain survives; re-run full init order on wake |
| Sim renders differ from watch | Different color depth or font set between builds — keep them identical |
| Battery drains overnight | LDO4/GPS or WiFi left on, or no sleep ladder — audit section 9 gating table |
| Display corrupts only when idle/DFS on | Frame pushed without CPU/APB max-freq lock held (section 9) |
| `Interrupt wdt timeout`, one core looping in `vListInsert`/`vTaskPlaceOnEventList`, other spinning on the tick spinlock | A **blocking call inside `portENTER_CRITICAL`**. Interrupts are off and a spinlock is held, so blocking corrupts scheduler state and wedges both cores. Hit for real with `esp_wifi_scan_get_ap_records()` inside the lock (2026-08-25). Critical sections may ONLY copy plain data — never call a driver/IPC/WiFi API inside one |
| `bma423_init failed with code -2` (`BMA4_E_COM_FAIL`) from SensorLib, yet the chip ACKs at 0x19 | **A SensorLib bug, not the chip.** Raw I2C reads of the same registers work perfectly (chip id = 0x13) at the same moment SensorLib fails, at boot and on demand alike. Fixed by driving the BMA423 directly in `tilt.cpp` (section 2). Diagnose with the `bma` console command, which probes, raw-reads the id, and prints a live reading — magnitude should be ~1g |
| (historical) Boot log ends at `tilt_init() failed` (`bma423_init failed with code -2`) | **Fixed 2026-08-25 — `app_main()` no longer aborts on it.** It used to `return` on any failed init step, so one flaky peripheral killed the whole boot: the UI task never started and the watch presented as frozen with no way in. Only `power_init()` and `lcd.init()` are load-bearing now; everything else logs and continues. The BMA423 flake itself is transient (it ACKs at 0x19 but `bma423_init` fails), and `tilt_init()` retries 3× before giving up |
| Watch "freezes on startup" — display static, but the serial console still answers `view`/`next` yet NOT `status` | The **UI task is not running** while the console task is. `status` is handled by the UI task, so no reply to it means the UI task never started or is wedged. Check the boot log for an init step that returned early. Everything the UI task owns — rendering, touch polling, touch auto-recovery — is dead in this state |

## 14. Power-key (PEK) handling
The side button is NOT a GPIO. AXP202 raises IRQ on GPIO35; read/clear IRQ status
via XPowersLib (`isPekeyShortPressIrq()` etc.). Enable short+long press IRQs during
PMU init. Long-press-6s hardware poweroff is built into the PMU.

## 15. Style / conventions
- ESP_LOG with per-module tags ("PWR", "DISP", "TOUCH", "GPS", "UI", ...)
- All GPIO/addr constants only from twatch_v2_pins.h — never magic numbers inline
- Check esp_err_t returns (ESP_ERROR_CHECK in init paths)
- UI task pinned to core 1; radio/sensors on core 0
- ui/ directory: no hardware includes, ever — it must compile in the sim build
- Before claiming "driver bug": print AXP202 output states and I2C scan both buses
  (SensorLib has SensorWireHelper for scanning)
- Before claiming "performance fix": show before/after fps numbers from DEBUG_FPS
- Before claiming "power fix": show before/after mA numbers from DEBUG_POWER
- Every new feature PR/change states its power story: what it powers on, and the
  exact code path that powers it back off (section 9 table must stay accurate)
