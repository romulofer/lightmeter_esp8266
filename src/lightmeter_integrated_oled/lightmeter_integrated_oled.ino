// =============================================================================
// Light Meter – ESP8266 with INTEGRATED OLED display
//
// Target board: ESP8266 module with built-in 128×64 SSD1306 OLED
//               (e.g. Wemos/LOLIN ESP8266 OLED, TTGO ESP8266 OLED)
//
// The display is wired internally to the ESP8266 on the board.
// Default I2C pins for most integrated boards:
//   SDA → GPIO4  (D2)
//   SCL → GPIO5  (D1)
//
// If your board uses different pins, change the Wire.begin() call in setup().
// Common alternative: Wemos OLED Shield uses SDA=GPIO2, SCL=GPIO14 — in that
// case replace Wire.begin() with Wire.begin(2, 14).
// =============================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BH1750.h>
#include <EEPROM.h>

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1       // no dedicated reset pin on integrated boards
#define OLED_I2C_ADDR  0x3C    // most common; try 0x3D if display is blank

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

BH1750 lightMeter;

// ── Calibration ──────────────────────────────────────────────────────────────
#define DomeMultiplier  2.17    // multiplier for white translucent dome

// ── Button pins ───────────────────────────────────────────────────────────────
// Buttons are wired between the pin and GND (active LOW, internal pull-up).
// GPIO16 (D0) has no internal pull-up – requires external 10 kΩ to 3V3.
#define MeteringButtonPin       14  // D5
#define PlusButtonPin           12  // D6
#define MinusButtonPin          13  // D7
#define ModeButtonPin           16  // D0  ⚠ external 10 kΩ pull-up to 3V3 required
#define MenuButtonPin            5  // D1
#define MeteringModeButtonPin    4  // D2

// ── Battery monitoring ────────────────────────────────────────────────────────
// A0 on NodeMCU-style boards accepts 0–3.2 V (on-board divider).
// Adjust thresholds (ADC counts 0–1023) to match your battery.
#define BATT_FULL  800  // ~2.5 V at A0
#define BATT_MED   640  // ~2.0 V
#define BATT_LOW   480  // ~1.5 V

// ── Constants ─────────────────────────────────────────────────────────────────
#define MaxISOIndex           57
#define MaxApertureIndex      70
#define MaxTimeIndex          80
#define MaxNDIndex            13
#define MaxFlashMeteringTime  5000  // ms

// ── State ─────────────────────────────────────────────────────────────────────
float   lux;
boolean Overflow   = 0;
float   ISOND;

boolean PlusButtonState;
boolean MinusButtonState;
boolean MeteringButtonState;
boolean ModeButtonState;
boolean MenuButtonState;
boolean MeteringModeButtonState;

boolean ISOMenu    = false;
boolean NDMenu     = false;
boolean mainScreen = false;
bool    settingsDirty = false;

// ── EEPROM ────────────────────────────────────────────────────────────────────
#define EEPROM_SIZE         16
#define ISOIndexAddr         1
#define apertureIndexAddr    2
#define modeIndexAddr        3
#define T_expIndexAddr       4
#define meteringModeAddr     5
#define ndIndexAddr          6

#define defaultApertureIndex 12
#define defaultISOIndex      11
#define defaultModeIndex      0
#define defaultT_expIndex    19

uint8_t ISOIndex;
uint8_t apertureIndex;
uint8_t T_expIndex;
uint8_t modeIndex;
uint8_t meteringMode;
uint8_t ndIndex;

int           battVolts;
#define       batteryInterval 10000
unsigned long lastBatteryTime = 0;

#include "lightmeter.h"

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(PlusButtonPin,         INPUT_PULLUP);
  pinMode(MinusButtonPin,        INPUT_PULLUP);
  pinMode(MeteringButtonPin,     INPUT_PULLUP);
  pinMode(ModeButtonPin,         INPUT);        // GPIO16 – external pull-up required
  pinMode(MenuButtonPin,         INPUT_PULLUP);
  pinMode(MeteringModeButtonPin, INPUT_PULLUP);

  // Default I2C pins: SDA=GPIO4 (D2), SCL=GPIO5 (D1).
  // Change to Wire.begin(SDA, SCL) if your board uses different pins.
  Wire.begin();

  EEPROM.begin(EEPROM_SIZE);

  ISOIndex      = EEPROM.read(ISOIndexAddr);
  apertureIndex = EEPROM.read(apertureIndexAddr);
  T_expIndex    = EEPROM.read(T_expIndexAddr);
  modeIndex     = EEPROM.read(modeIndexAddr);
  meteringMode  = EEPROM.read(meteringModeAddr);
  ndIndex       = EEPROM.read(ndIndexAddr);

  battVolts = analogRead(A0);

  lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE_2);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    // Display not found – blink GPIO2 LED rapidly and halt.
    pinMode(2, OUTPUT);
    while (true) {
      digitalWrite(2, LOW);  delay(100);
      digitalWrite(2, HIGH); delay(100);
    }
  }
  display.setTextColor(WHITE);
  display.clearDisplay();

  // Sanitise EEPROM (reads 0xFF = 255 on first boot)
  if (apertureIndex > MaxApertureIndex) apertureIndex = defaultApertureIndex;
  if (ISOIndex      > MaxISOIndex)      ISOIndex      = defaultISOIndex;
  if (T_expIndex    > MaxTimeIndex)     T_expIndex    = defaultT_expIndex;
  if (modeIndex     > 1)               modeIndex     = defaultModeIndex;
  if (meteringMode  > 1)               meteringMode  = 0;
  if (ndIndex       > MaxNDIndex)      ndIndex       = 0;

  lux = getLux();
  refresh();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  if (millis() >= lastBatteryTime + batteryInterval) {
    lastBatteryTime = millis();
    battVolts = analogRead(A0);
  }

  readButtons();
  menu();

  if (MeteringButtonState == LOW) {
    if (settingsDirty) { SaveSettings(); settingsDirty = false; }
    lux = 0;
    refresh();

    if (meteringMode == 0) {
      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE_2);
      lux = getLux();
      if (Overflow == 1) { delay(10); lux = getLux(); }
      refresh();
      delay(200);

    } else if (meteringMode == 1) {
      lightMeter.configure(BH1750::CONTINUOUS_LOW_RES_MODE);

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(22, 20);
      display.print(F("Waiting"));
      display.setTextSize(1);
      display.setCursor(28, 44);
      display.print(F("for flash..."));
      display.display();

      unsigned long startTime = millis();
      float currentLux = 0;
      lux = 0;

      while (true) {
        if (startTime + MaxFlashMeteringTime < millis()) break;
        currentLux = getLux();
        delay(16);
        if (currentLux > lux) lux = currentLux;
      }
      refresh();
    }
  }
}
