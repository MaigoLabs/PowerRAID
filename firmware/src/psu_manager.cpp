#include "psu_manager.h"
#include "util.h"

extern MonitorState monitor;

LiteonPsu psu(Wire, LiteonPsu::monitoringAddress7(kLiteonAddPins));
LiteonPsu psu2(Wire, LiteonPsu::monitoringAddress7(0b001));
LiteonPsu psu3(Wire, LiteonPsu::monitoringAddress7(0b010));

PsuSlot psuSlots[kPsuSlotCount] = {
    {1, "PSU1", PIN_PSU1_PRESENT_N, PIN_PSU1_PSOK, PIN_PSU1_PSON_N, &psu, false, false,
     LiteonResult::I2cNack, LiteonResult::I2cNack, 0, 0, 0, 0},
    {2, "PSU2", PIN_PSU2_PRESENT_N, PIN_PSU2_PSOK, PIN_PSU2_PSON_N, &psu2, false, false,
     LiteonResult::I2cNack, LiteonResult::I2cNack, 0, 0, 0, 0},
    {3, "PSU3", PIN_PSU3_PRESENT_N, PIN_PSU3_PSOK, PIN_PSU3_PSON_N, &psu3, false, false,
     LiteonResult::I2cNack, LiteonResult::I2cNack, 0, 0, 0, 0},
};

AtxState atx;
bool liteonAddressDetected = false;

PsuSlot &defaultPsuSlot() {
  return psuSlots[0];
}

uint8_t psuSlotIndex(const PsuSlot &slot) {
  return static_cast<uint8_t>(slot.id - 1);
}

PsuSlot *findPsuSlot(uint8_t id) {
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (psuSlots[index].id == id) {
      return &psuSlots[index];
    }
  }
  return nullptr;
}

bool parsePsuSlotToken(const char *token, PsuSlot *&slot) {
  if (token == nullptr) {
    return false;
  }

  if ((token[0] == 'p' || token[0] == 'P') &&
      (token[1] == 's' || token[1] == 'S') &&
      (token[2] == 'u' || token[2] == 'U')) {
    token += 3;
  } else if ((token[0] == 's' || token[0] == 'S') &&
             (token[1] == 'l' || token[1] == 'L') &&
             (token[2] == 'o' || token[2] == 'O') &&
             (token[3] == 't' || token[3] == 'T')) {
    token += 4;
  }

  if (*token == '=' || *token == ':' || *token == '#') {
    ++token;
  }

  uint8_t slotId = 0;
  if (!parseU8Token(token, slotId)) {
    return false;
  }

  slot = findPsuSlot(slotId);
  return slot != nullptr;
}

bool slotIsPresent(const PsuSlot &slot) {
  return digitalRead(slot.presentPin) == LOW;
}

bool psStatusOutputOk(uint16_t psStatus) {
  return (psStatus & (1U << 0)) != 0;
}

bool psStatusInputOk(uint16_t psStatus) {
  return (psStatus & (1U << 1)) != 0;
}

bool psStatusPsonAsserted(uint16_t psStatus) {
  return (psStatus & (1U << 2)) != 0;
}

LiteonResult probeLiteonAddressRange(uint8_t &selectedAddress7, uint16_t &ucInfo) {
  LiteonResult lastNonNack = LiteonResult::I2cNack;
  bool sawNonNack = false;
  const uint8_t originalAddress7 = psu.address7();

  for (uint8_t addPins = 0; addPins < kLiteonAddressPinCount; ++addPins) {
    const uint8_t address7 = LiteonPsu::monitoringAddress7(addPins);
    psu.setAddress7(address7);
    uint16_t value = 0;
    const LiteonResult result = psu.readRaw(LiteonRegister::UcInfo, value);

    if (result == LiteonResult::Ok) {
      selectedAddress7 = address7;
      ucInfo = value;
      return result;
    }
    if (result != LiteonResult::I2cNack) {
      sawNonNack = true;
      lastNonNack = result;
    }
  }

  psu.setAddress7(originalAddress7);
  return sawNonNack ? lastNonNack : LiteonResult::I2cNack;
}

bool probeAndSelectLiteonAddress() {
  uint8_t selectedAddress7 = psu.address7();
  uint16_t ucInfo = 0;
  const LiteonResult result = probeLiteonAddressRange(selectedAddress7, ucInfo);
  if (result != LiteonResult::Ok) {
    liteonAddressDetected = false;
    defaultPsuSlot().detected = false;
    return false;
  }

  psu.setAddress7(selectedAddress7);
  liteonAddressDetected = true;
  defaultPsuSlot().detected = true;
  return true;
}

bool ensureLiteonAddressSelected() {
  if (liteonAddressDetected) {
    return true;
  }
  return probeAndSelectLiteonAddress();
}

void clearSlotDetections() {
  liteonAddressDetected = false;
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    psuSlots[index].detected = false;
  }
}

