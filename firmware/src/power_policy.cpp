#include "power_policy.h"
#include <Preferences.h>
#include <math.h>
#include <string.h>
#include "util.h"

extern bool otaUploadRunning;
extern bool otaRestartPending;

static Preferences powerPrefs;
PowerPolicyState powerPolicy;
const EfficiencyProfile *efficiencyProfile = &kEfficiencyProfiles[kDefaultEfficiencyProfileIndex];

static void persistPowerPolicyPsonMask() {
  powerPrefs.putUInt("pson", currentPsonMask());
}

static uint8_t clampNRedundancy(uint32_t value) {
  return value > kMaxNRedundancy ? kMaxNRedundancy : static_cast<uint8_t>(value);
}

const EfficiencyProfile *findEfficiencyProfile(const char *id) {
  for (uint8_t index = 0; index < kEfficiencyProfileCount; ++index) {
    if (strcmp(id, kEfficiencyProfiles[index].id) == 0) {
      return &kEfficiencyProfiles[index];
    }
  }
  return nullptr;
}

bool setEfficiencyProfile(const char *id, bool persist) {
  const EfficiencyProfile *selected = findEfficiencyProfile(id);
  if (selected == nullptr) {
    return false;
  }
  efficiencyProfile = selected;
  if (persist) {
    powerPrefs.putString("effprof", efficiencyProfile->id);
  }
  powerPolicy.lastServiceMs = 0;
  return true;
}

void loadPowerPolicyPreferences() {
  powerPrefs.begin("pramon_power", false);
  powerPolicy.automatic = powerPrefs.getBool("auto", true);
  powerPolicy.nRedundancy = clampNRedundancy(powerPrefs.getUInt("nred", kDefaultNRedundancy));
  String storedProfile = powerPrefs.getString("effprof", kDefaultEfficiencyProfileId);
  const bool migratedToMeasuredVoltageProfile = powerPrefs.getBool("effp083", false);
  if (!migratedToMeasuredVoltageProfile) {
    if (storedProfile == kPreviousDefaultEfficiencyProfileId) {
      storedProfile = kDefaultEfficiencyProfileId;
      powerPrefs.putString("effprof", storedProfile);
    }
    powerPrefs.putBool("effp083", true);
  }
  if (!setEfficiencyProfile(storedProfile.c_str(), false)) {
    setEfficiencyProfile(kDefaultEfficiencyProfileId, false);
  }
  if (!powerPolicy.automatic) {
    applyPowerPolicyPsonMask(static_cast<uint8_t>(powerPrefs.getUInt("pson", 0)));
  }
}

void configurePowerPolicy() {
  loadPowerPolicyPreferences();
}

void setPowerPolicyAutomatic(bool automatic, bool persist) {
  powerPolicy.automatic = automatic;
  if (persist) {
    powerPrefs.putBool("auto", automatic);
    if (!automatic) {
      persistPowerPolicyPsonMask();
    }
  }
  powerPolicy.lastServiceMs = 0;
}

void setPowerPolicyManualPson(PsuSlot *slot, bool asserted, bool persist) {
  powerPolicy.automatic = false;
  if (slot == nullptr) {
    setAllPsuPsonAsserted(asserted);
  } else {
    setPsuSlotPsonAsserted(*slot, asserted);
  }
  applyAtxMainRails(currentPsonMask() != 0);
  if (persist) {
    powerPrefs.putBool("auto", false);
    persistPowerPolicyPsonMask();
  }
  powerPolicy.lastServiceMs = 0;
}

void setPowerPolicyNRedundancy(uint8_t nRedundancy, bool persist) {
  powerPolicy.nRedundancy = clampNRedundancy(nRedundancy);
  if (persist) {
    powerPrefs.putUInt("nred", powerPolicy.nRedundancy);
  }
  powerPolicy.lastServiceMs = 0;
}

const EfficiencyCurve &efficiencyCurveForActiveCount(uint8_t activeCount) {
  const uint8_t index = activeCount == 0 ? 0 : activeCount > kPsuSlotCount ? kPsuSlotCount - 1 : activeCount - 1;
  return efficiencyProfile->curves[index];
}

