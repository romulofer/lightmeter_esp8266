#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BH1750.h>
#include <EEPROM.h>

// ── I2C OLED (128×64, address 0x3C) ─────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1   // no reset pin; share ESP8266 reset
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

BH1750 lightMeter;

// ── Calibration ──────────────────────────────────────────────────────────────
#define DomeMultiplier          2.17    // Multiplier for white translucent dome

// ── Button pins (ESP8266 GPIO) ────────────────────────────────────────────────
// Avoid GPIO 0, 2, 15 (boot-mode strapping pins) and GPIO 1/3 (UART TX/RX).
#define MeteringButtonPin       14      // D5
#define PlusButtonPin           12      // D6
#define MinusButtonPin          13      // D7
#define ModeButtonPin           16      // D0  (no INPUT_PULLUP on GPIO16 – uses pull-down)
#define MenuButtonPin            5      // D1  (also I2C SCL – safe as button when not clocking)
#define MeteringModeButtonPin    4      // D2  (also I2C SDA – same note)
// NOTE: Wire.begin() is called before buttons are read, so D1/D2 are fine as
// open-drain I2C lines; the pull-ups keep them HIGH when idle.

// ── Battery monitoring ────────────────────────────────────────────────────────
// Connect battery (through a voltage divider if > 1 V) to the single ADC pin A0.
// ESP8266 ADC input range: 0–1 V (NodeMCU boards have an on-board 1:3.2 divider,
// giving a 0–3.2 V range on the A0 header pin).
// Adjust BATT_FULL / BATT_MED / BATT_LOW to match your actual battery voltage
// after the divider, expressed as raw ADC counts (0–1023).
#define BATT_FULL  800   // ~3.0 V on NodeMCU divider  (≈ 2×1.5 V alkaline, fresh)
#define BATT_MED   640   // ~2.4 V
#define BATT_LOW   480   // ~1.8 V

#define MaxISOIndex             57
#define MaxApertureIndex        70
#define MaxTimeIndex            80
#define MaxNDIndex              13
#define MaxFlashMeteringTime    5000    // ms

float   lux;
boolean Overflow = 0;
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

// ── EEPROM addresses ──────────────────────────────────────────────────────────
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

int battVolts;
#define batteryInterval 10000
unsigned long lastBatteryTime = 0;

#include "lightmeter.h"

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  // GPIO16 (D0) has no internal pull-up; wire a 10 kΩ external pull-up to 3.3 V.
  pinMode(PlusButtonPin,          INPUT_PULLUP);
  pinMode(MinusButtonPin,         INPUT_PULLUP);
  pinMode(MeteringButtonPin,      INPUT_PULLUP);
  pinMode(ModeButtonPin,          INPUT);        // GPIO16 – external pull-up required
  pinMode(MenuButtonPin,          INPUT_PULLUP);
  pinMode(MeteringModeButtonPin,  INPUT_PULLUP);

  //Serial.begin(115200);

  // I2C: default SDA = GPIO4 (D2), SCL = GPIO5 (D1)
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

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // If the display is not found, halt with a rapid blink on GPIO2 (built-in LED
    // on most ESP8266 modules; active LOW).
    pinMode(2, OUTPUT);
    while (true) {
      digitalWrite(2, LOW);  delay(100);
      digitalWrite(2, HIGH); delay(100);
    }
  }
  display.setTextColor(WHITE);
  display.clearDisplay();

  // Sanitise EEPROM values (first boot or corrupt data reads 0xFF = 255)
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
      // Ambient light metering
      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE_2);
      lux = getLux();

      if (Overflow == 1) {
        delay(10);
        lux = getLux();
      }

      refresh();
      delay(200);

    } else if (meteringMode == 1) {
      // Flash light metering
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