LiteonResult probePsuSlot(PsuSlot &slot, uint16_t &ucInfo) {
  uint16_t value = 0;
  const LiteonResult result = slot.device->readRaw(LiteonRegister::UcInfo, value);
  if (result == LiteonResult::Ok) {
    ucInfo = value;
    slot.detected = true;
    if (slot.id == 1) {
      liteonAddressDetected = true;
    }
  } else {
    slot.detected = false;
    if (slot.id == 1) {
      liteonAddressDetected = false;
    }
  }
  return result;
}

bool ensurePsuSlotDetected(PsuSlot &slot) {
  if (slot.id == 1 && liteonAddressDetected) {
    slot.detected = true;
    return true;
  }
  if (slot.detected) {
    return true;
  }

  uint16_t ucInfo = 0;
  return probePsuSlot(slot, ucInfo) == LiteonResult::Ok;
}

void setAllPsuCadence(bool respectSequenceSpacing) {
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    psuSlots[index].device->setRespectSequenceSpacing(respectSequenceSpacing);
  }
}

bool i2cEnableIsHigh() {
  return digitalRead(PIN_I2C_ENABLE) == HIGH;
}

void setI2cEnable(bool enabled) {
  digitalWrite(PIN_I2C_ENABLE, enabled ? HIGH : LOW);
  if (!enabled) {
    clearSlotDetections();
    monitor.cycleRunning = false;
  }
}

void configurePsuGpios() {
  pinMode(PIN_RUN_LED, OUTPUT);
  digitalWrite(PIN_RUN_LED, kLedOff);
  pinMode(PIN_WIFI_STATUS_LED, OUTPUT);
  digitalWrite(PIN_WIFI_STATUS_LED, kLedOff);
  pinMode(kPowerLimitLedPin, OUTPUT);
  digitalWrite(kPowerLimitLedPin, kLedOff);
  pinMode(kPowerFaultLedPin, OUTPUT);
  digitalWrite(kPowerFaultLedPin, kLedOff);

  pinMode(PIN_I2C_ENABLE, OUTPUT);
  setI2cEnable(true);

  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    PsuSlot &slot = psuSlots[index];
    pinMode(slot.psonPin, OUTPUT_OPEN_DRAIN);
    slot.psonAsserted = false;
    digitalWrite(slot.psonPin, HIGH);

    pinMode(slot.psokPin, INPUT);
    pinMode(slot.presentPin, INPUT_PULLUP);
  }
  pinMode(PIN_PSU_INTERRUPT_N, INPUT_PULLUP);

  // ATX interface. PS_ON# and the converter enables are open-drain so the
  // motherboard, the module pull-ups and the MCU share the node without contention.
  pinMode(PIN_ATX_PS_ON_N, INPUT);              // R612 pulls up to +3.3VS
  pinMode(PIN_ATX_5V_EN_N, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_ATX_3V3_EN_N, OUTPUT_OPEN_DRAIN);
  pinMode(PIN_ATX_5V_PG, INPUT);
  pinMode(PIN_ATX_3V3_PG, INPUT);
  digitalWrite(PIN_ATX_5V_EN_N, LOW);           // hold off until main rails are ready
  digitalWrite(PIN_ATX_3V3_EN_N, LOW);
  atx.rawAsserted = digitalRead(PIN_ATX_PS_ON_N) == LOW;
  atx.requestAsserted = atx.rawAsserted;
  atx.rawChangedMs = millis();
}

void playBootLedAnimation() {
  const uint8_t leds[] = {PIN_LED_1, PIN_LED_2, PIN_LED_3, PIN_LED_FAULT};
  const size_t numLeds = sizeof(leds) / sizeof(leds[0]);

  for (uint8_t pin : leds) {
    digitalWrite(pin, kLedOff);
  }
  delay(100);

  // Sequential sweep forward: LED1 -> LED2 -> LED3 -> FAULT
  for (size_t i = 0; i < numLeds; ++i) {
    digitalWrite(leds[i], kLedOn);
    delay(100);
    digitalWrite(leds[i], kLedOff);
  }

  // Sequential sweep backward: LED3 -> LED2 -> LED1
  for (int i = static_cast<int>(numLeds) - 2; i >= 0; --i) {
    digitalWrite(leds[i], kLedOn);
    delay(100);
    digitalWrite(leds[i], kLedOff);
  }

  delay(100);

  // Blink all LEDs together 3 times
  for (int i = 0; i < 3; ++i) {
    for (uint8_t pin : leds) {
      digitalWrite(pin, kLedOn);
    }
    delay(150);
    for (uint8_t pin : leds) {
      digitalWrite(pin, kLedOff);
    }
    delay(150);
  }
}

void applyAtxMainRails(bool enabled) {
  // NAE12S20-C EN pin is active-high (>1.27V enabled, <1.15V disabled).
  // Pull-down resistor R403/R503 (1.69k) + internal 10k pull-up to Vin form a divider.
  // Driving open-drain LOW pulls EN to GND (holding converter OFF).
  // Writing HIGH releases open-drain to high-Z, allowing pull-up to enable converter.
  digitalWrite(PIN_ATX_5V_EN_N, enabled ? HIGH : LOW);
  digitalWrite(PIN_ATX_3V3_EN_N, enabled ? HIGH : LOW);
}

