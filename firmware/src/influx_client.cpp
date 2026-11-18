#include "influx_client.h"
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>

#include "util.h"
#include "wifi_manager.h"
#include "psu_manager.h"
#include "power_policy.h"

extern bool otaUploadRunning;
extern bool otaRestartPending;

static Preferences influxPrefs;
InfluxState influx;

static const LiteonRegister kInfluxPsuReadRegisters[] = {
    LiteonRegister::UcInfo,
    LiteonRegister::PsStatus,
    LiteonRegister::ShutdownEvent,
    LiteonRegister::WarningEvent,
    LiteonRegister::PsControl,
    LiteonRegister::InputVoltage,
    LiteonRegister::InputCurrent,
    LiteonRegister::InputPower,
    LiteonRegister::OutputVoltage,
    LiteonRegister::OutputCurrent,
    LiteonRegister::OutputPower,
    LiteonRegister::InletTemperature,
    LiteonRegister::InternalTemperature,
    LiteonRegister::Fan1Speed,
};
static constexpr uint8_t kInfluxPsuRegisterCount = sizeof(kInfluxPsuReadRegisters) / sizeof(kInfluxPsuReadRegisters[0]);

static InfluxPsuSample influxPsuSamples[kPsuSlotCount];
static InfluxImonPoint influxImonQueue[kInfluxImonQueueDepth];
static uint8_t influxImonQueueHead = 0;
static uint8_t influxImonQueueCount = 0;
static uint32_t influxImonDropped = 0;
static uint32_t influxNextImonSequence = 1;
static InfluxUploadJob influxUploadJob;
static QueueHandle_t influxUploadQueue = nullptr;
static QueueHandle_t influxUploadCompletionQueue = nullptr;
static TaskHandle_t influxUploadTaskHandle = nullptr;
static volatile bool influxUploadInFlight = false;

static ImonWindow imonWindow;

