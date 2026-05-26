// ── Helpers ───────────────────────────────────────────────────────────────────

void outOfrange() {
  display.println(F("--"));
}

void SaveSettings() {
  // Persist light-meter settings to EEPROM.
  // ESP8266 EEPROM is emulated in flash; commit() flushes the write buffer.
  EEPROM.write(ndIndexAddr,       ndIndex);
  EEPROM.write(ISOIndexAddr,      ISOIndex);
  EEPROM.write(modeIndexAddr,     modeIndex);
  EEPROM.write(apertureIndexAddr, apertureIndex);
  EEPROM.write(T_expIndexAddr,    T_expIndex);
  EEPROM.write(meteringModeAddr,  meteringMode);
  EEPROM.commit();   // required on ESP8266
}

/*
  Get light value from BH1750 sensor.
*/
float getLux() {
  uint16_t raw = (uint16_t)lightMeter.readLightLevel();

  if (raw >= 65534) {
    Overflow = 1;
    raw = 65535;
  } else {
    Overflow = 0;
  }

  return raw * DomeMultiplier;
}

// log2 helper (avoids conflict with the C99 built-in on ESP8266 toolchain)
static inline float log2f_local(float x) {
  return log(x) / log(2.0f);
}

float lux2ev(float lux) {
  return log2f_local(lux / 2.5f);
}

// Return aperture value (1.4, 1.8, 2.0 …) by index.
float getApertureByIndex(uint8_t indx) {
  float roundIndx = 10.0f;

  if (indx > 39) roundIndx = 1.0f;

  float f = round(pow(2.0f, indx / 3.0f * 0.5f) * roundIndx) / roundIndx;

  if      (f >= 1.1f  && f < 1.2f)  f = 1.1f;
  else if (f >= 1.2f  && f < 1.4f)  f = 1.2f;
  else if (f >  3.2f  && f < 4.0f)  f = 3.5f;
  else if (f >  5.0f  && f < 6.3f)  f = 5.6f;
  else if (f >  10.0f && f < 11.0f) f = 10.0f;
  else if (f >= 11.0f && f < 12.0f) f = 11.0f;
  else if (f >= 12.0f && f < 14.0f) f = 13.0f;
  else if (f >= 14.0f && f < 16.0f) f = 14.0f;
  else if (f >= 20.0f && f < 22.0f) f = 20.0f;
  else if (f >= 22.0f && f < 25.0f) f = 22.0f;
  else if (f >= 25.0f && f < 28.0f) f = 25.0f;
  else if (f >= 28.0f && f < 40.0f) f = 36.0f;
  else if (f >= 40.0f && f < 45.0f) f = 40.0f;
  else if (f >= 45.0f && f < 50.0f) f = 45.0f;
  else if (f >= 50.0f && f < 57.0f) f = 51.0f;
  else if (f >= 71.0f && f < 80.0f) f = 72.0f;
  else if (f >= 80.0f && f < 90.0f) f = 80.0f;
  else if (f >= 90.0f && f < 101.f) f = 90.0f;

  return f;
}

// Return ISO value (100, 200, 400 …) by index.
long getISOByIndex(uint8_t indx) {
  if (indx > MaxISOIndex) indx = 0;

  indx += 10;

  long  factor = 1;
  float iso    = 0;

  if      (indx > 60) { indx -= 50; factor = 100000; }
  else if (indx > 50) { indx -= 40; factor =  10000; }
  else if (indx > 40) { indx -= 30; factor =   1000; }
  else if (indx > 30) { indx -= 20; factor =    100; }
  else if (indx > 20) { indx -= 10; factor =     10; }

  switch (indx) {
    case 10: iso =  8.0f;   break;
    case 11: iso = 10.0f;   break;
    case 12: iso = 12.5f;   break;
    case 13: iso = 16.0f;   break;
    case 14: iso = 20.0f;   break;
    case 15: iso = 25.0f;   break;
    case 16: iso = 32.0f;   break;
    case 17: iso = 40.0f;   break;
    case 18: iso = 50.0f;   break;
    case 19: iso = 64.0f;   break;
    case 20: iso = 80.0f;   break;
    default: iso =  0.0f;   break;
  }

  return (long)floor(iso * factor);
}

