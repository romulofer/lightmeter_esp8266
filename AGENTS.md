# AGENTS.md – Light Meter ESP8266

Reference document for AI agents working on this codebase. Read this before making any changes.

---

## Project overview

A handheld light meter / flash meter for photographers built on an ESP8266 microcontroller. It reads ambient or flash light intensity from a BH1750 sensor, calculates the correct exposure pair (aperture or shutter speed) for the chosen ISO and ND filter, and displays the result on a 128×64 SSD1306 OLED.

---

## Hardware

| Component | Notes |
|-----------|-------|
| ESP8266 (NodeMCU v2/v3 or module with built-in OLED) | Main MCU |
| BH1750 | I2C light sensor (lux, up to ~65535 lux raw) |
| SSD1306 128×64 OLED | I2C display, address 0x3C (some modules use 0x3D) |
| 6 push buttons | Active LOW, wired to GPIO and GND |
| Battery (2× AAA) | Monitored via ADC pin A0 |

### GPIO assignments

| Function | GPIO | NodeMCU pin | Pull-up |
|----------|------|-------------|---------|
| Metering | 14 | D5 | Internal |
| Plus (+) | 12 | D6 | Internal |
| Minus (−) | 13 | D7 | Internal |
| Mode (Ap ↔ Tv) | 16 | D0 | **External 10 kΩ to 3V3** — GPIO16 has no internal pull-up |
| Menu / ISO | 5 | D1 | Internal (shared with I2C SCL, safe when idle) |
| Metering Mode | 4 | D2 | Internal (shared with I2C SDA, safe when idle) |
| Battery ADC | A0 | A0 | NodeMCU on-board 1:3.2 divider → 0–3.2 V range |

All buttons are wired between the GPIO pin and GND. Pins read HIGH at rest, LOW when pressed.

---

## Firmware variants

There are three sketch variants under `src/`. The firmware logic is **identical** across all three — only the display wiring differs.

| Folder | Target board | Display connection |
|--------|-------------|-------------------|
| `src/lightmeter/` | NodeMCU (original/legacy) | External SSD1306 via I2C |
| `src/lightmeter_external_oled/` | NodeMCU v2/v3 or any bare ESP8266 | External SSD1306 module wired via I2C |
| `src/lightmeter_integrated_oled/` | ESP8266 board with built-in OLED (e.g. Wemos/LOLIN OLED, TTGO) | OLED wired internally on the board |

Each variant folder contains:
- `*.ino` — global state, `setup()`, `loop()`
- `lightmeter.h` — all function implementations (helpers, display, menu, button reading)

**The three `lightmeter.h` files are kept in sync and should always be identical.** If you change logic in one, apply the same change to all three.

---

## Source structure

```
src/
├── lightmeter/
│   ├── lightmeter.ino      # globals + setup() + loop()
│   └── lightmeter.h        # all functions
├── lightmeter_external_oled/
│   ├── lightmeter_external_oled.ino
│   └── lightmeter.h
└── lightmeter_integrated_oled/
    ├── lightmeter_integrated_oled.ino
    └── lightmeter.h

Schematic Diagram/
    schema_bb.png           # Fritzing breadboard diagram
    schema.fzz              # Fritzing source file

PINOUT.md                   # Full wiring tables for both variants
images/                     # Photos of the built device
```

---

## Library dependencies

| Library | Version tested | Purpose |
|---------|---------------|---------|
| Adafruit GFX Library | 1.12.6 | Display graphics primitives |
| Adafruit SSD1306 | 2.5.16 | OLED driver |
| BH1750 | 1.3.0 | Light sensor driver |
| Wire | built-in (ESP8266) | I2C bus |
| EEPROM | built-in (ESP8266) | Persistent settings |

---

## How to build

Use **arduino-cli** with the ESP8266 community board package (`esp8266:esp8266`).

```bash
# NodeMCU 1.0 (ESP-12E) — use for lightmeter and lightmeter_external_oled
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 src/lightmeter_external_oled

# Board with integrated OLED
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 src/lightmeter_integrated_oled
```