String normalizedInfluxUrl(String url) {
  url.trim();
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

bool influxUrlUsesHttps() {
  return influx.url.startsWith("https://") || influx.url.startsWith("HTTPS://");
}

bool urlUsesHttps(const String &url) {
  return url.startsWith("https://") || url.startsWith("HTTPS://");
}

void loadInfluxConfig() {
  influxPrefs.begin("pramon_influx", false);
  influx.enabled = influxPrefs.getBool("enabled", false);
  influx.url = normalizedInfluxUrl(influxPrefs.getString("url", ""));
  influx.org = influxPrefs.getString("org", "powerraid");
  influx.bucket = influxPrefs.getString("bucket", "zigbee");
  influx.token = influxPrefs.getString("token", "");
  influx.measurement = influxPrefs.getString("meas", "powerraid_psu");
  influx.intervalMs = clampInfluxInterval(influxPrefs.getUInt("interval", kInfluxDefaultIntervalMs));
  influx.nextReportMs = millis() + influx.intervalMs;
}

void saveInfluxConfig() {
  influxPrefs.putBool("enabled", influx.enabled);
  influxPrefs.putString("url", normalizedInfluxUrl(influx.url));
  influxPrefs.putString("org", influx.org);
  influxPrefs.putString("bucket", influx.bucket);
  influxPrefs.putString("token", influx.token);
  influxPrefs.putString("meas", influx.measurement);
  influxPrefs.putUInt("interval", influx.intervalMs);
}

bool influxConfigured() {
  return influx.url.length() > 0 &&
         influx.org.length() > 0 &&
         influx.bucket.length() > 0 &&
         influx.token.length() > 0;
}

static void appendInfluxFloatField(String &line, const char *name, float value, bool &first) {
  if (!isfinite(value)) {
    return;
  }
  appendInfluxFieldSeparator(line, first);
  line += name;
  line += '=';
  line += String(value, 3);
}

static void appendInfluxIntField(String &line, const char *name, int32_t value, bool &first) {
  appendInfluxFieldSeparator(line, first);
  line += name;
  line += '=';
  line += String(value);
  line += 'i';
}

static void appendInfluxBoolField(String &line, const char *name, bool value, bool &first) {
  appendInfluxFieldSeparator(line, first);
  line += name;
  line += '=';
  line += value ? "true" : "false";
}

static void appendInfluxStringField(String &line, const char *name, const String &value, bool &first) {
  appendInfluxFieldSeparator(line, first);
  line += name;
  line += "=\"";
  line += influxEscapeStringField(value);
  line += '"';
}

static float efficiencyPercent(float inputPowerW, float outputPowerW) {
  if (inputPowerW >= 20.0f && outputPowerW >= 36.0f && outputPowerW < inputPowerW) {
    return (outputPowerW / inputPowerW) * 100.0f;
  }
  return NAN;
}

static float imonAdcToSourceVoltage(float adcVoltageV) {
  return adcVoltageV * ((kImonDividerHighOhms + kImonDividerLowOhms) / kImonDividerLowOhms);
}

static float imonVoltageToCurrentA(float imonVoltageV) {
  const float currentA = imonVoltageV * (kImonRatedCurrentA / kImonRatedVoltageV) + kImonV1CurrentOffsetA;
  return currentA > 0.0f ? currentA : 0.0f;
}

static ImonAdcReading readImonAdc() {
  analogSetPinAttenuation(PIN_PSU_IMON_ADC, ADC_0db);
  delayMicroseconds(50);
  ImonAdcReading reading;
  reading.raw = static_cast<uint16_t>(analogRead(PIN_PSU_IMON_ADC));
  reading.milliVolts = static_cast<uint16_t>(analogReadMilliVolts(PIN_PSU_IMON_ADC));
  reading.attenuationDb = 0;

  if (reading.raw >= kImonLowRangeMaxRaw || reading.milliVolts >= kImonLowRangeMaxMilliVolts) {
    analogSetPinAttenuation(PIN_PSU_IMON_ADC, ADC_11db);
    delayMicroseconds(50);
    reading.raw = static_cast<uint16_t>(analogRead(PIN_PSU_IMON_ADC));
    reading.milliVolts = static_cast<uint16_t>(analogReadMilliVolts(PIN_PSU_IMON_ADC));
    reading.attenuationDb = 11;
  }
  return reading;
}

static void resetImonWindow(uint32_t now) {
  imonWindow.sampleCount = 0;
  imonWindow.rawSum = 0;
  imonWindow.milliVoltSum = 0;
  imonWindow.rawMin = UINT16_MAX;
  imonWindow.rawMax = 0;
  imonWindow.windowStartMs = now;
}

void configureImonMonitor() {
  pinMode(PIN_PSU_IMON_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_PSU_IMON_ADC, ADC_0db);
  const uint32_t now = millis();
  imonWindow.windowStartMs = now;
  imonWindow.lastSampleMs = now - kImonSampleIntervalMs;
}

ImonSnapshot snapshotImonWindow(bool resetAfterSnapshot) {
  const uint32_t now = millis();
  ImonSnapshot snapshot;
  snapshot.sampleCount = imonWindow.sampleCount;
  snapshot.windowMs = now - imonWindow.windowStartMs;

  if (imonWindow.sampleCount > 0) {
    const float rawAvg = static_cast<float>(imonWindow.rawSum) / static_cast<float>(imonWindow.sampleCount);
    const float adcVoltageVAvg = (static_cast<float>(imonWindow.milliVoltSum) / static_cast<float>(imonWindow.sampleCount)) / 1000.0f;
    snapshot.valid = true;
    snapshot.rawAvg = rawAvg;
    snapshot.adcVoltageVAvg = adcVoltageVAvg;
    snapshot.measuredImonVoltageVAvg = imonAdcToSourceVoltage(adcVoltageVAvg);
    snapshot.imonVoltageVAvg = snapshot.measuredImonVoltageVAvg * kImonV1SharedBusGain;
    snapshot.currentAAvg = imonVoltageToCurrentA(snapshot.imonVoltageVAvg);
    snapshot.rawMin = imonWindow.rawMin;
    snapshot.rawMax = imonWindow.rawMax;
    snapshot.attenuationDb = imonWindow.lastAttenuationDb;
  }

  if (resetAfterSnapshot) {
    resetImonWindow(now);
  }
  return snapshot;
}

static bool appendInfluxImonLine(String &payload, const ImonSnapshot &imon, uint64_t timestampMs = 0) {
  if (!imon.valid) {
    return false;
  }

  if (payload.length() > 0) {
    payload += '\n';
  }

  const String measurement = influx.measurement.length() > 0 ? influx.measurement : String("powerraid_psu");
  payload += influxEscapeKey(measurement + "_imon");
  payload += ",host=";
  payload += influxEscapeKey(getWifiHostname());
  payload += ' ';

  bool first = true;
  appendInfluxFloatField(payload, "imon_current_a_avg", imon.currentAAvg, first);
  appendInfluxFloatField(payload, "imon_voltage_v_avg", imon.imonVoltageVAvg, first);
  appendInfluxFloatField(payload, "imon_measured_voltage_v_avg", imon.measuredImonVoltageVAvg, first);
  appendInfluxFloatField(payload, "imon_adc_voltage_v_avg", imon.adcVoltageVAvg, first);
  appendInfluxFloatField(payload, "imon_adc_raw_avg", imon.rawAvg, first);
  appendInfluxIntField(payload, "imon_samples", static_cast<int32_t>(imon.sampleCount), first);
  appendInfluxIntField(payload, "imon_window_ms", static_cast<int32_t>(imon.windowMs), first);
  appendInfluxIntField(payload, "imon_adc_raw_min", imon.rawMin, first);
  appendInfluxIntField(payload, "imon_adc_raw_max", imon.rawMax, first);
  appendInfluxIntField(payload, "imon_adc_atten_db", imon.attenuationDb, first);
  if (timestampMs > 0) {
    payload += ' ';
    payload += String(timestampMs);
  }
  return !first;
}

void enqueueInfluxImonPoint(const ImonSnapshot &imon, uint32_t sampleMs) {
  if (!imon.valid || !influx.enabled) {
    return;
  }

  if (influxImonQueueCount >= kInfluxImonQueueDepth) {
    influxImonQueueHead = static_cast<uint8_t>((influxImonQueueHead + 1) % kInfluxImonQueueDepth);
    influxImonQueueCount -= 1;
    influxImonDropped += 1;
  }

  const uint8_t index = static_cast<uint8_t>((influxImonQueueHead + influxImonQueueCount) % kInfluxImonQueueDepth);
  InfluxImonPoint &point = influxImonQueue[index];
  point.sampleCount = imon.sampleCount;
  point.sampleMs = sampleMs;
  point.windowMs = imon.windowMs;
  point.sequence = influxNextImonSequence;
  point.rawAvg = imon.rawAvg;
  point.adcVoltageVAvg = imon.adcVoltageVAvg;
  point.measuredImonVoltageVAvg = imon.measuredImonVoltageVAvg;
  point.imonVoltageVAvg = imon.imonVoltageVAvg;
  point.currentAAvg = imon.currentAAvg;
  point.rawMin = imon.rawMin;
  point.rawMax = imon.rawMax;
  point.attenuationDb = imon.attenuationDb;
  influxNextImonSequence += 1;
  if (influxNextImonSequence == 0) {
    influxNextImonSequence = 1;
  }
  influxImonQueueCount += 1;
}

static bool dequeueInfluxImonPoint(InfluxImonPoint &point) {
  if (influxImonQueueCount == 0) {
    return false;
  }
  point = influxImonQueue[influxImonQueueHead];
  influxImonQueue[influxImonQueueHead] = InfluxImonPoint();
  influxImonQueueHead = static_cast<uint8_t>((influxImonQueueHead + 1) % kInfluxImonQueueDepth);
  influxImonQueueCount -= 1;
  return true;
}

void clearInfluxImonQueue() {
  for (uint8_t index = 0; index < kInfluxImonQueueDepth; ++index) {
    influxImonQueue[index] = InfluxImonPoint();
  }
  influxImonQueueHead = 0;
  influxImonQueueCount = 0;
}

void serviceImonMonitor() {
  const uint32_t now = millis();
  if (!timeReached(now, imonWindow.lastSampleMs + kImonSampleIntervalMs)) {
    return;
  }

  const ImonAdcReading reading = readImonAdc();
  imonWindow.lastSampleMs = now;
  imonWindow.lastRaw = reading.raw;
  imonWindow.lastMilliVolts = reading.milliVolts;
  imonWindow.lastAttenuationDb = reading.attenuationDb;
  imonWindow.rawSum += reading.raw;
  imonWindow.milliVoltSum += reading.milliVolts;
  imonWindow.sampleCount += 1;
  if (reading.raw < imonWindow.rawMin) {
    imonWindow.rawMin = reading.raw;
  }
  if (reading.raw > imonWindow.rawMax) {
    imonWindow.rawMax = reading.raw;
  }

  enqueueInfluxImonPoint(snapshotImonWindow(true), now);
}

static bool appendInfluxPsuLine(
    String &payload,
    PsuSlot &slot,
    const InfluxPsuSample &sample,
    uint64_t epochNowMs,
    uint32_t nowMs) {
  if (!slotIsPresent(slot) && !slot.detected) {
    return false;
  }
  if (!sample.pendingWrite) {
    return false;
  }

  const LiteonStatus &status = sample.status;
  const LiteonTelemetry &telemetry = sample.telemetry;
  if (payload.length() > 0) {
    payload += '\n';
  }

  const String measurement = influx.measurement.length() > 0 ? influx.measurement : String("powerraid_psu");
  payload += influxEscapeKey(measurement);
  payload += ",slot=";
  payload += String(slot.id);
  payload += ",name=";
  payload += influxEscapeKey(slot.name);
  payload += ",address=";
  payload += influxEscapeKey(hex8(slot.device->address7()));
  payload += ",host=";
  payload += influxEscapeKey(getWifiHostname());
  payload += ' ';

  bool first = true;
  appendInfluxBoolField(payload, "present", slotIsPresent(slot), first);
  appendInfluxBoolField(payload, "detected", slot.detected, first);
  appendInfluxBoolField(payload, "psok_high", digitalRead(slot.psokPin) == HIGH, first);
  appendInfluxBoolField(payload, "pson_asserted", slot.psonAsserted, first);
  appendInfluxBoolField(payload, "psu_pson_asserted", status.psonAsserted(), first);
  appendInfluxBoolField(payload, "power_policy_automatic", powerPolicy.automatic, first);
  appendInfluxBoolField(payload, "power_policy_candidate", slotPowerPolicyCandidate(slot, nowMs), first);
  appendInfluxBoolField(payload, "power_policy_failed", slotPowerPolicyFailed(slot, nowMs), first);
  appendInfluxBoolField(payload, "power_policy_startup_grace", slot.psonAsserted && nowMs - slot.psonChangedMs < kPowerPolicyStartupGraceMs, first);
  appendInfluxBoolField(payload, "power_policy_telemetry_valid", powerPolicy.telemetryValid, first);
  appendInfluxBoolField(payload, "power_policy_limit_exceeded", powerPolicy.limitExceeded, first);
  appendInfluxBoolField(payload, "power_policy_redundancy_shortfall", powerPolicy.redundancyShortfall, first);
  appendInfluxBoolField(payload, "input_ok", status.inputOk(), first);
  appendInfluxBoolField(payload, "output_ok", status.outputDcOk(), first);
  appendInfluxBoolField(payload, "interrupt_asserted", digitalRead(PIN_PSU_INTERRUPT_N) == LOW, first);
  appendInfluxBoolField(payload, "interrupt_latched", status.interruptLatched(), first);
  appendInfluxIntField(payload, "sample_age_ms", static_cast<int32_t>(nowMs - sample.readyMs), first);
  appendInfluxIntField(payload, "sample_window_ms", static_cast<int32_t>(sample.readyMs - sample.startedMs), first);
  appendInfluxIntField(payload, "power_policy_n_redundancy", powerPolicy.nRedundancy, first);
  appendInfluxIntField(payload, "power_policy_present_count", powerPolicy.presentCount, first);
  appendInfluxIntField(payload, "power_policy_candidate_count", powerPolicy.candidateCount, first);
  appendInfluxIntField(payload, "power_policy_active_count", powerPolicy.activeCount, first);
  appendInfluxIntField(payload, "power_policy_target_active_count", powerPolicy.targetActiveCount, first);
  appendInfluxIntField(payload, "power_policy_non_redundant_count", powerPolicy.nonRedundantCount, first);
  appendInfluxIntField(payload, "power_policy_max_output_w", powerPolicy.maxOutputW, first);
  appendInfluxStringField(payload, "power_policy_efficiency_profile", efficiencyProfile->id, first);
  appendInfluxIntField(payload, "uc_info_raw", status.ucInfo, first);
  appendInfluxIntField(payload, "ps_status_raw", status.psStatus, first);
  appendInfluxIntField(payload, "shutdown_event_raw", status.shutdownEvent, first);
  appendInfluxIntField(payload, "warning_event_raw", status.warningEvent, first);
  appendInfluxIntField(payload, "ps_control_raw", status.psControl, first);
  appendInfluxIntField(payload, "input_range", status.inputRange(), first);
  appendInfluxStringField(payload, "input_range_name", inputRangeName(status.inputRange()), first);
  appendInfluxFloatField(payload, "input_voltage_vrms", telemetry.inputVoltageVrms, first);
  appendInfluxFloatField(payload, "input_current_a", telemetry.inputCurrentA, first);
  appendInfluxFloatField(payload, "input_power_w", telemetry.inputPowerW, first);
  appendInfluxFloatField(payload, "power_policy_total_output_w", powerPolicy.totalOutputW, first);
  appendInfluxFloatField(payload, "output_voltage_v", telemetry.outputVoltageV, first);
  appendInfluxFloatField(payload, "output_current_a", telemetry.outputCurrentA, first);
  appendInfluxFloatField(payload, "output_power_w", telemetry.outputPowerW, first);
  appendInfluxFloatField(payload, "efficiency_percent", efficiencyPercent(telemetry.inputPowerW, telemetry.outputPowerW), first);
  appendInfluxFloatField(payload, "inlet_temperature_c", telemetry.inletTemperatureC, first);
  appendInfluxFloatField(payload, "internal_temperature_c", telemetry.internalTemperatureC, first);
  appendInfluxIntField(payload, "fan1_speed_rpm", telemetry.fan1SpeedRpm, first);
  appendInfluxIntField(payload, "input_voltage_raw", telemetry.inputVoltageRaw, first);
  appendInfluxIntField(payload, "input_current_raw", telemetry.inputCurrentRaw, first);
  appendInfluxIntField(payload, "input_power_raw", telemetry.inputPowerRaw, first);
  appendInfluxIntField(payload, "output_voltage_raw", telemetry.outputVoltageRaw, first);
  appendInfluxIntField(payload, "output_current_raw", telemetry.outputCurrentRaw, first);
  appendInfluxIntField(payload, "output_power_raw", telemetry.outputPowerRaw, first);
  appendInfluxIntField(payload, "inlet_temperature_raw", telemetry.inletTemperatureRaw, first);
  appendInfluxIntField(payload, "internal_temperature_raw", telemetry.internalTemperatureRaw, first);
  appendInfluxIntField(payload, "fan1_speed_raw", telemetry.fan1SpeedRaw, first);
  if (epochNowMs > 0 && sample.readyMs > 0) {
    const uint32_t ageMs = nowMs - sample.readyMs;
    const uint64_t sampleEpochMs = epochNowMs > ageMs ? epochNowMs - ageMs : epochNowMs;
    payload += ' ';
    payload += String(sampleEpochMs);
  }
  return !first;
}

String influxWriteUrl(const String &url, const String &org, const String &bucket) {
  return normalizedInfluxUrl(url) +
         "/api/v2/write?org=" + urlEncode(org) +
         "&bucket=" + urlEncode(bucket) +
         "&precision=ms";
}

String influxWriteUrl() {
  return influxWriteUrl(influx.url, influx.org, influx.bucket);
}

String influxJson() {
  String body = "{";
  body += "\"enabled\":";
  body += jsonBool(influx.enabled);
  body += ",\"configured\":";
  body += jsonBool(influxConfigured());
  body += ",\"url\":";
  body += jsonString(influx.url);
  body += ",\"org\":";
  body += jsonString(influx.org);
  body += ",\"bucket\":";
  body += jsonString(influx.bucket);
  body += ",\"measurement\":";
  body += jsonString(influx.measurement.length() > 0 ? influx.measurement : String("powerraid_psu"));
  body += ",\"intervalMs\":";
  body += String(influx.intervalMs);
  body += ",\"tokenSet\":";
  body += jsonBool(influx.token.length() > 0);
  body += ",\"uploadInFlight\":";
  body += jsonBool(influxUploadInFlight);
  body += ",\"imonQueued\":";
  body += String(influxImonQueueCount);
  body += ",\"imonDropped\":";
  body += String(influxImonDropped);
  body += ",\"lastResult\":";
  body += jsonString(influx.lastResult);
  body += ",\"lastError\":";
  body += jsonString(influx.lastError);
  body += ",\"lastHttpStatus\":";
  body += String(influx.lastHttpStatus);
  body += ",\"lastAttemptMs\":";
  body += String(influx.lastAttemptMs);
  body += ",\"lastSuccessMs\":";
  body += String(influx.lastSuccessMs);
  body += ",\"lastUploadDurationMs\":";
  body += String(influx.lastUploadDurationMs);
  body += ",\"maxUploadDurationMs\":";
  body += String(influx.maxUploadDurationMs);
  body += ",\"writeCount\":";
  body += String(influx.writeCount);
  body += ",\"errorCount\":";
  body += String(influx.errorCount);
  body += ",\"slowUploadCount\":";
  body += String(influx.slowUploadCount);
  body += "}";
  return body;
}

static void markInfluxError(const char *result, const String &message, int httpStatus = 0) {
  influx.lastResult = result;
  influx.lastError = message;
  influx.lastHttpStatus = httpStatus;
  influx.errorCount += 1;
}

static void writeInfluxSampleRegister(InfluxPsuSample &sample, LiteonRegister reg, uint16_t value) {
  switch (reg) {
    case LiteonRegister::UcInfo:
      sample.status.ucInfo = value;
      break;
    case LiteonRegister::PsStatus:
      sample.status.psStatus = value;
      break;
    case LiteonRegister::ShutdownEvent:
      sample.status.shutdownEvent = value;
      break;
    case LiteonRegister::WarningEvent:
      sample.status.warningEvent = value;
      break;
    case LiteonRegister::PsControl:
      sample.status.psControl = value;
      break;
    case LiteonRegister::InputVoltage:
      sample.telemetry.inputVoltageRaw = value;
      sample.telemetry.inputVoltageVrms = scaledSignedTelemetry(value, 32.0f);
      break;
    case LiteonRegister::InputCurrent:
      sample.telemetry.inputCurrentRaw = value;
      sample.telemetry.inputCurrentA = scaledSignedTelemetry(value, 64.0f);
      break;
    case LiteonRegister::InputPower:
      sample.telemetry.inputPowerRaw = value;
      sample.telemetry.inputPowerW = static_cast<float>(value);
      break;
    case LiteonRegister::OutputVoltage:
      sample.telemetry.outputVoltageRaw = value;
      sample.telemetry.outputVoltageV = scaledSignedTelemetry(value, 256.0f);
      break;
    case LiteonRegister::OutputCurrent:
      sample.telemetry.outputCurrentRaw = value;
      sample.telemetry.outputCurrentA = scaledSignedTelemetry(value, 64.0f);
      break;
    case LiteonRegister::OutputPower:
      sample.telemetry.outputPowerRaw = value;
      sample.telemetry.outputPowerW = static_cast<float>(value);
      break;
    case LiteonRegister::InletTemperature:
      sample.telemetry.inletTemperatureRaw = value;
      sample.telemetry.inletTemperatureC = scaledSignedTelemetry(value, 64.0f);
      break;
    case LiteonRegister::InternalTemperature:
      sample.telemetry.internalTemperatureRaw = value;
      sample.telemetry.internalTemperatureC = scaledSignedTelemetry(value, 64.0f);
      break;
    case LiteonRegister::Fan1Speed:
      sample.telemetry.fan1SpeedRaw = value;
      sample.telemetry.fan1SpeedRpm = value;
      break;
    default:
      break;
  }
}

static ImonSnapshot imonSnapshotFromInfluxPoint(const InfluxImonPoint &point) {
  ImonSnapshot snapshot;
  snapshot.valid = true;
  snapshot.sampleCount = point.sampleCount;
  snapshot.windowMs = point.windowMs;
  snapshot.rawAvg = point.rawAvg;
  snapshot.adcVoltageVAvg = point.adcVoltageVAvg;
  snapshot.measuredImonVoltageVAvg = point.measuredImonVoltageVAvg;
  snapshot.imonVoltageVAvg = point.imonVoltageVAvg;
  snapshot.currentAAvg = point.currentAAvg;
  snapshot.rawMin = point.rawMin;
  snapshot.rawMax = point.rawMax;
  snapshot.attenuationDb = point.attenuationDb;
  return snapshot;
}

static uint16_t appendQueuedInfluxImonLines(String &payload, InfluxUploadJob &job) {
  uint64_t epochNowMs = 0;
  const bool hasEpochTime = currentEpochMs(epochNowMs);
  const uint32_t nowMs = millis();
  uint16_t pointCount = 0;

  for (uint8_t offset = 0; offset < influxImonQueueCount; ++offset) {
    const uint8_t index = static_cast<uint8_t>((influxImonQueueHead + offset) % kInfluxImonQueueDepth);
    const InfluxImonPoint &point = influxImonQueue[index];
    const size_t payloadStart = payload.length();
    if (payload.length() > 0) {
      payload += '\n';
    }
    const ImonSnapshot snapshot = imonSnapshotFromInfluxPoint(point);
    uint64_t sampleEpochMs = 0;
    if (hasEpochTime) {
      const uint32_t ageMs = nowMs - point.sampleMs;
      sampleEpochMs = epochNowMs > ageMs ? epochNowMs - ageMs : epochNowMs;
    }
    if (!appendInfluxImonLine(payload, snapshot, sampleEpochMs)) {
      payload.remove(payloadStart);
      continue;
    }
    if (pointCount > 0 && payload.length() > kInfluxMaxPayloadBytes) {
      payload.remove(payloadStart);
      break;
    }
    if (job.imonPointCount == 0) {
      job.firstImonSequence = point.sequence;
    }
    job.imonPointCount += 1;
    pointCount += 1;
  }

  return pointCount;
}

static bool influxSequenceBefore(uint32_t first, uint32_t second) {
  return static_cast<int32_t>(first - second) < 0;
}

static void acknowledgeInfluxImonPoints(const InfluxUploadCompletion &completion) {
  for (uint16_t offset = 0; offset < completion.imonPointCount; ++offset) {
    const uint32_t expectedSequence = completion.firstImonSequence + offset;
    while (influxImonQueueCount > 0) {
      const uint32_t queuedSequence = influxImonQueue[influxImonQueueHead].sequence;
      if (queuedSequence == expectedSequence) {
        InfluxImonPoint discarded;
        dequeueInfluxImonPoint(discarded);
        break;
      }
      if (influxSequenceBefore(queuedSequence, expectedSequence)) {
        InfluxImonPoint discarded;
        dequeueInfluxImonPoint(discarded);
        continue;
      }
      break;
    }
  }
}

static bool submitInfluxTelemetryUpload() {
  if (influxUploadQueue == nullptr || influxUploadCompletionQueue == nullptr ||
      influxUploadTaskHandle == nullptr || influxUploadInFlight) {
    return false;
  }

  if (!wifiIsConnected()) {
    markInfluxError("wifi-disconnected", "WiFi is not connected");
    return false;
  }
  if (!influxConfigured()) {
    markInfluxError("not-configured", "InfluxDB URL, org, bucket, and token are required");
    return false;
  }

  InfluxUploadJob &job = influxUploadJob;
  job.url = influx.url;
  job.org = influx.org;
  job.bucket = influx.bucket;
  job.token = influx.token;
  job.payload.remove(0);
  job.pointCount = 0;
  job.imonPointCount = 0;
  job.firstImonSequence = 0;
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    job.includedSlots[index] = false;
  }

  uint64_t epochNowMs = 0;
  currentEpochMs(epochNowMs);
  const uint32_t nowMs = millis();

  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (appendInfluxPsuLine(job.payload, psuSlots[index], influxPsuSamples[index], epochNowMs, nowMs)) {
      job.includedSlots[index] = true;
      job.pointCount += 1;
    }
  }
  job.pointCount += appendQueuedInfluxImonLines(job.payload, job);

  if (job.pointCount == 0 || job.payload.length() == 0) {
    markInfluxError("no-telemetry", "No PSU or I-MON telemetry is available");
    return false;
  }

  InfluxUploadJob *jobPointer = &job;
  influxUploadInFlight = true;
  if (xQueueSend(influxUploadQueue, &jobPointer, 0) != pdTRUE) {
    influxUploadInFlight = false;
    markInfluxError("queue-full", "InfluxDB upload worker queue is full");
    return false;
  }
  return true;
}

