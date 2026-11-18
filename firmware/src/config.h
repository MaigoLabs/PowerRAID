#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "psu_wiring.h"

// I2C and Lite-On Hardware Constants
static constexpr uint32_t kI2cClockHz = 100000;
static constexpr uint8_t kLiteonAddPins = 0b000;
static constexpr uint8_t kLiteonAddressPinCount = 8;
static constexpr uint8_t kPsuSlotCount = 3;

// PS-2122-3H low-line rating for the 120 VAC deployment.
static constexpr uint16_t kPsuRatedOutputWatts = 900;
static constexpr uint8_t kDefaultNRedundancy = 0;
static constexpr uint8_t kMaxNRedundancy = kPsuSlotCount - 1;
static constexpr uint8_t kAllPsuPsonMask = (1U << kPsuSlotCount) - 1U;

// LED Indicators
static constexpr uint32_t kRunLedPeriodMs = 500;
static constexpr uint32_t kWifiStatusLedPeriodMs = 500;
static constexpr uint8_t kLedOn = HIGH;
static constexpr uint8_t kLedOff = LOW;
static constexpr uint8_t kPowerLimitLedPin = PIN_LED_2;
static constexpr uint8_t kPowerFaultLedPin = PIN_LED_FAULT;

// Telemetry & Monitoring Timing
static constexpr uint8_t kMonitorMaxRegisters = 24;
static constexpr uint16_t kMonitorDefaultIntervalMs = 1000;
static constexpr uint16_t kMonitorMinIntervalMs = 100;
static constexpr uint16_t kMonitorMaxIntervalMs = 10000;
static constexpr uint32_t kPowerPolicyIntervalMs = 2000;
static constexpr uint32_t kPowerPolicyStartupGraceMs = 1500;

// Network & Web Ports
static constexpr uint16_t kApiPort = 80;
static constexpr uint16_t kMonitorStreamPort = 81;
static constexpr uint32_t kMonitorStreamHeartbeatMs = 15000;
static constexpr uint32_t kWifiConnectTimeoutMs = 15000;
static constexpr uint32_t kOtaRestartDelayMs = 1000;
static const char kOtaConfirmHeader[] = "X-PowerRAID-OTA";
// Session token header for the API and the port-81 event stream.
static const char kAuthHeader[] = "X-PowerRAID-Auth";
static const char kMonitorStreamPath[] = "/api/v1/monitor/events";

// Serial Console
static constexpr uint32_t kSerialBaud = 115200;
static constexpr size_t kSerialLineMax = 512;

// InfluxDB Exporter
static constexpr uint32_t kInfluxDefaultIntervalMs = 80;
static constexpr uint32_t kInfluxMinIntervalMs = 80;
static constexpr uint32_t kInfluxMaxIntervalMs = 3600000;
static constexpr uint32_t kInfluxConnectTimeoutMs = 3000;
static constexpr uint32_t kInfluxHttpTimeoutMs = 3000;
static constexpr uint32_t kInfluxTlsHandshakeTimeoutSeconds = 3;
static constexpr uint32_t kInfluxSlowUploadThresholdMs = 1000;
static constexpr uint32_t kInfluxPsuRetryIntervalMs = 1000;
static constexpr uint32_t kInfluxBatchWindowMs = 1000;
static constexpr uint8_t kInfluxImonQueueDepth = 128;
static constexpr size_t kInfluxMaxPayloadBytes = 24000;
static constexpr uint32_t kInfluxUploadTaskStackBytes = 16384;

// Analog IMON Current Sense Parameters
static constexpr uint32_t kImonSampleIntervalMs = 80;
static constexpr uint16_t kImonLowRangeMaxMilliVolts = 800;
static constexpr uint16_t kImonLowRangeMaxRaw = 3900;
static constexpr float kImonDividerHighOhms = 82000.0f;
static constexpr float kImonDividerLowOhms = 100000.0f;
static constexpr float kImonRatedCurrentA = 75.0f;
static constexpr float kImonRatedVoltageV = 4.0f;
static constexpr float kImonV1SharedBusGain = 1.676f;
static constexpr float kImonV1CurrentOffsetA = -0.19f;

static constexpr uint32_t kAtxDebounceMs = 50;

#ifndef PRAMON_FW_VERSION
#define PRAMON_FW_VERSION "0.8.3"
#endif

#ifndef PRAMON_PROTOCOL_VERSION
#define PRAMON_PROTOCOL_VERSION 6
#endif
