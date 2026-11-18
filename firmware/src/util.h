#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "liteon_psu.h"

// Hexadecimal and token formatting
String hex8(uint8_t value);
String hex16(uint16_t value);
bool tokenEquals(const char *token, const char *expected);
bool tokenIsAll(const char *token);

// Parsing helpers
bool parseUnsignedToken(const char *token, uint32_t maxValue, uint32_t &value);
bool parseU8Token(const char *token, uint8_t &value);
bool parseU16Token(const char *token, uint16_t &value);
bool parseBoolToken(const char *token, bool &value);
bool parseRegisterToken(const char *token, uint8_t &reg);

// Time and clamping
bool timeReached(uint32_t now, uint32_t due);
uint32_t clampInfluxInterval(uint32_t intervalMs);
uint16_t clampMonitorInterval(uint32_t intervalMs);
float scaledSignedTelemetry(uint16_t raw, float divisor);

// Liteon and status names
const char *resultName(LiteonResult result);
const char *inputRangeName(uint8_t range);
String buildStamp();

// JSON and URL formatting
String jsonEscape(const String &value);
String jsonString(const String &value);
String jsonBool(bool value);
bool isUrlUnreserved(char ch);
String urlEncode(const String &value);

// Influx line protocol escaping
String influxEscapeKey(const String &value);
String influxEscapeStringField(const String &value);
void appendInfluxFieldSeparator(String &line, bool &first);