static bool postInfluxPayload(const InfluxUploadJob &job, int &status, String &response) {
  WiFiClient client;
  WiFiClientSecure secureClient;
  WiFiClient *httpClient = &client;
  if (urlUsesHttps(job.url)) {
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(kInfluxTlsHandshakeTimeoutSeconds);
    secureClient.setTimeout((kInfluxConnectTimeoutMs + 999) / 1000);
    httpClient = &secureClient;
  }
  HTTPClient http;
  http.setConnectTimeout(kInfluxConnectTimeoutMs);
  http.setTimeout(kInfluxHttpTimeoutMs);
  if (!http.begin(*httpClient, influxWriteUrl(job.url, job.org, job.bucket))) {
    return false;
  }
  http.addHeader("Authorization", String("Token ") + job.token);
  http.addHeader("Content-Type", "text/plain; charset=utf-8");
  http.addHeader("Accept", "application/json");
  status = http.POST(job.payload);
  if (status < 200 || status >= 300) {
    response = http.getString();
  }
  http.end();
  return true;
}

static void completeInfluxUploadSuccess(const InfluxUploadJob &job, int status) {
  influx.lastHttpStatus = status;
  influx.lastResult = "ok";
  influx.lastError = "";
  influx.lastSuccessMs = millis();
  influx.writeCount += job.pointCount;
}

