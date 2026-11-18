#include "util.h"
#include <stdlib.h>
#include <string.h>

String hex8(uint8_t value) {
  String out = "0x";
  if (value < 0x10) out += '0';
  out += String(value, HEX);
  out.toUpperCase();
  return out;
}

String hex16(uint16_t value) {
  String out = "0x";
  if (value < 0x1000) out += '0';
  if (value < 0x0100) out += '0';
  if (value < 0x0010) out += '0';
  out += String(value, HEX);
  out.toUpperCase();
  return out;
}

bool tokenEquals(const char *token, const char *expected) {
  if (token == nullptr || expected == nullptr) {
    return false;
  }
  while (*token != '\0' && *expected != '\0') {
    char lhs = *token++;
    char rhs = *expected++;
    if (lhs >= 'A' && lhs <= 'Z') lhs = static_cast<char>(lhs - 'A' + 'a');
    if (rhs >= 'A' && rhs <= 'Z') rhs = static_cast<char>(rhs - 'A' + 'a');
    if (lhs != rhs) {
      return false;
    }
  }
  return *token == '\0' && *expected == '\0';
}

bool tokenIsAll(const char *token) {
  return tokenEquals(token, "all") || tokenEquals(token, "*");
}

bool parseUnsignedToken(const char *token, uint32_t maxValue, uint32_t &value) {
  if (token == nullptr || *token == '\0') {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = strtoul(token, &end, 0);
  if (end == token || *end != '\0' || parsed > maxValue) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseU8Token(const char *token, uint8_t &value) {
  uint32_t parsed = 0;
  if (!parseUnsignedToken(token, 0xFF, parsed)) {
    return false;
  }
  value = static_cast<uint8_t>(parsed);
  return true;
}

bool parseU16Token(const char *token, uint16_t &value) {
  uint32_t parsed = 0;
  if (!parseUnsignedToken(token, 0xFFFF, parsed)) {
    return false;
  }
  value = static_cast<uint16_t>(parsed);
  return true;
}

bool parseBoolToken(const char *token, bool &value) {
  if (token == nullptr || *token == '\0') {
    return false;
  }
  if (tokenEquals(token, "1") || tokenEquals(token, "true") || tokenEquals(token, "yes") ||
      tokenEquals(token, "on") || tokenEquals(token, "enable") || tokenEquals(token, "enabled") ||
      tokenEquals(token, "auto") || tokenEquals(token, "automatic")) {
    value = true;
    return true;
  }
  if (tokenEquals(token, "0") || tokenEquals(token, "false") || tokenEquals(token, "no") ||
      tokenEquals(token, "off") || tokenEquals(token, "disable") || tokenEquals(token, "disabled") ||
      tokenEquals(token, "manual")) {
    value = false;
    return true;
  }
  return false;
}

bool parseRegisterToken(const char *token, uint8_t &reg) {
  if (!parseU8Token(token, reg)) {
    return false;
  }
  return (reg & 0x01) == 0;
}

bool timeReached(uint32_t now, uint32_t due) {
  return static_cast<int32_t>(now - due) >= 0;
}

uint32_t clampInfluxInterval(uint32_t intervalMs) {
  if (intervalMs < kInfluxMinIntervalMs) {
    return kInfluxMinIntervalMs;
  }
  if (intervalMs > kInfluxMaxIntervalMs) {
    return kInfluxMaxIntervalMs;
  }
  return intervalMs;
}

uint16_t clampMonitorInterval(uint32_t intervalMs) {
  if (intervalMs < kMonitorMinIntervalMs) {
    return kMonitorMinIntervalMs;
  }
  if (intervalMs > kMonitorMaxIntervalMs) {
    return kMonitorMaxIntervalMs;
  }
  return static_cast<uint16_t>(intervalMs);
}

float scaledSignedTelemetry(uint16_t raw, float divisor) {
  return static_cast<float>(static_cast<int16_t>(raw)) / divisor;
}

const char *resultName(LiteonResult result) {
  switch (result) {
    case LiteonResult::Ok:
      return "ok";
    case LiteonResult::I2cNack:
      return "i2c-nack";
    case LiteonResult::ShortRead:
      return "short-read";
    case LiteonResult::ChecksumMismatch:
      return "checksum-mismatch";
    case LiteonResult::DeviceReportedChecksumError:
      return "device-reported-checksum-error";
    case LiteonResult::InvalidRegister:
      return "invalid-register";
    default:
      return "unknown";
  }
}

const char *inputRangeName(uint8_t range) {
  switch (range) {
    case 0:
      return "out-of-range";
    case 1:
      return "low-line";
    case 2:
      return "mid-line";
    case 3:
      return "high-line";
    case 4:
      return "48VDC";
    case 5:
      return "420VDC";
    default:
      return "reserved";
  }
}

String buildStamp() {
  String stamp;
  bool previousWasSeparator = false;
  for (const char *cursor = __DATE__; *cursor != '\0'; ++cursor) {
    const bool separator = *cursor == ' ';
    if (separator) {
      if (!previousWasSeparator) {
        stamp += '-';
      }
      previousWasSeparator = true;
    } else {
      stamp += *cursor;
      previousWasSeparator = false;
    }
  }
  stamp += '_';
  stamp += __TIME__;
  return stamp;
}

String jsonEscape(const String &value) {
  String out;
  out.reserve(value.length() + 4);
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<uint8_t>(ch) < 0x20) {
          out += ' ';
        } else {
          out += ch;
        }
        break;
    }
  }
  return out;
}

String jsonString(const String &value) {
  return String("\"") + jsonEscape(value) + "\"";
}

String jsonBool(bool value) {
  return value ? "true" : "false";
}

bool isUrlUnreserved(char ch) {
  return (ch >= 'A' && ch <= 'Z') ||
         (ch >= 'a' && ch <= 'z') ||
         (ch >= '0' && ch <= '9') ||
         ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

String urlEncode(const String &value) {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); ++index) {
    const uint8_t ch = static_cast<uint8_t>(value[index]);
    if (isUrlUnreserved(static_cast<char>(ch))) {
      out += static_cast<char>(ch);
    } else {
      out += '%';
      out += hex[(ch >> 4) & 0x0F];
      out += hex[ch & 0x0F];
    }
  }
  return out;
}

String influxEscapeKey(const String &value) {
  String out;
  out.reserve(value.length() + 4);
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    if (ch == ' ' || ch == ',' || ch == '=' || ch == '\\') {
      out += '\\';
    }
    out += ch;
  }
  return out;
}

String influxEscapeStringField(const String &value) {
  String out;
  out.reserve(value.length() + 4);
  for (size_t index = 0; index < value.length(); ++index) {
    const char ch = value[index];
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if (ch == '\n' || ch == '\r') {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

void appendInfluxFieldSeparator(String &line, bool &first) {
  if (first) {
    first = false;
  } else {
    line += ',';
  }
}