float getMinDistance(float x, float v1, float v2) {
  return (x - v1 > v2 - x) ? v2 : v1;
}

float getTimeByIndex(uint8_t indx) {
  if (indx >= MaxTimeIndex) indx = 0;

  float factor = 0.0f;
  float t      = 0.0f;

  if      (indx < 10) {                  factor = 100.0f;    }
  else if (indx < 20) { indx -= 10;      factor =  10.0f;    }
  else if (indx < 30) { indx -= 20;      factor =   1.0f;    }
  else if (indx < 40) { indx -= 30;      factor =   0.1f;    }
  else if (indx < 50) { indx -= 40;      factor =   0.01f;   }
  else if (indx < 60) { indx -= 50;      factor =   0.001f;  }
  else if (indx < 70) { indx -= 60;      factor =   0.0001f; }
  else if (indx < 80) { indx -= 70;      factor =   0.00001f;}

  switch (indx) {
    case 0: t = 100.0f;  break;
    case 1: t =  80.0f;  break;
    case 2: t =  64.0f;  break;
    case 3: t =  50.0f;  break;
    case 4: t =  40.0f;  break;
    case 5: t =  32.0f;  break;
    case 6: t =  25.0f;  break;
    case 7: t =  20.0f;  break;
    case 8: t =  16.0f;  break;
    case 9: t =  12.5f;  break;
  }

  return 1.0f / (t * factor);
}

// Convert calculated time (seconds) to photography-style shutter speed.
double fixTime(double t) {
  float minTime = getTimeByIndex(MaxTimeIndex);

  if (t < minTime) return minTime;

  double divider = 1.0;
  t = 1.0 / t;

  if      (t > 99999) divider = 10000.0;
  else if (t >  9999) divider =  1000.0;
  else if (t >   999) divider =   100.0;
  else if (t >    99) divider =    10.0;

  t /= divider;

  if      (t >= 10   && t <= 12.5) t = getMinDistance(t, 10,   12.5);
  else if (t >= 12.5 && t <= 16  ) t = getMinDistance(t, 12.5, 16  );
  else if (t >= 16   && t <= 20  ) t = getMinDistance(t, 16,   20  );
  else if (t >= 20   && t <= 25  ) t = getMinDistance(t, 20,   25  );
  else if (t >= 25   && t <= 32  ) t = getMinDistance(t, 25,   32  );
  else if (t >= 32   && t <= 40  ) t = getMinDistance(t, 32,   40  );
  else if (t >= 40   && t <= 50  ) t = getMinDistance(t, 40,   50  );
  else if (t >= 50   && t <= 64  ) t = getMinDistance(t, 50,   64  );
  else if (t >= 64   && t <= 80  ) t = getMinDistance(t, 64,   80  );
  else if (t >= 80   && t <= 100 ) t = getMinDistance(t, 80,   100 );

  t *= divider;

  if (t == 32.0) t = 30.0;
  if (t == 16.0) t = 15.0;

  return 1.0 / t;
}

// Convert calculated aperture to photography-style aperture value.
float fixAperture(float a) {
  for (int i = 0; i < MaxApertureIndex; i++) {
    float a1 = getApertureByIndex(i);
    float a2 = getApertureByIndex(i + 1);

    if (a1 < a && a2 >= a) {
      return getMinDistance(a, a1, a2);
    }
  }
  return 0;
}

/*
  Return ND stop value from ndIndex.
  ND stops: 0, 3, 6, 9, 12 … (multiples of 3)
*/
uint8_t getND(uint8_t ndIndex) {
  if (ndIndex == 0) return 0;
  return 3 + (ndIndex - 1) * 3;
}

// ── Display ───────────────────────────────────────────────────────────────────