static void recordInfluxUploadDuration(uint32_t startedMs) {
  const uint32_t durationMs = millis() - startedMs;
  influx.lastUploadDurationMs = durationMs;
  if (durationMs > influx.maxUploadDurationMs) {
    influx.maxUploadDurationMs = durationMs;
  }
  if (durationMs >= kInfluxSlowUploadThresholdMs) {
    influx.slowUploadCount += 1;
  }
}

static bool serviceInfluxUploadJob(InfluxUploadJob &job) {
  const uint32_t startedMs = millis();
  influx.lastAttemptMs = startedMs;
  influx.lastHttpStatus = 0;

  if (!wifiIsConnected()) {
    markInfluxError("wifi-disconnected", "WiFi is not connected");
    recordInfluxUploadDuration(startedMs);
    return false;
  }
  if (job.url.length() == 0 || job.org.length() == 0 || job.bucket.length() == 0 || job.token.length() == 0) {
    markInfluxError("not-configured", "InfluxDB URL, org, bucket, and token are required");
    recordInfluxUploadDuration(startedMs);
    return false;
  }

  int status = 0;
  String response;
  if (!postInfluxPayload(job, status, response)) {
    markInfluxError("begin-failed", "could not initialize HTTP client");
    recordInfluxUploadDuration(startedMs);
    return false;
  }
  if (status >= 200 && status < 300) {
    completeInfluxUploadSuccess(job, status);
    recordInfluxUploadDuration(startedMs);
    return true;
  }

  if (status <= 0) {
    markInfluxError("http-failed", String("HTTP client error ") + String(status), status);
  } else {
    markInfluxError("write-failed", response.length() > 0 ? response : String("InfluxDB write failed"), status);
  }
  recordInfluxUploadDuration(startedMs);
  return false;
}