static float interpolatedEfficiencyPercent(float loadPercent, uint8_t activeCount) {
  const EfficiencyCurve &curve = efficiencyCurveForActiveCount(activeCount);
  if (loadPercent <= curve.points[0].loadPercent) {
    return curve.points[0].efficiencyPercent;
  }

  for (uint8_t index = 1; index < curve.pointCount; ++index) {
    const EfficiencyCurvePoint &left = curve.points[index - 1];
    const EfficiencyCurvePoint &right = curve.points[index];
    if (loadPercent <= right.loadPercent) {
      const float span = right.loadPercent - left.loadPercent;
      const float ratio = span <= 0.0f ? 0.0f : (loadPercent - left.loadPercent) / span;
      return left.efficiencyPercent + (right.efficiencyPercent - left.efficiencyPercent) * ratio;
    }
  }

  return curve.points[curve.pointCount - 1].efficiencyPercent;
}

static float estimatedInputPowerW(float outputPowerW, uint8_t activeCount) {
  if (outputPowerW <= 0.0f || activeCount == 0) {
    return 0.0f;
  }

  const float loadPercent = (outputPowerW * 100.0f) /
      (static_cast<float>(kPsuRatedOutputWatts) * activeCount);
  const float efficiencyPercent = interpolatedEfficiencyPercent(loadPercent, activeCount);
  if (efficiencyPercent <= 0.0f) {
    return 1000000000.0f;
  }
  return outputPowerW / (efficiencyPercent / 100.0f);
}

static uint8_t psuCountForOutputPower(float outputPowerW) {
  if (outputPowerW <= 0.0f) {
    return 1;
  }
  return static_cast<uint8_t>(ceilf(outputPowerW / static_cast<float>(kPsuRatedOutputWatts)));
}

bool slotPowerPolicyFailed(const PsuSlot &slot, uint32_t now) {
  if (!slotIsPresent(slot) || !slot.psonAsserted) {
    return false;
  }
  if (now - slot.psonChangedMs < kPowerPolicyStartupGraceMs) {
    return false;
  }

  if (slot.lastStatusResult == LiteonResult::Ok) {
    if (!psStatusInputOk(slot.lastPsStatus)) {
      return true;
    }
    if (psStatusPsonAsserted(slot.lastPsStatus) && psStatusOutputOk(slot.lastPsStatus)) {
      return false;
    }
    return digitalRead(slot.psokPin) != HIGH;
  }

  return digitalRead(slot.psokPin) != HIGH;
}

bool slotPowerPolicyCandidate(const PsuSlot &slot, uint32_t now) {
  if (!slotIsPresent(slot)) {
    return false;
  }
  if (slot.lastStatusResult == LiteonResult::Ok && !psStatusInputOk(slot.lastPsStatus)) {
    return false;
  }
  return !slotPowerPolicyFailed(slot, now);
}

static void samplePowerPolicySlot(PsuSlot &slot) {
  slot.lastTelemetryMs = millis();
  slot.lastStatusResult = slot.device->readRaw(LiteonRegister::PsStatus, slot.lastPsStatus);
  if (slot.lastStatusResult == LiteonResult::Ok) {
    slot.detected = true;
    if (slot.id == 1) {
      liteonAddressDetected = true;
    }
  } else if (slot.lastStatusResult == LiteonResult::I2cNack) {
    slot.detected = false;
    if (slot.id == 1) {
      liteonAddressDetected = false;
    }
  }

  const bool readOutputPower = slot.psonAsserted ||
      (slot.lastStatusResult == LiteonResult::Ok && psStatusOutputOk(slot.lastPsStatus)) ||
      digitalRead(slot.psokPin) == HIGH;
  if (!readOutputPower) {
    slot.lastOutputPowerW = 0;
    slot.lastOutputPowerResult = LiteonResult::Ok;
    return;
  }

  uint16_t outputPowerW = 0;
  slot.lastOutputPowerResult = slot.device->readRaw(LiteonRegister::OutputPower, outputPowerW);
  if (slot.lastOutputPowerResult == LiteonResult::Ok) {
    slot.lastOutputPowerW = outputPowerW;
  } else {
    slot.lastOutputPowerW = 0;
    if (slot.lastOutputPowerResult == LiteonResult::I2cNack) {
      slot.detected = false;
      if (slot.id == 1) {
        liteonAddressDetected = false;
      }
    }
  }
}

