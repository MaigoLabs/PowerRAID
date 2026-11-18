#include "serial_console.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>

#include "auth.h"
#include "config.h"
#include "util.h"
#include "wifi_manager.h"

static char serialLine[kSerialLineMax] = {};
static size_t serialLineLen = 0;
static bool serialLineOverflow = false;

static void serialPrintWifiStatus(const char *prefix) {
  Serial.print(prefix);
  Serial.print(" STATUS=");
  Serial.print(wifiStatusName(WiFi.status()));
  Serial.print(" CONNECTED=");
  Serial.print(wifiIsConnected() ? 1 : 0);
  Serial.print(" REMEMBERED=");
  Serial.print(wifiCredentialsRemembered() ? 1 : 0);
  Serial.print(" HOSTNAME=");
  Serial.print(getWifiHostname());
  Serial.print(" MDNS=");
  Serial.print(isMdnsStarted() ? 1 : 0);
  Serial.print(" SSID=");
  Serial.print(jsonString(wifiIsConnected() ? WiFi.SSID() : String("")));
  Serial.print(" IP=");
  Serial.print(wifiIpOrDash());
  Serial.print(" RSSI=");
  Serial.print(wifiIsConnected() ? WiFi.RSSI() : 0);
  Serial.println();
}

static bool parseSerialArgs(char *line, char *argv[], size_t maxArgc, size_t &argc, const char *&error) {
  argc = 0;
  error = nullptr;
  char *read = line;
  char *write = line;

  while (*read != '\0') {
    while (*read != '\0' && isspace(static_cast<unsigned char>(*read))) {
      ++read;
    }
    if (*read == '\0') {
      break;
    }
    if (argc >= maxArgc) {
      error = "too many arguments";
      return false;
    }

    argv[argc++] = write;
    bool quoted = false;
    while (*read != '\0') {
      const char ch = *read;
      if (quoted) {
        if (ch == '"') {
          quoted = false;
          ++read;
          continue;
        }
        if (ch == '\\' && read[1] != '\0') {
          ++read;
        }
        *write++ = *read++;
        continue;
      }

      if (isspace(static_cast<unsigned char>(ch))) {
        ++read;
        break;
      }
      if (ch == '"') {
        quoted = true;
        ++read;
        continue;
      }
      if (ch == '\\' && read[1] != '\0') {
        ++read;
      }
      *write++ = *read++;
    }

    if (quoted) {
      error = "unterminated quote";
      return false;
    }
    *write++ = '\0';
  }

  return true;
}

static void printSerialHelp() {
  Serial.println("PowerRAID serial commands:");
  Serial.println("  help");
  Serial.println("  wifi status");
  Serial.println("  wifi connect <ssid> [password] [--no-remember]");
  Serial.println("  wifi reconnect");
  Serial.println("  wifi disconnect");
  Serial.println("  wifi forget");
  Serial.println("  auth status");
  Serial.println("  auth reset");
  Serial.println("Quote SSIDs or passwords that contain spaces.");
}

static void serialPrintAuthStatus(const char *prefix) {
  Serial.print(prefix);
  Serial.print(" PASSWORD_SET=");
  Serial.print(auth::passwordConfigured() ? 1 : 0);
  Serial.print(" TELEMETRY_PUBLIC=");
  Serial.print(auth::telemetryPublic() ? 1 : 0);
  Serial.print(" LOCKOUT_MS=");
  Serial.println(auth::lockoutRemainingMs());
}

static void handleSerialAuthCommand(char *argv[], size_t argc) {
  if (argc == 1 || tokenEquals(argv[1], "status")) {
    serialPrintAuthStatus("OK AUTH");
    return;
  }
  if (tokenEquals(argv[1], "reset")) {
    // Physical access is the recovery path: clearing the password from the
    // USB console reopens the device, exactly like a factory reset would.
    auth::clearPassword();
    serialPrintAuthStatus("OK AUTH");
    return;
  }
  Serial.println("ERR AUTH USAGE=\"auth status|reset\"");
}