static uint32_t influxReportDelayMs() {
  return influx.intervalMs > kInfluxBatchWindowMs ? influx.intervalMs : kInfluxBatchWindowMs;
}

static void influxUploadTask(void *parameter) {
  (void)parameter;
  for (;;) {
    InfluxUploadJob *job = nullptr;
    if (xQueueReceive(influxUploadQueue, &job, portMAX_DELAY) == pdTRUE && job != nullptr) {
      const bool succeeded = serviceInfluxUploadJob(*job);
      if (succeeded) {
        InfluxUploadCompletion completion;
        completion.imonPointCount = job->imonPointCount;
        completion.firstImonSequence = job->firstImonSequence;
        for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
          completion.includedSlots[index] = job->includedSlots[index];
        }
        if (xQueueSend(influxUploadCompletionQueue, &completion, portMAX_DELAY) != pdTRUE) {
          markInfluxError("completion-queue-full", "InfluxDB upload acknowledgement queue is full");
        }
      }
      influx.nextReportMs = millis() + (succeeded ? influxReportDelayMs() : kInfluxPsuRetryIntervalMs);
      influxUploadInFlight = false;
    }
  }
}

void configureInfluxUploadWorker() {
  influxUploadJob.payload.reserve(kInfluxMaxPayloadBytes);
  influxUploadQueue = xQueueCreate(1, sizeof(InfluxUploadJob *));
  if (influxUploadQueue == nullptr) {
    markInfluxError("queue-create-failed", "could not create InfluxDB upload queue");
    return;
  }
  influxUploadCompletionQueue = xQueueCreate(1, sizeof(InfluxUploadCompletion));
  if (influxUploadCompletionQueue == nullptr) {
    markInfluxError("queue-create-failed", "could not create InfluxDB upload acknowledgement queue");
    return;
  }

  const BaseType_t result = xTaskCreatePinnedToCore(
      influxUploadTask,
      "influx_http",
      kInfluxUploadTaskStackBytes,
      nullptr,
      1,
      &influxUploadTaskHandle,
      0);
  if (result != pdPASS) {
    influxUploadTaskHandle = nullptr;
    markInfluxError("task-create-failed", "could not create InfluxDB upload task");
  }
}

