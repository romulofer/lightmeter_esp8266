# Light Meter – Pinout Reference

Two firmware variants are available under `src/`:

| Variant         | Sketch folder                     | Board                                                                                       |
| --------------- | --------------------------------- | ------------------------------------------------------------------------------------------- |
| Integrated OLED | `src/lightmeter_integrated_oled/` | ESP8266 module with built-in 128×64 OLED (e.g. Wemos/LOLIN ESP8266 OLED, TTGO ESP8266 OLED) |
| External OLED   | `src/lightmeter_external_oled/`   | NodeMCU v2/v3 (ESP-12E) or any bare ESP8266 + separate SSD1306 OLED module                  |

The firmware logic is identical. The only difference is how the display is connected.

---

## Variant A – Integrated OLED board

The OLED is wired internally on the board. No display connections needed.

> Check your specific board's datasheet to confirm which GPIO the display is
> connected to. The most common mapping is SDA=GPIO4, SCL=GPIO5, but some boards
> (e.g. Wemos OLED Shield) use SDA=GPIO2, SCL=GPIO14. If the display stays blank,
> change `Wire.begin()` to `Wire.begin(SDA_PIN, SCL_PIN)` in the sketch.

### External connections required

#### Light Sensor (BH1750, I2C)

| Sensor pin | Connect to | Notes                               |
| ---------- | ---------- | ----------------------------------- |
| VCC        | 3V3        |                                     |
| GND        | GND        |                                     |
| SDA        | D2 (GPIO4) | Shared I2C bus with integrated OLED |
| SCL        | D1 (GPIO5) | Shared I2C bus with integrated OLED |
| ADDR       | GND        | I2C address → 0x23 (default)        |

#### Buttons

| Button        | Board pin | GPIO | Pull-up                      | Function                                           |
| ------------- | --------- | ---- | ---------------------------- | -------------------------------------------------- |
| Metering      | D5        | 14   | Internal                     | Take a light reading                               |
| Plus (+)      | D6        | 12   | Internal                     | Increase aperture / shutter speed / ISO / ND value |
| Minus (−)     | D7        | 13   | Internal                     | Decrease aperture / shutter speed / ISO / ND value |
| Mode          | D0        | 16   | **External 10 kΩ to 3V3** ⚠️ | Toggle aperture priority ↔ shutter speed priority  |
| Menu / ISO    | D1        | 5    | Internal                     | Cycle screens: main → ISO → ND filter → main       |
| Metering Mode | D2        | 4    | Internal                     | Switch ambient ↔ flash metering                    |

#### Battery monitoring

| Signal    | Pin | Notes                                         |
| --------- | --- | --------------------------------------------- |
| Battery + | A0  | Max 3.2 V (assuming on-board voltage divider) |
| Battery − | GND |                                               |

#### Full wiring summary

```
Integrated OLED board         External components
──────────────────────────────────────────────────────────
3V3  ───────────────────────── VCC  (BH1750)
GND  ───────────────────────── GND  (BH1750, all buttons)
D1 (GPIO5 / SCL) ────────────── SCL  (BH1750)
D2 (GPIO4 / SDA) ────────────── SDA  (BH1750)
D5 (GPIO14) ─────────────────── Metering button      → GND
D6 (GPIO12) ─────────────────── Plus (+) button      → GND
D7 (GPIO13) ─────────────────── Minus (−) button     → GND
D0 (GPIO16) ─────────────────── Mode button          → GND  (+10kΩ pull-up to 3V3)
D1 (GPIO5)  ─────────────────── Menu button          → GND
D2 (GPIO4)  ─────────────────── Metering Mode button → GND
A0          ─────────────────── Battery positive (max 3.2 V)
```

---

## Variant B – External OLED (NodeMCU + separate module)

The OLED module is wired manually via I2C alongside the BH1750 sensor.
No external I2C pull-up resistors are needed — the NodeMCU board has them.

### External connections required

#### OLED Display (SSD1306, 128×64, I2C)

| Display pin | Connect to | Notes          |
| ----------- | ---------- | -------------- |
| VCC         | 3V3        |                |
| GND         | GND        |                |
| SDA         | D2 (GPIO4) | Shared I2C bus |
| SCL         | D1 (GPIO5) | Shared I2C bus |

I2C address: **0x3C** (most common; some modules use 0x3D — change `OLED_I2C_ADDR` in the sketch)

#### Light Sensor (BH1750, I2C)

| Sensor pin | Connect to | Notes                        |
| ---------- | ---------- | ---------------------------- |
| VCC        | 3V3        |                              |
| GND        | GND        |                              |
| SDA        | D2 (GPIO4) | Shared I2C bus with OLED     |
| SCL        | D1 (GPIO5) | Shared I2C bus with OLED     |
| ADDR       | GND        | I2C address → 0x23 (default) |

#### Buttons

| Button        | Board pin | GPIO | Pull-up                      | Function                                           |
| ------------- | --------- | ---- | ---------------------------- | -------------------------------------------------- |
| Metering      | D5        | 14   | Internal                     | Take a light reading                               |
| Plus (+)      | D6        | 12   | Internal                     | Increase aperture / shutter speed / ISO / ND value |
| Minus (−)     | D7        | 13   | Internal                     | Decrease aperture / shutter speed / ISO / ND value |
| Mode          | D0        | 16   | **External 10 kΩ to 3V3** ⚠️ | Toggle aperture priority ↔ shutter speed priority  |
| Menu / ISO    | D1        | 5    | Internal                     | Cycle screens: main → ISO → ND filter → main       |
| Metering Mode | D2        | 4    | Internal                     | Switch ambient ↔ flash metering                    |

#### Battery monitoring

| Signal    | Pin | Notes                                        |
| --------- | --- | -------------------------------------------- |
| Battery + | A0  | Max 3.2 V (NodeMCU on-board voltage divider) |
| Battery − | GND |                                              |

#### Full wiring summary

```
NodeMCU                       External components
──────────────────────────────────────────────────────────
3V3  ───────────────────────── VCC  (OLED, BH1750)
GND  ───────────────────────── GND  (OLED, BH1750, all buttons)
D1 (GPIO5 / SCL) ────────────── SCL  (OLED, BH1750)
D2 (GPIO4 / SDA) ────────────── SDA  (OLED, BH1750)
D5 (GPIO14) ─────────────────── Metering button      → GND
D6 (GPIO12) ─────────────────── Plus (+) button      → GND
D7 (GPIO13) ─────────────────── Minus (−) button     → GND
D0 (GPIO16) ─────────────────── Mode button          → GND  (+10kΩ pull-up to 3V3)
D1 (GPIO5)  ─────────────────── Menu button          → GND
D2 (GPIO4)  ─────────────────── Metering Mode button → GND
A0          ─────────────────── Battery positive (max 3.2 V)
```

---

## Button wiring (both variants)

All buttons connect between the GPIO pin and GND. The firmware enables internal
pull-ups, so the pin reads HIGH at rest and LOW when pressed.

```
NodeMCU pin ──── button ──── GND
```

Exception — Mode button (GPIO16, no internal pull-up):

```
3V3 ──── 10 kΩ ──┬──── button ──── GND
                 │
              D0 (GPIO16)
```

---

## Battery thresholds

Defined in the sketch as raw ADC counts (0–1023). Adjust to match your battery:

| Constant    | Default | Approx. voltage at A0 |
| ----------- | ------- | --------------------- |
| `BATT_FULL` | 800     | ~2.5 V                |
| `BATT_MED`  | 640     | ~2.0 V                |
| `BATT_LOW`  | 480     | ~1.5 V                |