static uint8_t choosePowerPolicyActiveCount(uint8_t candidateCount, float outputPowerW) {
  if (candidateCount == 0) {
    return 0;
  }

  const uint8_t neededForLoad = psuCountForOutputPower(outputPowerW);
  uint8_t minActive = neededForLoad + powerPolicy.nRedundancy;
  if (minActive < 1) {
    minActive = 1;
  }
  if (minActive > candidateCount) {
    minActive = candidateCount;
  }

  uint8_t bestCount = minActive;
  float bestInputPowerW = 1000000000.0f;
  bool foundFeasibleCount = false;
  for (uint8_t activeCount = minActive; activeCount <= candidateCount; ++activeCount) {
    const float capacityW = static_cast<float>(activeCount) * kPsuRatedOutputWatts;
    if (outputPowerW > capacityW + 1.0f) {
      continue;
    }
    const float inputPowerW = estimatedInputPowerW(outputPowerW, activeCount);
    if (!foundFeasibleCount || inputPowerW < bestInputPowerW - 0.05f) {
      foundFeasibleCount = true;
      bestInputPowerW = inputPowerW;
      bestCount = activeCount;
    }
  }

  return foundFeasibleCount ? bestCount : candidateCount;
}

static void updatePowerPolicyMeasurements(uint32_t now) {
  powerPolicy.totalOutputW = 0.0f;
  powerPolicy.presentCount = 0;
  powerPolicy.candidateCount = 0;
  powerPolicy.activeCount = 0;
  powerPolicy.telemetryValid = false;

  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    PsuSlot &slot = psuSlots[index];
    if (!slotIsPresent(slot)) {
      slot.detected = false;
      slot.lastOutputPowerW = 0;
      slot.lastStatusResult = LiteonResult::I2cNack;
      slot.lastOutputPowerResult = LiteonResult::I2cNack;
      continue;
    }

    ++powerPolicy.presentCount;
    samplePowerPolicySlot(slot);
    if (slot.psonAsserted) {
      ++powerPolicy.activeCount;
    }
    if (slot.lastOutputPowerResult == LiteonResult::Ok) {
      powerPolicy.telemetryValid = true;
      powerPolicy.totalOutputW += static_cast<float>(slot.lastOutputPowerW);
    }
    if (slotPowerPolicyCandidate(slot, now)) {
      ++powerPolicy.candidateCount;
    }
  }

  const uint8_t capacityCount = powerPolicy.candidateCount;
  powerPolicy.nonRedundantCount = capacityCount > powerPolicy.nRedundancy
      ? static_cast<uint8_t>(capacityCount - powerPolicy.nRedundancy)
      : 0;
  powerPolicy.maxOutputW = static_cast<uint16_t>(powerPolicy.nonRedundantCount * kPsuRatedOutputWatts);
  powerPolicy.limitExceeded = powerPolicy.totalOutputW > static_cast<float>(powerPolicy.maxOutputW) + 1.0f;
  powerPolicy.targetActiveCount = choosePowerPolicyActiveCount(powerPolicy.candidateCount, powerPolicy.totalOutputW);

  const uint8_t neededForLoad = psuCountForOutputPower(powerPolicy.totalOutputW);
  powerPolicy.redundancyShortfall =
      powerPolicy.candidateCount < static_cast<uint8_t>(neededForLoad + powerPolicy.nRedundancy);
}