void serviceInfluxUploadCompletions() {
  if (influxUploadCompletionQueue == nullptr) {
    return;
  }

  InfluxUploadCompletion completion;
  while (xQueueReceive(influxUploadCompletionQueue, &completion, 0) == pdTRUE) {
    for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
      if (completion.includedSlots[index]) {
        influxPsuSamples[index].pendingWrite = false;
      }
    }
    acknowledgeInfluxImonPoints(completion);
  }
}

static bool influxSlotIncluded(uint8_t index) {
  if (index >= kPsuSlotCount) {
    return false;
  }
  const PsuSlot &slot = psuSlots[index];
  return slotIsPresent(slot) || slot.detected;
}

static void startInfluxPsuSample(InfluxPsuSample &sample, uint8_t slotIndex, uint32_t now) {
  sample.active = true;
  sample.slotIndex = slotIndex;
  sample.registerIndex = 0;
  sample.startedMs = now;
  sample.readyMs = 0;
  sample.status = LiteonStatus();
  sample.telemetry = LiteonTelemetry();
}

static void failInfluxPsuSample(InfluxPsuSample &sample, PsuSlot &slot, LiteonResult result, uint32_t now) {
  sample.active = false;
  sample.nextRetryMs = now + kInfluxPsuRetryIntervalMs;
  if (result == LiteonResult::I2cNack) {
    slot.detected = false;
    if (slot.id == 1) {
      liteonAddressDetected = false;
    }
  }
}