static void handleSerialWifiConnect(char *argv[], size_t argc) {
  if (argc < 3) {
    Serial.println("ERR WIFI USAGE=\"wifi connect <ssid> [password] [--no-remember]\"");
    return;
  }

  const char *ssid = argv[2];
  if (ssid[0] == '\0') {
    Serial.println("ERR WIFI RESULT=missing-ssid");
    return;
  }

  const char *password = "";
  bool passwordSet = false;
  bool remember = true;
  for (size_t index = 3; index < argc; ++index) {
    if (tokenEquals(argv[index], "--no-remember") || tokenEquals(argv[index], "remember=0")) {
      remember = false;
      continue;
    }
    if (passwordSet) {
      Serial.println("ERR WIFI RESULT=too-many-arguments");
      return;
    }
    password = argv[index];
    passwordSet = true;
  }

  Serial.print("OK WIFI CONNECTING SSID=");
  Serial.print(jsonString(ssid));
  Serial.print(" REMEMBER=");
  Serial.println(remember ? 1 : 0);
  Serial.flush();

  startWifiConnect(ssid, password);
  const bool connected = waitForWifiConnect(kWifiConnectTimeoutMs);
  if (connected && remember) {
    rememberWifiCredentials(ssid, password);
  }

  serialPrintWifiStatus(connected ? "OK WIFI" : "ERR WIFI");
}

static void handleSerialWifiReconnect() {
  String ssid, password;
  if (!getRememberedCredentials(ssid, password)) {
    Serial.println("ERR WIFI RESULT=no-remembered-credentials");
    return;
  }

  Serial.println("OK WIFI RECONNECTING");
  Serial.flush();
  const bool connected = reconnectWifi(kWifiConnectTimeoutMs);
  serialPrintWifiStatus(connected ? "OK WIFI" : "ERR WIFI");
}

static void handleSerialWifiDisconnect() {
  disconnectWifi(false);
  serialPrintWifiStatus("OK WIFI");
}

static void handleSerialWifiForget() {
  forgetWifi();
  serialPrintWifiStatus("OK WIFI");
}

static void handleSerialWifiCommand(char *argv[], size_t argc) {
  if (argc == 1 || tokenEquals(argv[1], "status")) {
    serialPrintWifiStatus("OK WIFI");
    return;
  }
  if (tokenEquals(argv[1], "connect")) {
    handleSerialWifiConnect(argv, argc);
    return;
  }
  if (tokenEquals(argv[1], "reconnect")) {
    handleSerialWifiReconnect();
    return;
  }
  if (tokenEquals(argv[1], "disconnect")) {
    handleSerialWifiDisconnect();
    return;
  }
  if (tokenEquals(argv[1], "forget")) {
    handleSerialWifiForget();
    return;
  }
  Serial.println("ERR WIFI USAGE=\"wifi status|connect|reconnect|disconnect|forget\"");
}

static void handleSerialCommand(char *line) {
  char *argv[6] = {};
  size_t argc = 0;
  const char *error = nullptr;
  if (!parseSerialArgs(line, argv, sizeof(argv) / sizeof(argv[0]), argc, error)) {
    Serial.print("ERR PARSE MESSAGE=");
    Serial.println(jsonString(error == nullptr ? "parse error" : error));
    return;
  }
  if (argc == 0) {
    return;
  }

  if (tokenEquals(argv[0], "help") || tokenEquals(argv[0], "?")) {
    printSerialHelp();
    return;
  }
  if (tokenEquals(argv[0], "wifi")) {
    handleSerialWifiCommand(argv, argc);
    return;
  }
  if (tokenEquals(argv[0], "auth")) {
    handleSerialAuthCommand(argv, argc);
    return;
  }

  Serial.println("ERR unknown-command");
}

static void handleSerialByte(char incoming) {
  if (incoming == '\r' || incoming == '\n') {
    if (serialLineOverflow) {
      serialLineLen = 0;
      serialLineOverflow = false;
      Serial.println("ERR line-too-long");
      return;
    }
    if (serialLineLen == 0) {
      return;
    }
    serialLine[serialLineLen] = '\0';
    handleSerialCommand(serialLine);
    serialLineLen = 0;
    return;
  }

  if (incoming == '\b' || incoming == 0x7F) {
    if (serialLineLen > 0) {
      --serialLineLen;
    }
    return;
  }

  if (serialLineOverflow || static_cast<uint8_t>(incoming) < 0x20) {
    return;
  }
  if (serialLineLen >= kSerialLineMax - 1) {
    serialLineOverflow = true;
    return;
  }
  serialLine[serialLineLen++] = incoming;
}

void configureSerialConsole() {
  Serial.begin(kSerialBaud);
  delay(25);
  Serial.println();
  Serial.print("PowerRAID ");
  Serial.print(PRAMON_FW_VERSION);
  Serial.print(" serial ready HOSTNAME=");
  Serial.println(getWifiHostname());
  Serial.println("Type help for commands.");
}

void serviceSerialConsole() {
  while (Serial.available() > 0) {
    handleSerialByte(static_cast<char>(Serial.read()));
  }
}