Board package URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`

---

## EEPROM layout

The ESP8266 EEPROM is flash-emulated; `EEPROM.commit()` is required to flush writes. Total size: 16 bytes.

| Address | Variable | Default |
|---------|----------|---------|
| 0 | *(unused)* | — |
| 1 | `ISOIndex` | 11 → ISO 100 |
| 2 | `apertureIndex` | 12 → f/5.6 |
| 3 | `modeIndex` | 0 (aperture priority) |
| 4 | `T_expIndex` | 19 → 1/100 s |
| 5 | `meteringMode` | 0 (ambient) |
| 6 | `ndIndex` | 0 (no filter) |

On first boot all bytes read 0xFF (255), which the firmware detects and replaces with defaults.

---

## Key constants and calibration

| Constant | Value | Meaning |
|----------|-------|---------|
| `DomeMultiplier` | 2.17 | Scales raw lux to account for the white translucent dome diffuser |
| `MaxISOIndex` | 57 | ISO range: 8 – 4 000 000 |
| `MaxApertureIndex` | 70 | f/1.0 – f/3251 |
| `MaxTimeIndex` | 80 | 1/10000 s – ~133 s |
| `MaxNDIndex` | 13 | ND2 – ND8192 |
| `MaxFlashMeteringTime` | 5000 ms | Window to capture flash peak |
| `BATT_FULL / BATT_MED / BATT_LOW` | 800 / 640 / 480 | Raw ADC thresholds for battery indicator |

`DomeMultiplier` should be recalibrated if a different diffuser material is used.

---

## Core logic

### Exposure formula

- **Aperture priority** (`modeIndex == 0`): calculates shutter speed
  ```
  T = 100 × A² / ISO_eff / 2^EV
  ```
- **Shutter priority** (`modeIndex == 1`): calculates aperture
  ```
  A = sqrt(2^EV × ISO_eff × T / 100)
  ```
- `ISO_eff` (`ISOND`) = ISO / 2^ndIndex (ND filter reduces effective sensitivity)
- `EV` = log2(lux / 2.5)

### ND filter encoding

`ndIndex` 0 means no filter. For `ndIndex` ≥ 1:
- Filter factor displayed: `2^ndIndex` (ND2, ND4, ND8 …)
- Optical density displayed: `(3 × ndIndex) / 10` (0.3, 0.6, 0.9 …)
- Exposure effect: divides effective ISO by `2^ndIndex`

### BH1750 modes

- **Ambient metering**: `ONE_TIME_HIGH_RES_MODE_2` (single shot, highest resolution)
- **Flash metering**: `CONTINUOUS_LOW_RES_MODE` — polls every ~16 ms for 5 seconds, keeps peak reading

### Index-to-value functions

- `getApertureByIndex(i)` — computes `f = round(2^(i/6) × scale) / scale` then snaps to standard f-stop values
- `getTimeByIndex(i)` — maps index to shutter speed in seconds (1/10000 s at index 0, longest at index 79)
- `getISOByIndex(i)` — maps index to ISO value (8 at index 0, up to 4 000 000)
- `fixTime(t)` — snaps a calculated shutter speed to the nearest standard value
- `fixAperture(a)` — snaps a calculated aperture to the nearest standard f-stop

---

## UI / menu flow

```
Main screen
  │
  ├── Menu button → ISO menu (adjust with +/−)
  │     └── Menu button → ND filter menu (adjust with +/−)
  │           └── Menu button → back to main screen
  │
  ├── Mode button → toggle Aperture priority ↔ Shutter priority
  ├── Metering Mode button → toggle Ambient ↔ Flash metering
  ├── +/− buttons → adjust aperture (Ap mode) or shutter speed (Tv mode)
  └── Metering button → take a reading (saves settings first if changed)
```

The active priority (aperture or shutter) is marked with `*` on the left of the display.

---

## Display layout (128×64 OLED)

```
[A/F] ISO:xxx          lx:xxxxx  [BAT]
──────────────────────────────────────
* f/xx.x                    │ EV: xx
                             │
  T: 1/xxxx
ND4=0.6
```

- Top row: metering mode (A=ambient, F=flash), ISO, lux reading, battery icon
- Middle: aperture (large), EV value
- Bottom: shutter speed (large), ND filter info if active
- `*` marks the priority parameter (the one held fixed by the user)

---

## Known design notes

- GPIO16 (D0 / Mode button) has no internal pull-up. An external 10 kΩ resistor to 3V3 is required on the hardware side.
- GPIO4 (D2) and GPIO5 (D1) are shared between I2C (SDA/SCL) and two buttons (Menu, Metering Mode). This works because buttons are only read when I2C is idle; the lines sit HIGH via the pull-ups.
- The three `lightmeter.h` files are manually kept in sync (no shared library or symlink). Any logic change must be applied to all three.
- EEPROM address 0 is intentionally unused (serves as an implicit reserved/alignment slot).
- `getTimeByIndex(MaxTimeIndex)` wraps to index 0 (the fastest shutter) because the guard condition is `>= MaxTimeIndex`. This is used intentionally in `fixTime()` to obtain the minimum representable shutter speed as a clamp value.