bool influxHasPendingTelemetry() {
  if (influxImonQueueCount > 0) {
    return true;
  }
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (influxPsuSamples[index].pendingWrite) {
      return true;
    }
  }
  return false;
}

void serviceInfluxPsuSampler() {
  if (!influx.enabled || otaUploadRunning || otaRestartPending) {
    return;
  }

  const uint32_t now = millis();
  static uint8_t nextSlotIndex = 0;
  for (uint8_t attempt = 0; attempt < kPsuSlotCount; ++attempt) {
    const uint8_t slotIndex = nextSlotIndex;
    nextSlotIndex = static_cast<uint8_t>((nextSlotIndex + 1) % kPsuSlotCount);

    if (!influxSlotIncluded(slotIndex)) {
      continue;
    }

    PsuSlot &slot = psuSlots[slotIndex];
    InfluxPsuSample &sample = influxPsuSamples[slotIndex];
    if (sample.pendingWrite) {
      continue;
    }
    if (!sample.active) {
      if (!timeReached(now, sample.nextRetryMs)) {
        continue;
      }
      startInfluxPsuSample(sample, slotIndex, now);
    }

    if (!ensurePsuSlotDetected(slot)) {
      failInfluxPsuSample(sample, slot, LiteonResult::I2cNack, now);
      return;
    }

    const LiteonRegister reg = kInfluxPsuReadRegisters[sample.registerIndex];
    uint16_t value = 0;
    const LiteonResult result = slot.device->readRaw(reg, value);
    if (result != LiteonResult::Ok) {
      failInfluxPsuSample(sample, slot, result, millis());
      return;
    }

    writeInfluxSampleRegister(sample, reg, value);
    sample.registerIndex += 1;
    if (sample.registerIndex >= kInfluxPsuRegisterCount) {
      const uint32_t readyMs = millis();
      const bool alreadyHadPendingTelemetry = influxHasPendingTelemetry();
      sample.active = false;
      sample.pendingWrite = true;
      sample.readyMs = readyMs;
      slot.detected = true;
      slot.lastTelemetryMs = readyMs;
      if (slot.id == 1) {
        liteonAddressDetected = true;
      }
      if (!alreadyHadPendingTelemetry) {
        influx.nextReportMs = readyMs + influxReportDelayMs();
      }
    }
    return;
  }
}

void serviceInfluxReporter() {
  if (!influx.enabled || otaUploadRunning || otaRestartPending) {
    return;
  }
  if (influxUploadInFlight) {
    return;
  }

  const uint32_t now = millis();
  if (!influxHasPendingTelemetry() || !timeReached(now, influx.nextReportMs)) {
    return;
  }

  if (!submitInfluxTelemetryUpload()) {
    influx.nextReportMs = millis() + kInfluxPsuRetryIntervalMs;
  }
}

void configureInflux() {
  loadInfluxConfig();
  configureImonMonitor();
  configureInfluxUploadWorker();
}
