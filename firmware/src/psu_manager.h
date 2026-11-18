#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "types.h"
#include "liteon_psu.h"

// Hardware Devices and Slot State
extern LiteonPsu psu;
extern LiteonPsu psu2;
extern LiteonPsu psu3;
extern PsuSlot psuSlots[kPsuSlotCount];
extern AtxState atx;
extern bool liteonAddressDetected;

// Slot Accessors and Queries
PsuSlot &defaultPsuSlot();
uint8_t psuSlotIndex(const PsuSlot &slot);
PsuSlot *findPsuSlot(uint8_t id);
bool parsePsuSlotToken(const char *token, PsuSlot *&slot);
bool slotIsPresent(const PsuSlot &slot);

// Status Bit Utilities
bool psStatusOutputOk(uint16_t psStatus);
bool psStatusInputOk(uint16_t psStatus);
bool psStatusPsonAsserted(uint16_t psStatus);

// Hardware Initialization & GPIO Controls
void configurePsuGpios();
void playBootLedAnimation();
void setI2cEnable(bool enabled);
bool i2cEnableIsHigh();
void setAllPsuCadence(bool respectSequenceSpacing);

// I2C Probing and Slot Detection
LiteonResult probeLiteonAddressRange(uint8_t &selectedAddress7, uint16_t &ucInfo);
bool probeAndSelectLiteonAddress();
bool ensureLiteonAddressSelected();
void clearSlotDetections();
LiteonResult probePsuSlot(PsuSlot &slot, uint16_t &ucInfo);
bool ensurePsuSlotDetected(PsuSlot &slot);

// PSON Line Assertions
void setPsuSlotPsonAsserted(PsuSlot &slot, bool asserted);
void setAllPsuPsonAsserted(bool asserted);
uint8_t currentPsonMask();
void applyPowerPolicyPsonMask(uint8_t mask);

// ATX Main-Rail & Supervision
void applyAtxMainRails(bool enabled);
bool serviceAtxPsOnRequest(uint32_t now);
bool atxPwrOkAsserted();

// JSON Diagnostics
String pinsJson(const PsuSlot &slot);
String psuSlotsJson();
String atxJson();
String i2cEnableJson();