static void applyPowerPolicyDecision(uint32_t now) {
  if (!powerPolicy.automatic) {
    return;
  }

  // The motherboard owns the main rails through PS_ON#. When it is released,
  // every PSU PSON# is released and the ATX main-rail converters are disabled;
  // the +12VSB standby rail stays up so the MCU and the LiteOn buses survive.
  if (!atx.requestAsserted) {
    setAllPsuPsonAsserted(false);
    applyAtxMainRails(false);
    powerPolicy.activeCount = 0;
    powerPolicy.lastDecisionMs = now;
    return;
  }

  bool desired[kPsuSlotCount] = {};
  uint8_t desiredCount = 0;

  for (uint8_t index = 0; index < kPsuSlotCount && desiredCount < powerPolicy.targetActiveCount; ++index) {
    const PsuSlot &slot = psuSlots[index];
    if (slot.psonAsserted && slotPowerPolicyCandidate(slot, now)) {
      desired[index] = true;
      ++desiredCount;
    }
  }

  for (uint8_t index = 0; index < kPsuSlotCount && desiredCount < powerPolicy.targetActiveCount; ++index) {
    const PsuSlot &slot = psuSlots[index];
    if (!desired[index] && slotPowerPolicyCandidate(slot, now)) {
      desired[index] = true;
      ++desiredCount;
    }
  }

  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    setPsuSlotPsonAsserted(psuSlots[index], desired[index]);
  }
  // Main rails follow the request once at least one supply is being turned on.
  applyAtxMainRails(desiredCount > 0);
  powerPolicy.activeCount = desiredCount;
  powerPolicy.lastDecisionMs = now;
}

void servicePowerPolicyLeds() {
  const bool overLimit = powerPolicy.limitExceeded;
  digitalWrite(kPowerLimitLedPin, overLimit ? kLedOn : kLedOff);
  digitalWrite(kPowerFaultLedPin, overLimit ? kLedOn : kLedOff);
}

void servicePowerPolicy(bool force) {
  if (otaUploadRunning || otaRestartPending) {
    servicePowerPolicyLeds();
    return;
  }

  const uint32_t now = millis();
  if (!force && powerPolicy.lastServiceMs != 0 && now - powerPolicy.lastServiceMs < kPowerPolicyIntervalMs) {
    servicePowerPolicyLeds();
    return;
  }
  powerPolicy.lastServiceMs = now;

  if (!i2cEnableIsHigh()) {
    powerPolicy.telemetryValid = false;
    powerPolicy.limitExceeded = false;
    servicePowerPolicyLeds();
    return;
  }

  serviceAtxPsOnRequest(now);
  updatePowerPolicyMeasurements(now);
  applyPowerPolicyDecision(now);
  servicePowerPolicyLeds();
}

String powerPolicySlotJson(const PsuSlot &slot, uint32_t now) {
  String body = "{";
  body += "\"slot\":";
  body += String(slot.id);
  body += ",\"present\":";
  body += jsonBool(slotIsPresent(slot));
  body += ",\"psonAsserted\":";
  body += jsonBool(slot.psonAsserted);
  body += ",\"psok\":";
  body += digitalRead(slot.psokPin);
  body += ",\"statusResult\":";
  body += jsonString(resultName(slot.lastStatusResult));
  body += ",\"outputPowerResult\":";
  body += jsonString(resultName(slot.lastOutputPowerResult));
  body += ",\"psStatus\":";
  body += jsonString(hex16(slot.lastPsStatus));
  body += ",\"inputOk\":";
  body += jsonBool(slot.lastStatusResult == LiteonResult::Ok && psStatusInputOk(slot.lastPsStatus));
  body += ",\"outputOk\":";
  body += jsonBool(slot.lastStatusResult == LiteonResult::Ok && psStatusOutputOk(slot.lastPsStatus));
  body += ",\"psuPsonAsserted\":";
  body += jsonBool(slot.lastStatusResult == LiteonResult::Ok && psStatusPsonAsserted(slot.lastPsStatus));
  body += ",\"outputPowerW\":";
  body += String(slot.lastOutputPowerW);
  body += ",\"candidate\":";
  body += jsonBool(slotPowerPolicyCandidate(slot, now));
  body += ",\"failed\":";
  body += jsonBool(slotPowerPolicyFailed(slot, now));
  body += ",\"startupGrace\":";
  body += jsonBool(slot.psonAsserted && now - slot.psonChangedMs < kPowerPolicyStartupGraceMs);
  body += "}";
  return body;
}