void refresh() {
  ISOMenu    = false;
  mainScreen = true;
  NDMenu     = false;

  float EV  = lux2ev(lux);
  float T   = getTimeByIndex(T_expIndex);
  float A   = getApertureByIndex(apertureIndex);
  long  iso = getISOByIndex(ISOIndex);

  uint8_t ndStop = getND(ndIndex);

  if (ndIndex > 0) {
    ISOND = iso / pow(2.0f, ndIndex);
  } else {
    ISOND = iso;
  }

  if (lux > 0) {
    if (modeIndex == 0) {
      // Aperture priority – calculate shutter speed
      T = fixTime(100.0 * pow(A, 2) / ISOND / pow(2.0, EV));

      for (int i = 0; i <= MaxTimeIndex; i++) {
        if (T == getTimeByIndex(i)) { T_expIndex = i; break; }
      }
    } else if (modeIndex == 1) {
      // Shutter priority – calculate aperture
      A = fixAperture(sqrt(pow(2.0, EV) * ISOND * T / 100.0));

      if (A > 0) {
        for (int i = 0; i <= MaxApertureIndex; i++) {
          if (A == getApertureByIndex(i)) { apertureIndex = i; break; }
        }
      }
    }
  } else {
    if (modeIndex == 0) T = 0;
    else                A = 0;
  }

  uint8_t Tdisplay = 0;
  double  Tfr  = 0;
  float   Tmin = 0;

  if (T >= 60) {
    Tdisplay = 0; Tmin = T / 60.0f;
  } else if (T >= 0.5f) {
    Tdisplay = 2;
  } else {
    Tdisplay = 1; Tfr = round(1.0 / T);
  }

  uint8_t linePos[] = {15, 37};

  display.clearDisplay();
  display.setTextColor(WHITE);

  // ── ISO label ──
  display.setTextSize(1);
  display.setCursor(13, 1);
  display.print(F("ISO:"));
  if (iso > 999999) {
    display.print(iso / 1000000.0, 2); display.print(F("M"));
  } else if (iso > 9999) {
    display.print(iso / 1000.0, 0);   display.print(F("K"));
  } else {
    display.print(iso);
  }

  display.drawLine(0, 10, 128, 10, WHITE);

  // ── Aperture ──
  display.setCursor(10, linePos[0]);
  display.setTextSize(2);
  display.print(F("f/"));
  if (A > 0) {
    display.print(A >= 100 ? A : A, A >= 100 ? 0 : 1);
  } else {
    outOfrange();
  }

  // ── Battery indicator ──
  display.setTextSize(1);
  display.drawRect(122, 1, 6, 8, WHITE);
  display.drawLine(124, 0, 125, 0, WHITE);

  if (battVolts > BATT_FULL) {
    display.fillRect(123, 1, 4, 7, WHITE);   // full
  } else if (battVolts > BATT_MED) {
    display.fillRect(123, 4, 4, 5, WHITE);   // medium
  } else if (battVolts > BATT_LOW) {
    display.fillRect(123, 6, 4, 3, WHITE);   // low
  }
  // else: empty – draw nothing inside the rectangle

  // ── Metering mode icon ──
  display.setCursor(0, 1);
  display.print(meteringMode == 0 ? F("A") : F("F"));

  // ── Lux reading ──
  display.setCursor(72, 1);
  display.print(F("lx:"));
  display.print(lux, 0);

  // ── EV ──
  display.drawLine(95, linePos[0] - 1, 95, linePos[0] + 17, WHITE);
  display.setTextSize(1);
  display.setCursor(100, linePos[0]);
  display.print(F("EV:"));
  display.setCursor(100, linePos[0] + 10);
  display.println(lux > 0 ? EV : 0, 0);

  // ── ND filter indicator ──
  if (ndIndex > 0) {
    display.setTextSize(1);
    display.setCursor(0, 57);
    display.print(F("ND"));
    display.print(pow(2.0f, ndIndex), 0);
    display.print(F("="));
    display.println(ndStop / 10.0f, 1);
  }

  // ── Shutter speed ──
  display.setTextSize(2);
  display.setCursor(10, linePos[1]);
  display.print(F("T:"));

  if (Tdisplay == 0) {
    display.print(Tmin, 1); display.print(F("m"));
  } else if (Tdisplay == 1) {
    if (T > 0) {
      display.print(F("1/")); display.print(Tfr, 0);
    } else {
      outOfrange();
    }
  } else if (Tdisplay == 2) {
    display.print(T, 1); display.print(F("s"));
  }

  // ── Priority marker ──
  display.setTextSize(1);
  display.setCursor(0, linePos[modeIndex] + 5);
  display.print(F("*"));

  display.display();
}

