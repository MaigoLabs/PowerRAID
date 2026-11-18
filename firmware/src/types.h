#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <cmath>
#include "config.h"
#include "liteon_psu.h"

struct PsuSlot {
  uint8_t id;
  const char *name;
  uint8_t presentPin;
  uint8_t psokPin;
  uint8_t psonPin;
  LiteonPsu *device;
  bool detected;
  bool psonAsserted;
  LiteonResult lastStatusResult;
  LiteonResult lastOutputPowerResult;
  uint16_t lastPsStatus;
  uint16_t lastOutputPowerW;
  uint32_t lastTelemetryMs;
  uint32_t psonChangedMs;
};

struct PowerPolicyState {
  bool automatic = true;
  uint8_t nRedundancy = kDefaultNRedundancy;
  uint32_t lastServiceMs = 0;
  uint32_t lastDecisionMs = 0;
  float totalOutputW = 0.0f;
  uint8_t presentCount = 0;
  uint8_t candidateCount = 0;
  uint8_t activeCount = 0;
  uint8_t targetActiveCount = 0;
  uint8_t nonRedundantCount = 0;
  uint16_t maxOutputW = 0;
  bool telemetryValid = false;
  bool limitExceeded = false;
  bool redundancyShortfall = false;
};

struct AtxState {
  bool requestAsserted = false;   // debounced PS_ON# request from the motherboard
  bool rawAsserted = false;       // most recent sample of the PS_ON# pin
  uint32_t rawChangedMs = 0;      // when the raw sample last changed
  bool trackAutomatic = true;     // follow PS_ON#; false after a manual /atx override
};

struct MonitorState {
  bool active = false;
  bool cycleRunning = false;
  bool allSlots = false;
  uint8_t slotIndex = 0;
  uint8_t currentSlotIndex = 0;
  uint16_t intervalMs = kMonitorDefaultIntervalMs;
  uint8_t registers[kMonitorMaxRegisters] = {};
  uint8_t registerCount = 0;
  uint8_t registerIndex = 0;
  uint32_t cycle = 0;
  uint32_t cycleStartMs = 0;
  uint32_t nextCycleStartMs = 0;
};

struct InfluxState {
  bool enabled = false;
  String url;
  String org = "powerraid";
  String bucket = "zigbee";
  String token;
  String measurement = "powerraid_psu";
  uint32_t intervalMs = kInfluxDefaultIntervalMs;
  uint32_t nextReportMs = 0;
  uint32_t lastAttemptMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t lastUploadDurationMs = 0;
  uint32_t maxUploadDurationMs = 0;
  uint32_t writeCount = 0;
  uint32_t errorCount = 0;
  uint32_t slowUploadCount = 0;
  int lastHttpStatus = 0;
  String lastResult = "idle";
  String lastError;
};

struct InfluxPsuSample {
  bool active = false;
  bool pendingWrite = false;
  uint8_t slotIndex = 0;
  uint8_t registerIndex = 0;
  uint32_t startedMs = 0;
  uint32_t readyMs = 0;
  uint32_t nextRetryMs = 0;
  LiteonStatus status;
  LiteonTelemetry telemetry;
};

struct InfluxImonPoint {
  uint32_t sampleCount = 0;
  uint32_t sampleMs = 0;
  uint32_t windowMs = 0;
  uint32_t sequence = 0;
  float rawAvg = NAN;
  float adcVoltageVAvg = NAN;
  float measuredImonVoltageVAvg = NAN;
  float imonVoltageVAvg = NAN;
  float currentAAvg = NAN;
  uint16_t rawMin = 0;
  uint16_t rawMax = 0;
  uint8_t attenuationDb = 0;
};

struct InfluxUploadJob {
  String url;
  String org;
  String bucket;
  String token;
  String payload;
  uint16_t pointCount = 0;
  uint16_t imonPointCount = 0;
  uint32_t firstImonSequence = 0;
  bool includedSlots[kPsuSlotCount] = {};
};

struct InfluxUploadCompletion {
  uint16_t imonPointCount = 0;
  uint32_t firstImonSequence = 0;
  bool includedSlots[kPsuSlotCount] = {};
};

struct ImonWindow {
  uint32_t sampleCount = 0;
  uint64_t rawSum = 0;
  uint64_t milliVoltSum = 0;
  uint16_t rawMin = UINT16_MAX;
  uint16_t rawMax = 0;
  uint16_t lastRaw = 0;
  uint16_t lastMilliVolts = 0;
  uint8_t lastAttenuationDb = 0;
  uint32_t windowStartMs = 0;
  uint32_t lastSampleMs = 0;
};

struct ImonSnapshot {
  bool valid = false;
  uint32_t sampleCount = 0;
  uint32_t windowMs = 0;
  float rawAvg = NAN;
  float adcVoltageVAvg = NAN;
  float measuredImonVoltageVAvg = NAN;
  float imonVoltageVAvg = NAN;
  float currentAAvg = NAN;
  uint16_t rawMin = 0;
  uint16_t rawMax = 0;
  uint8_t attenuationDb = 0;
};

struct ImonAdcReading {
  uint16_t raw = 0;
  uint16_t milliVolts = 0;
  uint8_t attenuationDb = 0;
};