String powerPolicySlotsJson(uint32_t now) {
  String body = "[";
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (index > 0) {
      body += ',';
    }
    body += powerPolicySlotJson(psuSlots[index], now);
  }
  body += "]";
  return body;
}

String efficiencyCurveJson(const EfficiencyCurve &curve) {
  String body = "[";
  for (uint8_t index = 0; index < curve.pointCount; ++index) {
    if (index > 0) {
      body += ',';
    }
    body += "{\"loadPercent\":";
    body += String(curve.points[index].loadPercent, 3);
    body += ",\"efficiencyPercent\":";
    body += String(curve.points[index].efficiencyPercent, 3);
    body += "}";
  }
  body += "]";
  return body;
}

String efficiencyCurvesJson() {
  String body = "[";
  for (uint8_t activeCount = 1; activeCount <= kPsuSlotCount; ++activeCount) {
    if (activeCount > 1) {
      body += ',';
    }
    body += "{\"activeCount\":";
    body += String(activeCount);
    body += ",\"points\":";
    body += efficiencyCurveJson(efficiencyCurveForActiveCount(activeCount));
    body += "}";
  }
  body += "]";
  return body;
}

String availableEfficiencyProfilesJson() {
  String body = "[";
  for (uint8_t index = 0; index < kEfficiencyProfileCount; ++index) {
    if (index > 0) {
      body += ',';
    }
    body += "{\"id\":";
    body += jsonString(kEfficiencyProfiles[index].id);
    body += ",\"label\":";
    body += jsonString(kEfficiencyProfiles[index].label);
    body += "}";
  }
  body += "]";
  return body;
}

String powerPolicyJson() {
  const uint32_t now = millis();
  const uint8_t curveActiveCount = powerPolicy.targetActiveCount == 0 ? 1 : powerPolicy.targetActiveCount;
  String body = "{";
  body += "\"automatic\":";
  body += jsonBool(powerPolicy.automatic);
  body += ",\"n_redundancy\":";
  body += String(powerPolicy.nRedundancy);
  body += ",\"psuWatts\":";
  body += String(kPsuRatedOutputWatts);
  body += ",\"presentCount\":";
  body += String(powerPolicy.presentCount);
  body += ",\"candidateCount\":";
  body += String(powerPolicy.candidateCount);
  body += ",\"activeCount\":";
  body += String(powerPolicy.activeCount);
  body += ",\"targetActiveCount\":";
  body += String(powerPolicy.targetActiveCount);
  body += ",\"nonRedundantCount\":";
  body += String(powerPolicy.nonRedundantCount);
  body += ",\"maxOutputW\":";
  body += String(powerPolicy.maxOutputW);
  body += ",\"totalOutputW\":";
  body += String(powerPolicy.totalOutputW, 1);
  body += ",\"telemetryValid\":";
  body += jsonBool(powerPolicy.telemetryValid);
  body += ",\"limitExceeded\":";
  body += jsonBool(powerPolicy.limitExceeded);
  body += ",\"redundancyShortfall\":";
  body += jsonBool(powerPolicy.redundancyShortfall);
  body += ",\"lastServiceMs\":";
  body += String(powerPolicy.lastServiceMs);
  body += ",\"lastDecisionMs\":";
  body += String(powerPolicy.lastDecisionMs);
  body += ",\"efficiencyProfile\":";
  body += jsonString(efficiencyProfile->id);
  body += ",\"efficiencyProfileLabel\":";
  body += jsonString(efficiencyProfile->label);
  body += ",\"efficiencyProfileSource\":";
  body += jsonString(efficiencyProfile->source);
  body += ",\"availableEfficiencyProfiles\":";
  body += availableEfficiencyProfilesJson();
  body += ",\"efficiencyCurveActiveCount\":";
  body += String(curveActiveCount);
  body += ",\"efficiencyCurve\":";
  body += efficiencyCurveJson(efficiencyCurveForActiveCount(curveActiveCount));
  body += ",\"efficiencyCurves\":";
  body += efficiencyCurvesJson();
  body += ",\"slots\":";
  body += powerPolicySlotsJson(now);
  body += "}";
  return body;
}