void showISOMenu() {
  ISOMenu    = true;
  NDMenu     = false;
  mainScreen = false;

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(50, 4);
  display.println(F("ISO"));
  display.setTextSize(3);

  long iso = getISOByIndex(ISOIndex);

  int cx = 50;
  if      (iso > 999999) cx =  0;
  else if (iso >  99999) cx = 10;
  else if (iso >   9999) cx = 20;
  else if (iso >    999) cx = 30;
  else if (iso >     99) cx = 40;

  display.setCursor(cx, 40);
  display.print(iso);
  display.display();
  delay(200);
}

void showNDMenu() {
  ISOMenu    = false;
  mainScreen = false;
  NDMenu     = true;

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(10, 4);
  display.println(F("ND Filter"));
  display.setTextSize(3);

  int cx = 40;
  if      (ndIndex > 9) cx = 10;
  else if (ndIndex > 6) cx = 20;
  else if (ndIndex > 3) cx = 30;

  if (ndIndex > 0) {
    display.setCursor(cx, 40);
    display.print(F("ND"));
    display.print(pow(2.0f, ndIndex), 0);
  } else {
    display.setTextSize(2);
    display.setCursor(10, 40);
    display.print(F("No filter"));
  }

  display.display();
  delay(200);
}

// ── Navigation menu ───────────────────────────────────────────────────────────
void menu() {
  if (MenuButtonState == LOW) {
    if (mainScreen)    showISOMenu();
    else if (ISOMenu)  showNDMenu();
    else             { refresh(); delay(200); }
  }

  if (NDMenu) {
    if (PlusButtonState == LOW) {
      ndIndex++;
      if (ndIndex > MaxNDIndex) ndIndex = 0;
    } else if (MinusButtonState == LOW) {
      ndIndex = (ndIndex <= 0) ? MaxNDIndex : ndIndex - 1;
    }
    if (PlusButtonState == LOW || MinusButtonState == LOW) { settingsDirty = true; showNDMenu(); }
  }

  if (ISOMenu) {
    if (PlusButtonState == LOW) {
      ISOIndex++;
      if (ISOIndex > MaxISOIndex) ISOIndex = 0;
    } else if (MinusButtonState == LOW) {
      ISOIndex = (ISOIndex > 0) ? ISOIndex - 1 : MaxISOIndex;
    }
    if (PlusButtonState == LOW || MinusButtonState == LOW) { settingsDirty = true; showISOMenu(); }
  }

  if (ModeButtonState == LOW) {
    if (mainScreen) {
      modeIndex++;
      if (modeIndex > 1) modeIndex = 0;
      settingsDirty = true;
    }
    refresh();
    delay(200);
  }

  if (mainScreen && MeteringModeButtonState == LOW) {
    meteringMode = (meteringMode == 0) ? 1 : 0;
    settingsDirty = true;
    refresh();
    delay(200);
  }

  if (mainScreen && (PlusButtonState == LOW || MinusButtonState == LOW)) {
    if (modeIndex == 0) {
      // Aperture priority
      if (PlusButtonState == LOW) {
        apertureIndex++;
        if (apertureIndex > MaxApertureIndex) apertureIndex = 0;
      } else {
        apertureIndex = (apertureIndex > 0) ? apertureIndex - 1 : MaxApertureIndex;
      }
    } else {
      // Shutter priority
      if (PlusButtonState == LOW) {
        T_expIndex++;
        if (T_expIndex > MaxTimeIndex) T_expIndex = 0;
      } else {
        T_expIndex = (T_expIndex > 0) ? T_expIndex - 1 : MaxTimeIndex;
      }
    }
    settingsDirty = true;
    delay(200);
    refresh();
  }
}

// ── Button reading ────────────────────────────────────────────────────────────
void readButtons() {
  PlusButtonState         = digitalRead(PlusButtonPin);
  MinusButtonState        = digitalRead(MinusButtonPin);
  MeteringButtonState     = digitalRead(MeteringButtonPin);
  ModeButtonState         = digitalRead(ModeButtonPin);
  MenuButtonState         = digitalRead(MenuButtonPin);
  MeteringModeButtonState = digitalRead(MeteringModeButtonPin);
}