bool serviceAtxPsOnRequest(uint32_t now) {
  const bool raw = digitalRead(PIN_ATX_PS_ON_N) == LOW;
  if (raw != atx.rawAsserted) {
    atx.rawAsserted = raw;
    atx.rawChangedMs = now;
  }
  if (atx.trackAutomatic && atx.requestAsserted != atx.rawAsserted &&
      now - atx.rawChangedMs >= kAtxDebounceMs) {
    atx.requestAsserted = atx.rawAsserted;
  }
  return atx.requestAsserted;
}

bool atxPwrOkAsserted() {
  return digitalRead(PIN_ATX_5V_EN_N) == HIGH &&
         digitalRead(PIN_ATX_5V_PG) == HIGH &&
         digitalRead(PIN_ATX_3V3_PG) == HIGH;
}

void setPsuSlotPsonAsserted(PsuSlot &slot, bool asserted) {
  if (slot.psonAsserted != asserted) {
    slot.psonChangedMs = millis();
  }
  slot.psonAsserted = asserted;
  digitalWrite(slot.psonPin, asserted ? LOW : HIGH);
}

void setAllPsuPsonAsserted(bool asserted) {
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    setPsuSlotPsonAsserted(psuSlots[index], asserted);
  }
}

uint8_t currentPsonMask() {
  uint8_t mask = 0;
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (psuSlots[index].psonAsserted) {
      mask |= static_cast<uint8_t>(1U << index);
    }
  }
  return mask & kAllPsuPsonMask;
}

void applyPowerPolicyPsonMask(uint8_t mask) {
  mask &= kAllPsuPsonMask;
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    setPsuSlotPsonAsserted(psuSlots[index], (mask & (1U << index)) != 0);
  }
  applyAtxMainRails(mask != 0);
}

String pinsJson(const PsuSlot &slot) {
  String body = "{";
  body += "\"slot\":";
  body += String(slot.id);
  body += ",\"name\":";
  body += jsonString(slot.name);
  body += ",\"address\":";
  body += jsonString(hex8(slot.device->address7()));
  body += ",\"detected\":";
  body += jsonBool(slot.detected);
  body += ",\"presentN\":";
  body += digitalRead(slot.presentPin);
  body += ",\"present\":";
  body += jsonBool(slotIsPresent(slot));
  body += ",\"psok\":";
  body += digitalRead(slot.psokPin);
  body += ",\"psInterruptN\":";
  body += digitalRead(PIN_PSU_INTERRUPT_N);
  body += ",\"interruptAsserted\":";
  body += jsonBool(digitalRead(PIN_PSU_INTERRUPT_N) == LOW);
  body += ",\"psonAsserted\":";
  body += jsonBool(slot.psonAsserted);
  body += ",\"psonNPin\":";
  body += digitalRead(slot.psonPin);
  body += ",\"i2cEnable\":";
  body += jsonBool(i2cEnableIsHigh());
  body += ",\"i2cEnablePin\":";
  body += digitalRead(PIN_I2C_ENABLE);
  body += "}";
  return body;
}

String psuSlotsJson() {
  String body = "[";
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (index > 0) {
      body += ',';
    }
    body += pinsJson(psuSlots[index]);
  }
  body += "]";
  return body;
}

String atxJson() {
  String body = "{";
  body += "\"psOnRequested\":";
  body += jsonBool(atx.requestAsserted);
  body += ",\"psOnPin\":";
  body += digitalRead(PIN_ATX_PS_ON_N);
  body += ",\"trackAutomatic\":";
  body += jsonBool(atx.trackAutomatic);
  body += ",\"mainRailsEnabled\":";
  body += jsonBool(digitalRead(PIN_ATX_5V_EN_N) == HIGH);
  body += ",\"enable5VPin\":";
  body += digitalRead(PIN_ATX_5V_EN_N);
  body += ",\"enable3V3Pin\":";
  body += digitalRead(PIN_ATX_3V3_EN_N);
  body += ",\"pwrOk\":";
  body += jsonBool(atxPwrOkAsserted());
  body += ",\"pg5V\":";
  body += jsonBool(digitalRead(PIN_ATX_5V_PG) == HIGH);
  body += ",\"pg3V3\":";
  body += jsonBool(digitalRead(PIN_ATX_3V3_PG) == HIGH);
  body += "}";
  return body;
}

String i2cEnableJson() {
  String body = "{";
  body += "\"gpio\":";
  body += PIN_I2C_ENABLE;
  body += ",\"enabled\":";
  body += jsonBool(i2cEnableIsHigh());
  body += ",\"pin\":";
  body += digitalRead(PIN_I2C_ENABLE);
  body += "}";
  return body;
}
