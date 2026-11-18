#include "web_server.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ctype.h>
#include <errno.h>
#include <lwip/sockets.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "generated/pramon_assets.h"
#include "config.h"
#include "types.h"
#include "util.h"
#include "wifi_manager.h"
#include "psu_manager.h"
#include "power_policy.h"
#include "influx_client.h"
#include "auth.h"

// Request headers the API reads. Bearer tokens carry the login session; the
// SSE stream on port 81 reuses the same header for its EventSource auth.
static const char *kApiHeaderKeys[] = {kOtaConfirmHeader, kAuthHeader};

WebServer apiServer(kApiPort);
WiFiServer monitorStreamServer(kMonitorStreamPort);
WiFiClient monitorStreamClient;

bool otaUploadRunning = false;
static bool otaUploadSucceeded = false;
bool otaRestartPending = false;
static uint32_t otaRestartAtMs = 0;
static size_t otaBytesWritten = 0;
static String otaLastResult = "idle";
static String otaLastError;
static uint32_t monitorStreamLastHeartbeatMs = 0;

static uint32_t otaMaxSketchSpace();

static const uint8_t kDefaultMonitorRegisters[] = {
    static_cast<uint8_t>(LiteonRegister::PsStatus),
    static_cast<uint8_t>(LiteonRegister::ShutdownEvent),
    static_cast<uint8_t>(LiteonRegister::WarningEvent),
    static_cast<uint8_t>(LiteonRegister::InputVoltage),
    static_cast<uint8_t>(LiteonRegister::InputCurrent),
    static_cast<uint8_t>(LiteonRegister::InputPower),
    static_cast<uint8_t>(LiteonRegister::OutputVoltage),
    static_cast<uint8_t>(LiteonRegister::OutputCurrent),
    static_cast<uint8_t>(LiteonRegister::OutputPower),
    static_cast<uint8_t>(LiteonRegister::InletTemperature),
    static_cast<uint8_t>(LiteonRegister::InternalTemperature),
    static_cast<uint8_t>(LiteonRegister::Fan1Speed),
    static_cast<uint8_t>(LiteonRegister::AverageInputPowerCount),
    static_cast<uint8_t>(LiteonRegister::MaxHalfSecondInputPower),
    static_cast<uint8_t>(LiteonRegister::MaxHalfSecondInputCurrent),
    static_cast<uint8_t>(LiteonRegister::PeakOutputCurrent),
    static_cast<uint8_t>(LiteonRegister::PsControl),
};

MonitorState monitor;

static void configureDefaultMonitorRegisters() {
  monitor.registerCount = static_cast<uint8_t>(sizeof(kDefaultMonitorRegisters));
  for (uint8_t index = 0; index < monitor.registerCount; ++index) {
    monitor.registers[index] = kDefaultMonitorRegisters[index];
  }
}

static uint32_t otaMaxSketchSpace() {
  const uint32_t freeSketchSpace = ESP.getFreeSketchSpace();
  if (freeSketchSpace <= 0x1000) {
    return 0;
  }
  return (freeSketchSpace - 0x1000) & 0xFFFFF000;
}

static void prepareForOtaUpdate() {
  monitor.active = false;
  monitor.cycleRunning = false;
  setAllPsuPsonAsserted(false);
}

static void failOtaUpload(const char *result, const String &message) {
  otaUploadRunning = false;
  otaUploadSucceeded = false;
  otaLastResult = result;
  otaLastError = message;
  if (Update.isRunning()) {
    Update.abort();
  }
}

static void failOtaUploadFromUpdate(const char *result, const char *fallback) {
  const char *updateError = Update.errorString();
  failOtaUpload(result, updateError != nullptr && updateError[0] != '\0' ? String(updateError) : String(fallback));
}

static void scheduleOtaRestart() {
  otaRestartPending = true;
  otaRestartAtMs = millis() + kOtaRestartDelayMs;
}

static void serviceOtaRestart() {
  if (!otaRestartPending || !timeReached(millis(), otaRestartAtMs)) {
    return;
  }

  ESP.restart();
}

static void sendCorsHeaders() {
  apiServer.sendHeader("Access-Control-Allow-Origin", "*");
  apiServer.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  apiServer.sendHeader("Access-Control-Allow-Headers",
                       String("Content-Type,") + kOtaConfirmHeader + "," + kAuthHeader);
  apiServer.sendHeader("Cache-Control", "no-store");
}

static void sendJson(int code, const String &body) {
  sendCorsHeaders();
  apiServer.send(code, "application/json", body);
}

static void sendApiError(int code, const char *result, const char *message) {
  String body = "{\"ok\":false,\"result\":";
  body += jsonString(result);
  body += ",\"message\":";
  body += jsonString(message);
  body += '}';
  sendJson(code, body);
}

// Access classes for API routes. With no password stored the device is open,
// exactly as before; once a password exists, everything but authentication
// needs a session unless the route is read-only telemetry and the owner opted
// in to publishing it.
enum class ApiAccess {
  Public,     // authentication itself; never gated
  Telemetry,  // read-only measurements; may be public via auth settings
  Read,       // reads that exist only to drive the UI, so a telemetry viewer
              // can still load a dashboard; never public to a stranger's writes
  Session,    // any state change, including password and OTA
};

static bool tokenValid(const char *token) {
  return token != nullptr && token[0] != '\0' && auth::sessionValid(token);
}

// Accepts an Authorization-style header value, a bare token header, or the
// ?token= query parameter used by EventSource, which cannot set headers.
static bool requestAuthenticated() {
  const String header = apiServer.header(kAuthHeader);
  if (header.length() > 0) {
    if (header.startsWith("Bearer ")) {
      return tokenValid(header.c_str() + 7);
    }
    return tokenValid(header.c_str());
  }
  return tokenValid(apiServer.arg("token").c_str());
}

// Quiet form of the check, for callers that must not emit a response (the OTA
// upload handler runs mid-request and reports through its own status).
static bool accessGranted(ApiAccess access) {
  if (access == ApiAccess::Public || !auth::passwordConfigured()) {
    return true;
  }
  if (requestAuthenticated()) {
    return true;
  }
  // A telemetry viewer drives the dashboard: it may read, and it may configure
  // the register monitor, which only performs reads on the PSUs.
  const bool readOnlyViewer = access == ApiAccess::Telemetry || access == ApiAccess::Read;
  return readOnlyViewer && auth::telemetryPublic();
}

static bool authorize(ApiAccess access) {
  if (accessGranted(access)) {
    return true;
  }

  if (apiServer.header(kAuthHeader).length() > 0 || apiServer.hasArg("token")) {
    sendApiError(401, "invalid-session", "session token is missing or expired");
    return false;
  }
  sendApiError(401, "login-required", "this bridge requires a password");
  return false;
}

static WebServer::THandlerFunction guarded(ApiAccess access, WebServer::THandlerFunction handler) {
  return [access, handler]() {
    if (authorize(access)) {
      handler();
    }
  };
}

static bool decodeHexArg(const char *name, uint8_t *out, size_t length) {
  const String text = apiServer.arg(name);
  if (text.length() != length * 2) {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    uint8_t value = 0;
    for (size_t nibble = 0; nibble < 2; ++nibble) {
      const char ch = text[index * 2 + nibble];
      uint8_t digit = 0;
      if (ch >= '0' && ch <= '9') {
        digit = static_cast<uint8_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        digit = static_cast<uint8_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        digit = static_cast<uint8_t>(ch - 'A' + 10);
      } else {
        return false;
      }
      value = static_cast<uint8_t>((value << 4) | digit);
    }
    out[index] = value;
  }
  return true;
}

static String toHexString(const uint8_t *data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  String out;
  out.reserve(length * 2);
  for (size_t index = 0; index < length; ++index) {
    out += digits[data[index] >> 4];
    out += digits[data[index] & 0x0F];
  }
  return out;
}

static void handleApiAuthStatus() {
  String body = "{\"ok\":true,\"auth\":";
  body += auth::statusJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiAuthChallenge() {
  if (!auth::passwordConfigured()) {
    sendApiError(409, "no-password", "no password is set; every request is allowed");
    return;
  }

  uint8_t clientPublic[srp::kGroupBytes];
  if (!decodeHexArg("A", clientPublic, sizeof(clientPublic))) {
    sendApiError(400, "bad-request", "A must be 256 hex-encoded bytes");
    return;
  }

  String challengeId;
  uint8_t salt[srp::kSaltBytes];
  uint8_t serverPublic[srp::kGroupBytes];
  if (!auth::beginChallenge(clientPublic, challengeId, salt, serverPublic)) {
    sendApiError(400, "bad-request", "client public key is not a valid group element");
    return;
  }

  String body = "{\"ok\":true,\"challenge\":";
  body += jsonString(challengeId);
  body += ",\"salt\":";
  body += jsonString(toHexString(salt, sizeof(salt)));
  body += ",\"B\":";
  body += jsonString(toHexString(serverPublic, sizeof(serverPublic)));
  body += ",\"iterations\":";
  body += String(auth::iterations());
  body += ",\"identity\":";
  body += jsonString(srp::kIdentity);
  body += "}";
  sendJson(200, body);
}

static void handleApiAuthLogin() {
  const String challengeId = apiServer.arg("challenge");
  uint8_t clientProof[srp::kHashBytes];
  if (challengeId.length() == 0 || !decodeHexArg("M1", clientProof, sizeof(clientProof))) {
    sendApiError(400, "bad-request", "challenge and M1 are required");
    return;
  }

  String token;
  uint8_t serverProof[srp::kHashBytes];
  uint32_t retryAfterMs = 0;
  switch (auth::finishChallenge(challengeId.c_str(), clientProof, token, serverProof, retryAfterMs)) {
    case auth::LoginResult::Ok: {
      String body = "{\"ok\":true,\"token\":";
      body += jsonString(token);
      body += ",\"M2\":";
      body += jsonString(toHexString(serverProof, sizeof(serverProof)));
      body += ",\"auth\":";
      body += auth::statusJson();
      body += "}";
      sendJson(200, body);
      return;
    }
    case auth::LoginResult::BadProof:
      if (retryAfterMs > 0) {
        sendApiError(429, "too-many-attempts", "too many failed logins; retry later");
        return;
      }
      sendApiError(401, "bad-password", "password verification failed");
      return;
    case auth::LoginResult::LockedOut:
      sendApiError(429, "too-many-attempts", "too many failed logins; retry later");
      return;
    case auth::LoginResult::UnknownChallenge:
      sendApiError(400, "unknown-challenge", "login challenge expired or unknown");
      return;
    case auth::LoginResult::NotConfigured:
    default:
      sendApiError(409, "no-password", "no password is set; every request is allowed");
      return;
  }
}

static void handleApiAuthLogout() {
  String header = apiServer.header(kAuthHeader);
  if (header.startsWith("Bearer ")) {
    header = header.substring(7);
  }
  const char *token = header.length() > 0 ? header.c_str() : apiServer.arg("token").c_str();
  const bool revoked = token[0] != '\0' && auth::sessionValid(token);
  if (revoked) {
    auth::revokeSession(token);
  }
  String body = "{\"ok\":true,\"revoked\":";
  body += jsonBool(revoked);
  body += ",\"auth\":";
  body += auth::statusJson();
  body += "}";
  sendJson(200, body);
}

// The browser derives (v, salt) locally, so the password never reaches the
// device. Changing it re-issues the caller's session because every older
// session is revoked.
static void handleApiAuthSetPassword() {
  uint8_t verifier[srp::kVerifierBytes];
  uint8_t salt[srp::kSaltBytes];
  if (!decodeHexArg("verifier", verifier, sizeof(verifier)) ||
      !decodeHexArg("salt", salt, sizeof(salt))) {
    sendApiError(400, "bad-request", "verifier (256 bytes) and salt (16 bytes) must be hex-encoded");
    return;
  }

  uint32_t iterations = srp::kDefaultIterations;
  const String iterationsRaw = apiServer.hasArg("iterations") ? apiServer.arg("iterations") : String();
  if (iterationsRaw.length() > 0 && !parseUnsignedToken(iterationsRaw.c_str(), srp::kMaxIterations, iterations)) {
    sendApiError(400, "bad-request", "iterations must be a positive integer");
    return;
  }

  String token;
  if (!auth::setPassword(verifier, salt, iterations, token)) {
    sendApiError(400, "bad-request", "verifier or iteration count is out of range");
    return;
  }

  String body = "{\"ok\":true,\"token\":";
  body += jsonString(token);
  body += ",\"auth\":";
  body += auth::statusJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiAuthClearPassword() {
  auth::clearPassword();
  String body = "{\"ok\":true,\"auth\":";
  body += auth::statusJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiAuthTelemetryPublic() {
  bool enabled = false;
  const String raw = apiServer.arg("enabled");
  if (raw.length() == 0 || !parseBoolToken(raw.c_str(), enabled)) {
    sendApiError(400, "bad-request", "enabled must be true or false");
    return;
  }
  auth::setTelemetryPublic(enabled);
  String body = "{\"ok\":true,\"auth\":";
  body += auth::statusJson();
  body += "}";
  sendJson(200, body);
}

static const PramonAsset *findFrontendAsset(const String &requestPath) {
  String path = requestPath;
  if (path.length() == 0 || path == "/") {
    path = "/index.html";
  }

  for (size_t index = 0; index < kPramonAssetCount; ++index) {
    if (path == kPramonAssets[index].path) {
      return &kPramonAssets[index];
    }
  }
  return nullptr;
}

static void sendFrontendAsset(const PramonAsset &asset) {
  if (asset.gzip) {
    apiServer.sendHeader("Content-Encoding", "gzip");
  }
  if (String(asset.path) == "/index.html") {
    apiServer.sendHeader("Cache-Control", "no-store");
  } else {
    apiServer.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  }
  apiServer.send_P(200, asset.contentType, reinterpret_cast<const char *>(asset.data), asset.length);
}

static bool serveFrontendPath(const String &requestPath) {
  if (const PramonAsset *asset = findFrontendAsset(requestPath)) {
    sendFrontendAsset(*asset);
    return true;
  }

  if (requestPath.indexOf('.') < 0) {
    if (const PramonAsset *indexAsset = findFrontendAsset("/index.html")) {
      sendFrontendAsset(*indexAsset);
      return true;
    }
  }

  return false;
}

static bool parseApiU8Arg(const char *name, uint8_t &value) {
  const String raw = apiServer.arg(name);
  return raw.length() > 0 && parseU8Token(raw.c_str(), value);
}

static bool parseApiBoolArg(const char *name, bool &value) {
  const String raw = apiServer.arg(name);
  return raw.length() > 0 && parseBoolToken(raw.c_str(), value);
}

static bool parseApiRegisterArg(const char *name, uint8_t &value) {
  const String raw = apiServer.arg(name);
  return raw.length() > 0 && parseRegisterToken(raw.c_str(), value);
}

static bool parseApiRegisterList(uint8_t *registers, uint8_t maxRegisters, uint8_t &registerCount) {
  registerCount = 0;
  String raw = apiServer.arg("regs");
  if (raw.length() == 0) {
    raw = apiServer.arg("reg");
  }

  if (raw.length() == 0) {
    const uint8_t defaultCount = static_cast<uint8_t>(sizeof(kDefaultMonitorRegisters));
    if (defaultCount > maxRegisters) {
      return false;
    }
    for (uint8_t index = 0; index < defaultCount; ++index) {
      registers[index] = kDefaultMonitorRegisters[index];
    }
    registerCount = defaultCount;
    return true;
  }

  int tokenStart = 0;
  for (int index = 0; index <= raw.length(); ++index) {
    const char ch = index < raw.length() ? raw[index] : ',';
    if (ch != ',' && ch != ';' && ch != ' ' && ch != '\t') {
      continue;
    }

    String token = raw.substring(tokenStart, index);
    token.trim();
    tokenStart = index + 1;
    if (token.length() == 0) {
      continue;
    }
    if (registerCount >= maxRegisters) {
      return false;
    }

    uint8_t reg = 0;
    if (!parseRegisterToken(token.c_str(), reg)) {
      return false;
    }
    registers[registerCount++] = reg;
  }

  return registerCount > 0;
}

static bool parseApiU16Arg(const char *name, uint16_t &value) {
  const String raw = apiServer.arg(name);
  return raw.length() > 0 && parseU16Token(raw.c_str(), value);
}

static bool parseApiSlotArg(PsuSlot *&slot) {
  if (!apiServer.hasArg("slot")) {
    slot = &defaultPsuSlot();
    return true;
  }

  const String raw = apiServer.arg("slot");
  if (raw.length() == 0) {
    slot = &defaultPsuSlot();
    return true;
  }

  if (tokenIsAll(raw.c_str())) {
    slot = nullptr;
    return true;
  }

  PsuSlot *parsedSlot = nullptr;
  if (!parsePsuSlotToken(raw.c_str(), parsedSlot)) {
    sendApiError(400, "bad-request", "slot must be 1, 2, 3, or all");
    return false;
  }
  slot = parsedSlot;
  return true;
}

static String otaJson() {
  String body = "{";
  body += "\"enabled\":true";
  body += ",\"path\":\"/api/v1/ota\"";
  body += ",\"field\":\"firmware\"";
  body += ",\"confirmHeader\":";
  body += jsonString(String(kOtaConfirmHeader) + ": 1");
  body += ",\"running\":";
  body += jsonBool(otaUploadRunning);
  body += ",\"restartPending\":";
  body += jsonBool(otaRestartPending);
  body += ",\"bytesWritten\":";
  body += String(otaBytesWritten);
  body += ",\"maxSketchBytes\":";
  body += String(otaMaxSketchSpace());
  body += ",\"lastResult\":";
  body += jsonString(otaLastResult);
  body += ",\"lastError\":";
  body += jsonString(otaLastError);
  body += "}";
  return body;
}

static String monitorSlotJsonValue() {
  if (monitor.allSlots) {
    return "\"all\"";
  }
  return String(psuSlots[monitor.slotIndex].id);
}

static String monitorRegistersJson() {
  String body = "[";
  for (uint8_t index = 0; index < monitor.registerCount; ++index) {
    if (index > 0) {
      body += ',';
    }
    body += jsonString(hex8(monitor.registers[index]));
  }
  body += "]";
  return body;
}

static String monitorStreamUrl() {
  if (!wifiIsConnected()) {
    return "";
  }
  return String("http://") + WiFi.localIP().toString() + ":" + String(kMonitorStreamPort) + kMonitorStreamPath;
}

static String monitorJson() {
  String body = "{";
  body += "\"active\":";
  body += jsonBool(monitor.active);
  body += ",\"slot\":";
  body += monitorSlotJsonValue();
  body += ",\"intervalMs\":";
  body += String(monitor.intervalMs);
  body += ",\"count\":";
  body += String(monitor.registerCount);
  body += ",\"registers\":";
  body += monitorRegistersJson();
  body += ",\"cadence\":";
  body += jsonString(psu.respectSequenceSpacing() ? "normal" : "fast");
  body += ",\"cycle\":";
  body += String(monitor.cycle);
  body += ",\"stream\":{\"path\":";
  body += jsonString(kMonitorStreamPath);
  body += ",\"port\":";
  body += String(kMonitorStreamPort);
  body += ",\"connected\":";
  body += jsonBool(monitorStreamClient && monitorStreamClient.connected());
  body += ",\"url\":";
  body += jsonString(monitorStreamUrl());
  body += "}}";
  return body;
}

static bool monitorStreamConnected() {
  return monitorStreamClient && monitorStreamClient.connected();
}

static void stopMonitorStreamClient() {
  if (monitorStreamClient) {
    monitorStreamClient.stop();
  }
}

static bool writeMonitorStreamFrame(const String &frame) {
  if (!monitorStreamConnected() || frame.length() == 0) {
    return false;
  }

  const int socketFd = monitorStreamClient.fd();
  if (socketFd < 0) {
    stopMonitorStreamClient();
    return false;
  }

  const ssize_t written = send(socketFd, frame.c_str(), frame.length(), MSG_DONTWAIT);
  if (written != static_cast<ssize_t>(frame.length())) {
    stopMonitorStreamClient();
    return false;
  }
  return true;
}

static void streamMonitorEvent(const char *eventName, const String &data) {
  if (!monitorStreamConnected()) {
    return;
  }

  String frame;
  frame.reserve(strlen(eventName) + data.length() + 18);
  frame += "event: ";
  frame += eventName;
  frame += "\ndata: ";
  frame += data;
  frame += "\n\n";
  if (writeMonitorStreamFrame(frame)) {
    monitorStreamLastHeartbeatMs = millis();
  }
}

static void streamMonitorCycle(uint32_t cycle, const char *phase) {
  String body = "{\"type\":\"cycle\",\"cycle\":";
  body += String(cycle);
  body += ",\"slot\":";
  body += monitorSlotJsonValue();
  body += ",\"phase\":";
  body += jsonString(phase);
  body += ",\"ms\":";
  body += String(millis());
  body += "}";
  streamMonitorEvent("monitor", body);
}

static void streamMonitorPins(uint32_t cycle, const PsuSlot &slot) {
  String body = "{\"type\":\"pins\",\"cycle\":";
  body += String(cycle);
  body += ",\"slot\":";
  body += String(slot.id);
  body += ",\"ms\":";
  body += String(millis());
  body += ",\"pins\":";
  body += pinsJson(slot);
  body += "}";
  streamMonitorEvent("monitor", body);
}

static void streamMonitorRegister(uint32_t cycle, const PsuSlot &slot, uint8_t reg, LiteonResult result, uint16_t value) {
  String body = "{\"type\":\"register\",\"cycle\":";
  body += String(cycle);
  body += ",\"slot\":";
  body += String(slot.id);
  body += ",\"address\":";
  body += jsonString(hex8(slot.device->address7()));
  body += ",\"register\":";
  body += jsonString(hex8(reg));
  body += ",\"ms\":";
  body += String(millis());
  body += ",\"result\":";
  body += jsonString(resultName(result));
  if (result == LiteonResult::Ok) {
    body += ",\"value\":";
    body += jsonString(hex16(value));
  }
  body += "}";
  streamMonitorEvent("monitor", body);
}

static void streamMonitorError(uint32_t cycle, const PsuSlot &slot, const char *result) {
  String body = "{\"type\":\"error\",\"cycle\":";
  body += String(cycle);
  body += ",\"slot\":";
  body += String(slot.id);
  body += ",\"address\":";
  body += jsonString(hex8(slot.device->address7()));
  body += ",\"ms\":";
  body += String(millis());
  body += ",\"result\":";
  body += jsonString(result);
  body += "}";
  streamMonitorEvent("monitor", body);
}

static void sendMonitorStreamNotFound(WiFiClient &client) {
  client.print("HTTP/1.1 404 Not Found\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print("{\"ok\":false,\"result\":\"not-found\"}");
  client.stop();
}

static void sendMonitorStreamUnauthorized(WiFiClient &client) {
  client.print("HTTP/1.1 401 Unauthorized\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print("{\"ok\":false,\"result\":\"login-required\"}");
  client.stop();
}

static void sendMonitorStreamOptions(WiFiClient &client) {
  client.print("HTTP/1.1 204 No Content\r\n");
  client.print("Access-Control-Allow-Origin: *\r\n");
  client.print("Access-Control-Allow-Methods: GET,OPTIONS\r\n");
  client.print(String("Access-Control-Allow-Headers: Content-Type,") + kAuthHeader + "\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.print("Connection: close\r\n\r\n");
  client.stop();
}

static void acceptMonitorStreamClient(WiFiClient &client) {
  stopMonitorStreamClient();
  monitorStreamClient = client;
  monitorStreamClient.setNoDelay(true);
  const String headers =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-store\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Connection: keep-alive\r\n\r\n";
  if (!writeMonitorStreamFrame(headers)) {
    return;
  }
  monitorStreamLastHeartbeatMs = millis();
  streamMonitorEvent("monitor", String("{\"type\":\"hello\",\"ms\":") + String(monitorStreamLastHeartbeatMs) + ",\"monitor\":" + monitorJson() + "}");
}

static void serviceMonitorStreamServer() {
  if (!wifiIsConnected()) {
    stopMonitorStreamClient();
    return;
  }

  if (monitorStreamClient && !monitorStreamClient.connected()) {
    monitorStreamClient.stop();
  }

  WiFiClient pending = monitorStreamServer.available();
  if (pending) {
    pending.setTimeout(250);
    const String requestLine = pending.readStringUntil('\n');
    const bool isOptions = requestLine.startsWith("OPTIONS ");
    const bool isGet = requestLine.startsWith("GET ");
    const bool isStreamPath = requestLine.indexOf(String(" ") + kMonitorStreamPath + " ") >= 0 ||
                              requestLine.indexOf(String(" ") + kMonitorStreamPath + "?") >= 0;

    // EventSource cannot set request headers, so the stream also accepts the
    // session token as a query parameter.
    String streamToken;
    const int queryStart = requestLine.indexOf('?');
    if (queryStart >= 0) {
      const int queryEnd = requestLine.indexOf(' ', queryStart);
      const String query = requestLine.substring(queryStart + 1, queryEnd < 0 ? requestLine.length() : queryEnd);
      int cursor = 0;
      while (cursor < query.length()) {
        int next = query.indexOf('&', cursor);
        if (next < 0) {
          next = query.length();
        }
        const String pair = query.substring(cursor, next);
        const int equals = pair.indexOf('=');
        if (equals > 0 && pair.substring(0, equals) == "token") {
          streamToken = pair.substring(equals + 1);
        }
        cursor = next + 1;
      }
    }

    while (pending.connected()) {
      const String header = pending.readStringUntil('\n');
      if (header == "\r" || header.length() == 0) {
        break;
      }
      const int colon = header.indexOf(':');
      if (colon > 0 && header.substring(0, colon).equalsIgnoreCase(kAuthHeader)) {
        String value = header.substring(colon + 1);
        value.trim();
        if (value.startsWith("Bearer ")) {
          value = value.substring(7);
        }
        streamToken = value;
      }
    }

    if (isOptions) {
      sendMonitorStreamOptions(pending);
    } else if (!isGet || !isStreamPath) {
      sendMonitorStreamNotFound(pending);
    } else if (auth::passwordConfigured() && !auth::telemetryPublic() && !tokenValid(streamToken.c_str())) {
      sendMonitorStreamUnauthorized(pending);
    } else {
      acceptMonitorStreamClient(pending);
    }
  }

  if (monitorStreamConnected() &&
      timeReached(millis(), monitorStreamLastHeartbeatMs + kMonitorStreamHeartbeatMs)) {
    if (writeMonitorStreamFrame(": keepalive\n\n")) {
      monitorStreamLastHeartbeatMs = millis();
    }
  }
}

static String firmwareInfoJson() {
  String body = "{";
  body += "\"ok\":true,\"target\":\"esp32s3-pmbus-bridge\"";
  body += ",\"firmware\":";
  body += jsonString(PRAMON_FW_VERSION);
  body += ",\"protocol\":";
  body += String(PRAMON_PROTOCOL_VERSION);
  body += ",\"build\":";
  body += jsonString(buildStamp());
  body += ",\"cadence\":";
  body += jsonString(psu.respectSequenceSpacing() ? "normal" : "fast");
  body += ",\"address\":";
  body += jsonString(hex8(psu.address7()));
  body += ",\"detected\":";
  body += jsonBool(liteonAddressDetected);
  body += ",\"slotCount\":";
  body += String(kPsuSlotCount);
  body += ",\"slots\":";
  body += psuSlotsJson();
  body += ",\"powerPolicy\":";
  body += powerPolicyJson();
  body += ",\"api\":{\"enabled\":true,\"base\":\"/api/v1\",\"port\":";
  body += String(kApiPort);
  body += ",\"monitorStream\":{\"path\":";
  body += jsonString(kMonitorStreamPath);
  body += ",\"port\":";
  body += String(kMonitorStreamPort);
  body += "}},\"wifi\":";
  body += wifiJson();
  body += ",\"ota\":";
  body += otaJson();
  body += ",\"influx\":";
  body += influxJson();
  body += ",\"auth\":";
  body += auth::statusJson();
  body += "}";
  return body;
}

static void handleApiInfo() {
  sendJson(200, firmwareInfoJson());
}

static void handleApiWifiStatus() {
  String body = "{\"ok\":true,\"wifi\":";
  body += wifiJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiWifiConnect() {
  const String ssid = apiServer.arg("ssid");
  const String password = apiServer.arg("password");
  if (ssid.length() == 0) {
    sendApiError(400, "bad-request", "missing ssid query parameter");
    return;
  }

  startWifiConnect(ssid.c_str(), password.c_str());
  const bool connected = waitForWifiConnect(kWifiConnectTimeoutMs);
  if (connected && apiServer.arg("remember") != "0") {
    rememberWifiCredentials(ssid.c_str(), password.c_str());
  }

  String body = "{\"ok\":";
  body += jsonBool(connected);
  body += ",\"wifi\":";
  body += wifiJson();
  body += "}";
  sendJson(connected ? 200 : 504, body);
}

static void handleApiWifiReconnect() {
  if (wifiIsConnected()) {
    String body = "{\"ok\":true,\"wifi\":";
    body += wifiJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  String ssid, password;
  if (!getRememberedCredentials(ssid, password)) {
    sendApiError(400, "no-remembered-credentials", "no remembered WiFi credentials");
    return;
  }

  const bool connected = reconnectWifi(kWifiConnectTimeoutMs);
  String body = "{\"ok\":";
  body += jsonBool(connected);
  body += ",\"wifi\":";
  body += wifiJson();
  body += "}";
  sendJson(connected ? 200 : 504, body);
}

static void handleApiWifiDisconnect() {
  disconnectWifi(false);
  String body = "{\"ok\":true,\"wifi\":";
  body += wifiJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiWifiForget() {
  forgetWifi();
  String body = "{\"ok\":true,\"wifi\":";
  body += wifiJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiOtaStatus() {
  String body = "{\"ok\":true,\"ota\":";
  body += otaJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiOtaComplete() {
  if (apiServer.header(kOtaConfirmHeader) != "1") {
    sendApiError(403, "confirmation-required", "missing X-PowerRAID-OTA: 1 header");
    return;
  }

  if (!otaUploadSucceeded) {
    const String result = otaLastResult == "idle" ? String("no-upload") : otaLastResult;
    const String message = otaLastError.length() > 0 ? otaLastError : String("no firmware upload received");
    sendApiError(400, result.c_str(), message.c_str());
    return;
  }

  scheduleOtaRestart();
  String body = "{\"ok\":true,\"result\":\"accepted\",\"bytesWritten\":";
  body += String(otaBytesWritten);
  body += ",\"rebootMs\":";
  body += String(kOtaRestartDelayMs);
  body += ",\"ota\":";
  body += otaJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiOtaUpload() {
  HTTPUpload &upload = apiServer.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaUploadRunning = false;
    otaUploadSucceeded = false;
    otaRestartPending = false;
    otaBytesWritten = 0;
    otaLastResult = "running";
    otaLastError = "";

    // Authorize before the first byte reaches flash. The completion handler
    // runs once and reports the refusal, so no response is sent here.
    if (!accessGranted(ApiAccess::Session)) {
      failOtaUpload("login-required", "a session token is required to update firmware");
      return;
    }
    if (apiServer.header(kOtaConfirmHeader) != "1") {
      failOtaUpload("confirmation-required", "missing X-PowerRAID-OTA: 1 header");
      return;
    }

    prepareForOtaUpdate();
    const uint32_t maxSketchSpace = otaMaxSketchSpace();
    if (maxSketchSpace == 0 || !Update.begin(maxSketchSpace, U_FLASH)) {
      failOtaUploadFromUpdate("begin-failed", "could not start OTA update");
      return;
    }

    otaUploadRunning = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaUploadRunning || otaLastError.length() > 0) {
      return;
    }
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      failOtaUploadFromUpdate("write-failed", "firmware flash write failed");
      return;
    }
    otaBytesWritten += upload.currentSize;
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!otaUploadRunning || otaLastError.length() > 0) {
      if (Update.isRunning()) {
        Update.abort();
      }
      return;
    }
    if (!Update.end(true)) {
      failOtaUploadFromUpdate("end-failed", "firmware image finalize failed");
      return;
    }

    otaUploadRunning = false;
    otaUploadSucceeded = true;
    otaLastResult = "accepted";
    otaLastError = "";
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    failOtaUpload("aborted", "firmware upload was aborted");
  }
}

static void handleApiInflux() {
  if (apiServer.method() == HTTP_GET) {
    String body = "{\"ok\":true,\"influx\":";
    body += influxJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  if (apiServer.hasArg("enabled")) {
    bool enabled = false;
    if (!parseApiBoolArg("enabled", enabled)) {
      sendApiError(400, "bad-request", "enabled must be true or false");
      return;
    }
    influx.enabled = enabled;
    if (!influx.enabled) {
      clearInfluxImonQueue();
    }
  }
  if (apiServer.hasArg("url")) {
    influx.url = normalizedInfluxUrl(apiServer.arg("url"));
  }
  if (apiServer.hasArg("org")) {
    influx.org = apiServer.arg("org");
    influx.org.trim();
  }
  if (apiServer.hasArg("bucket")) {
    influx.bucket = apiServer.arg("bucket");
    influx.bucket.trim();
  }
  if (apiServer.hasArg("measurement")) {
    influx.measurement = apiServer.arg("measurement");
    influx.measurement.trim();
    if (influx.measurement.length() == 0) {
      influx.measurement = "powerraid_psu";
    }
  }
  if (apiServer.hasArg("token")) {
    influx.token = apiServer.arg("token");
    influx.token.trim();
  }
  if (apiServer.hasArg("clearToken")) {
    bool clearToken = false;
    if (!parseApiBoolArg("clearToken", clearToken)) {
      sendApiError(400, "bad-request", "clearToken must be true or false");
      return;
    }
    if (clearToken) {
      influx.token = "";
    }
  }
  if (apiServer.hasArg("intervalMs")) {
    uint32_t intervalMs = kInfluxDefaultIntervalMs;
    if (!parseUnsignedToken(apiServer.arg("intervalMs").c_str(), kInfluxMaxIntervalMs, intervalMs)) {
      sendApiError(400, "bad-request", "intervalMs must be a positive millisecond value");
      return;
    }
    influx.intervalMs = clampInfluxInterval(intervalMs);
  }

  if (influx.enabled && !influxConfigured()) {
    sendApiError(400, "not-configured", "enabled InfluxDB reporting requires url, org, bucket, and token");
    return;
  }

  saveInfluxConfig();
  influx.nextReportMs = millis() + influx.intervalMs;
  String body = "{\"ok\":true,\"influx\":";
  body += influxJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiAtx() {
  const String state = apiServer.arg("state");
  if (apiServer.method() == HTTP_POST && state.length() > 0) {
    if (tokenEquals(state.c_str(), "auto") || tokenEquals(state.c_str(), "track")) {
      atx.trackAutomatic = true;
      atx.requestAsserted = digitalRead(PIN_ATX_PS_ON_N) == LOW;
      atx.rawAsserted = atx.requestAsserted;
      atx.rawChangedMs = millis();
    } else if (tokenEquals(state.c_str(), "on") || tokenEquals(state.c_str(), "assert") || state == "1") {
      atx.trackAutomatic = false;
      atx.requestAsserted = true;
    } else if (tokenEquals(state.c_str(), "off") || tokenEquals(state.c_str(), "release") || state == "0") {
      atx.trackAutomatic = false;
      atx.requestAsserted = false;
    } else if (!tokenEquals(state.c_str(), "status")) {
      sendApiError(400, "bad-request", "state must be status, auto, on, or off");
      return;
    }
    servicePowerPolicy(true);
  }

  String body = "{\"ok\":true,\"atx\":";
  body += atxJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiPins() {
  PsuSlot *slot = nullptr;
  if (!parseApiSlotArg(slot)) {
    return;
  }

  String body = "{\"ok\":true,\"pins\":";
  body += slot == nullptr ? psuSlotsJson() : pinsJson(*slot);
  body += "}";
  sendJson(200, body);
}

static void handleApiPsus() {
  String body = "{\"ok\":true,\"count\":";
  body += String(kPsuSlotCount);
  body += ",\"psus\":";
  body += psuSlotsJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiPowerPolicy() {
  if (apiServer.method() == HTTP_GET && apiServer.args() == 0) {
    servicePowerPolicy(false);
    String body = "{\"ok\":true,\"powerPolicy\":";
    body += powerPolicyJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  bool changed = false;

  if (apiServer.hasArg("automatic") || apiServer.hasArg("enabled")) {
    bool automatic = false;
    const char *argName = apiServer.hasArg("automatic") ? "automatic" : "enabled";
    if (!parseApiBoolArg(argName, automatic)) {
      sendApiError(400, "bad-request", "automatic/enabled must be true or false");
      return;
    }
    setPowerPolicyAutomatic(automatic, true);
    changed = true;
  }

  if (apiServer.hasArg("state")) {
    const String state = apiServer.arg("state");
    if (!tokenEquals(state.c_str(), "status")) {
      if (tokenEquals(state.c_str(), "off") || tokenEquals(state.c_str(), "release")) {
        setPowerPolicyManualPson(nullptr, false, true);
      } else {
        bool automatic = false;
        if (!parseBoolToken(state.c_str(), automatic)) {
          sendApiError(400, "bad-request", "state must be status, auto, manual, or off");
          return;
        }
        setPowerPolicyAutomatic(automatic, true);
      }
      changed = true;
    }
  }

  if (apiServer.hasArg("n_redundancy") || apiServer.hasArg("nRedundancy")) {
    const String raw = apiServer.hasArg("n_redundancy") ? apiServer.arg("n_redundancy") : apiServer.arg("nRedundancy");
    uint32_t parsed = 0;
    if (!parseUnsignedToken(raw.c_str(), kMaxNRedundancy, parsed)) {
      sendApiError(400, "bad-request", "n_redundancy must be 0, 1, or 2");
      return;
    }
    setPowerPolicyNRedundancy(static_cast<uint8_t>(parsed), true);
    changed = true;
  }

  if (apiServer.hasArg("efficiency_profile") || apiServer.hasArg("efficiencyProfile")) {
    const String raw = apiServer.hasArg("efficiency_profile")
        ? apiServer.arg("efficiency_profile")
        : apiServer.arg("efficiencyProfile");
    if (!setEfficiencyProfile(raw.c_str(), true)) {
      sendApiError(400, "bad-request", "unknown efficiency_profile");
      return;
    }
    changed = true;
  }

  servicePowerPolicy(true);
  String body = "{\"ok\":true,\"changed\":";
  body += jsonBool(changed);
  body += ",\"powerPolicy\":";
  body += powerPolicyJson();
  body += ",\"atx\":";
  body += atxJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiProbe() {
  if (apiServer.hasArg("slot")) {
    PsuSlot *slot = nullptr;
    if (!parseApiSlotArg(slot)) {
      return;
    }

    if (slot == nullptr) {
      String body = "{\"ok\":true,\"probes\":[";
      bool first = true;
      uint8_t okCount = 0;
      for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
        PsuSlot &candidate = psuSlots[index];
        uint16_t ucInfo = 0;
        const LiteonResult result = probePsuSlot(candidate, ucInfo);
        if (!first) body += ',';
        first = false;
        if (result == LiteonResult::Ok) {
          ++okCount;
        }
        body += "{\"slot\":";
        body += String(candidate.id);
        body += ",\"address\":";
        body += jsonString(hex8(candidate.device->address7()));
        body += ",\"present\":";
        body += jsonBool(slotIsPresent(candidate));
        body += ",\"result\":";
        body += jsonString(resultName(result));
        if (result == LiteonResult::Ok) {
          body += ",\"ucInfo\":";
          body += jsonString(hex16(ucInfo));
        }
        body += "}";
      }
      body += "],\"okCount\":";
      body += String(okCount);
      body += "}";
      sendJson(200, body);
      return;
    }

    uint16_t ucInfo = 0;
    const LiteonResult result = probePsuSlot(*slot, ucInfo);
    String body = "{\"ok\":";
    body += jsonBool(result == LiteonResult::Ok);
    body += ",\"slot\":";
    body += String(slot->id);
    body += ",\"address\":";
    body += jsonString(hex8(slot->device->address7()));
    body += ",\"result\":";
    body += jsonString(resultName(result));
    if (result == LiteonResult::Ok) {
      body += ",\"ucInfo\":";
      body += jsonString(hex16(ucInfo));
    }
    body += "}";
    sendJson(result == LiteonResult::Ok ? 200 : 404, body);
    return;
  }

  String attempts = "[";
  LiteonResult lastNonNack = LiteonResult::I2cNack;
  bool sawNonNack = false;
  bool first = true;
  const uint8_t originalAddress7 = psu.address7();
  for (uint8_t addPins = 0; addPins < kLiteonAddressPinCount; ++addPins) {
    const uint8_t address7 = LiteonPsu::monitoringAddress7(addPins);
    psu.setAddress7(address7);
    uint16_t ucInfo = 0;
    const LiteonResult result = psu.readRaw(LiteonRegister::UcInfo, ucInfo);

    if (!first) attempts += ',';
    first = false;
    attempts += "{\"add\":";
    attempts += String(addPins);
    attempts += ",\"address\":";
    attempts += jsonString(hex8(address7));
    attempts += ",\"result\":";
    attempts += jsonString(resultName(result));
    if (result == LiteonResult::Ok) {
      attempts += ",\"ucInfo\":";
      attempts += jsonString(hex16(ucInfo));
    }
    attempts += "}";

    if (result == LiteonResult::Ok) {
      liteonAddressDetected = true;
      defaultPsuSlot().detected = true;
      String body = "{\"ok\":true,\"address\":";
      body += jsonString(hex8(psu.address7()));
      body += ",\"ucInfo\":";
      body += jsonString(hex16(ucInfo));
      body += ",\"attempts\":";
      body += attempts;
      body += "]}";
      sendJson(200, body);
      return;
    }
    if (result != LiteonResult::I2cNack) {
      sawNonNack = true;
      lastNonNack = result;
    }
  }

  liteonAddressDetected = false;
  defaultPsuSlot().detected = false;
  psu.setAddress7(originalAddress7);
  attempts += "]";
  String body = "{\"ok\":false,\"result\":";
  body += jsonString(resultName(sawNonNack ? lastNonNack : LiteonResult::I2cNack));
  body += ",\"attempts\":";
  body += attempts;
  body += "}";
  sendJson(404, body);
}

static void handleApiRead() {
  uint8_t reg = 0;
  if (!parseApiRegisterArg("reg", reg)) {
    sendApiError(400, "bad-request", "missing or invalid even reg query parameter");
    return;
  }

  PsuSlot *slot = nullptr;
  const bool slotProvided = apiServer.hasArg("slot");
  if (slotProvided && !parseApiSlotArg(slot)) {
    return;
  }
  if (slotProvided && slot == nullptr) {
    if (slot == nullptr) {
      sendApiError(400, "bad-request", "read requires a single slot");
    }
    return;
  }

  if (slotProvided ? !ensurePsuSlotDetected(*slot) : !ensureLiteonAddressSelected()) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }

  uint16_t value = 0;
  LiteonPsu &device = slotProvided ? *slot->device : psu;
  const LiteonResult result = device.readRaw(static_cast<LiteonRegister>(reg), value);
  if (result != LiteonResult::Ok) {
    if (result == LiteonResult::I2cNack) {
      if (slotProvided) {
        slot->detected = false;
      } else {
        liteonAddressDetected = false;
        defaultPsuSlot().detected = false;
      }
    }
    sendApiError(502, resultName(result), "LiteOn register read failed");
    return;
  }

  String body = "{\"ok\":true,\"address\":";
  body += jsonString(hex8(device.address7()));
  if (slotProvided) {
    body += ",\"slot\":";
    body += String(slot->id);
  }
  body += ",\"register\":";
  body += jsonString(hex8(reg));
  body += ",\"value\":";
  body += jsonString(hex16(value));
  body += "}";
  sendJson(200, body);
}

static void handleApiWrite() {
  uint8_t reg = 0;
  uint16_t value = 0;
  if (!parseApiRegisterArg("reg", reg) || !parseApiU16Arg("value", value)) {
    sendApiError(400, "bad-request", "missing or invalid even reg/value query parameters");
    return;
  }
  PsuSlot *slot = nullptr;
  const bool slotProvided = apiServer.hasArg("slot");
  if (slotProvided && !parseApiSlotArg(slot)) {
    return;
  }
  if (slotProvided && slot == nullptr) {
    if (slot == nullptr) {
      sendApiError(400, "bad-request", "write requires a single slot");
    }
    return;
  }

  if (slotProvided ? !ensurePsuSlotDetected(*slot) : !ensureLiteonAddressSelected()) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }

  LiteonPsu &device = slotProvided ? *slot->device : psu;
  const LiteonResult result = device.writeRaw(static_cast<LiteonRegister>(reg), value);
  if (result != LiteonResult::Ok) {
    if (result == LiteonResult::I2cNack) {
      if (slotProvided) {
        slot->detected = false;
      } else {
        liteonAddressDetected = false;
        defaultPsuSlot().detected = false;
      }
    }
    sendApiError(502, resultName(result), "LiteOn register write failed");
    return;
  }

  String body = "{\"ok\":true,\"address\":";
  body += jsonString(hex8(device.address7()));
  if (slotProvided) {
    body += ",\"slot\":";
    body += String(slot->id);
  }
  body += ",\"register\":";
  body += jsonString(hex8(reg));
  body += ",\"value\":";
  body += jsonString(hex16(value));
  body += "}";
  sendJson(200, body);
}

static void handleApiAddress() {
  PsuSlot *slot = nullptr;
  const bool slotProvided = apiServer.hasArg("slot");
  if (slotProvided && !parseApiSlotArg(slot)) {
    return;
  }
  if (slotProvided && slot == nullptr) {
    sendApiError(400, "bad-request", "address requires a single slot");
    return;
  }
  LiteonPsu &device = slotProvided ? *slot->device : psu;

  if (apiServer.method() == HTTP_GET || !apiServer.hasArg("address")) {
    String body = "{\"ok\":true,\"address\":";
    body += jsonString(hex8(device.address7()));
    body += ",\"detected\":";
    body += jsonBool(slotProvided ? slot->detected : liteonAddressDetected);
    if (slotProvided) {
      body += ",\"slot\":";
      body += String(slot->id);
    }
    body += "}";
    sendJson(200, body);
    return;
  }

  uint8_t address7 = 0;
  if (!parseApiU8Arg("address", address7) || address7 > 0x7F) {
    sendApiError(400, "bad-request", "invalid 7-bit address query parameter");
    return;
  }
  device.setAddress7(address7);
  if (slotProvided) {
    slot->detected = true;
  } else {
    liteonAddressDetected = true;
    defaultPsuSlot().detected = true;
  }
  String body = "{\"ok\":true,\"address\":";
  body += jsonString(hex8(device.address7()));
  if (slotProvided) {
    body += ",\"slot\":";
    body += String(slot->id);
  }
  body += ",\"detected\":true}";
  sendJson(200, body);
}

static void handleApiPson() {
  PsuSlot *slot = nullptr;
  if (!parseApiSlotArg(slot)) {
    return;
  }

  const String state = apiServer.arg("state");
  if (state.length() == 0 || tokenEquals(state.c_str(), "status")) {
    String body = "{\"ok\":true,\"pson\":{\"asserted\":";
    if (slot == nullptr) {
      bool allAsserted = true;
      for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
        allAsserted = allAsserted && psuSlots[index].psonAsserted;
      }
      body += jsonBool(allAsserted);
      body += ",\"slot\":\"all\",\"slots\":";
      body += psuSlotsJson();
    } else {
      body += jsonBool(slot->psonAsserted);
      body += ",\"slot\":";
      body += String(slot->id);
      body += ",\"psonNPin\":";
      body += digitalRead(slot->psonPin);
    }
    body += ",\"automatic\":";
    body += jsonBool(powerPolicy.automatic);
    body += ",\"powerPolicy\":";
    body += powerPolicyJson();
    body += "}}";
    sendJson(200, body);
    return;
  }
  if (tokenEquals(state.c_str(), "auto") || tokenEquals(state.c_str(), "automatic")) {
    setPowerPolicyAutomatic(true, true);
  } else if (tokenEquals(state.c_str(), "manual")) {
    setPowerPolicyAutomatic(false, true);
  } else if (tokenEquals(state.c_str(), "on") || tokenEquals(state.c_str(), "assert") || state == "1") {
    setPowerPolicyManualPson(slot, true, true);
  } else if (tokenEquals(state.c_str(), "off") || tokenEquals(state.c_str(), "release") ||
             state == "0") {
    setPowerPolicyManualPson(slot, false, true);
  } else {
    sendApiError(400, "bad-request", "state must be status, auto, manual, on, or off");
    return;
  }

  servicePowerPolicy(true);

  String body = "{\"ok\":true,\"pson\":{\"asserted\":";
  if (slot == nullptr) {
    bool allAsserted = true;
    for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
      allAsserted = allAsserted && psuSlots[index].psonAsserted;
    }
    body += jsonBool(allAsserted);
    body += ",\"slot\":\"all\",\"slots\":";
    body += psuSlotsJson();
  } else {
    body += jsonBool(slot->psonAsserted);
    body += ",\"slot\":";
    body += String(slot->id);
    body += ",\"psonNPin\":";
    body += digitalRead(slot->psonPin);
  }
  body += ",\"automatic\":";
  body += jsonBool(powerPolicy.automatic);
  body += ",\"powerPolicy\":";
  body += powerPolicyJson();
  body += ",\"atx\":";
  body += atxJson();
  body += "}}";
  sendJson(200, body);
}

static void handleApiI2cEnable() {
  const String state = apiServer.arg("state");
  if (apiServer.method() == HTTP_GET || state.length() == 0 || tokenEquals(state.c_str(), "status")) {
    String body = "{\"ok\":true,\"i2cEnable\":";
    body += i2cEnableJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  if (tokenEquals(state.c_str(), "toggle") || tokenEquals(state.c_str(), "switch")) {
    setI2cEnable(!i2cEnableIsHigh());
  } else if (tokenEquals(state.c_str(), "on") || tokenEquals(state.c_str(), "enable") ||
             tokenEquals(state.c_str(), "high") || state == "1") {
    setI2cEnable(true);
  } else if (tokenEquals(state.c_str(), "off") || tokenEquals(state.c_str(), "disable") ||
             tokenEquals(state.c_str(), "low") || state == "0") {
    setI2cEnable(false);
  } else {
    sendApiError(400, "bad-request", "state must be status, toggle, on, or off");
    return;
  }

  String body = "{\"ok\":true,\"i2cEnable\":";
  body += i2cEnableJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiCadence() {
  const String mode = apiServer.arg("mode");
  if (mode.length() == 0 || tokenEquals(mode.c_str(), "status")) {
    String body = "{\"ok\":true,\"mode\":";
    body += jsonString(psu.respectSequenceSpacing() ? "normal" : "fast");
    body += ",\"minMs\":";
    body += psu.respectSequenceSpacing() ? "80" : "0";
    body += "}";
    sendJson(200, body);
    return;
  }

  if (tokenEquals(mode.c_str(), "normal") || tokenEquals(mode.c_str(), "safe") ||
      tokenEquals(mode.c_str(), "80ms")) {
    setAllPsuCadence(true);
  } else if (tokenEquals(mode.c_str(), "fast") || tokenEquals(mode.c_str(), "unsafe") ||
             tokenEquals(mode.c_str(), "0ms")) {
    setAllPsuCadence(false);
  } else {
    sendApiError(400, "bad-request", "mode must be normal or fast");
    return;
  }

  String body = "{\"ok\":true,\"mode\":";
  body += jsonString(psu.respectSequenceSpacing() ? "normal" : "fast");
  body += ",\"minMs\":";
  body += psu.respectSequenceSpacing() ? "80" : "0";
  body += "}";
  sendJson(200, body);
}

static void handleApiClearInterrupt() {
  PsuSlot *slot = nullptr;
  if (!parseApiSlotArg(slot)) {
    return;
  }

  if (slot == nullptr) {
    uint8_t okCount = 0;
    for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
      PsuSlot &candidate = psuSlots[index];
      if (ensurePsuSlotDetected(candidate) && candidate.device->clearInterrupt() == LiteonResult::Ok) {
        ++okCount;
      }
    }
    String body = "{\"ok\":true,\"result\":\"ok\",\"slot\":\"all\",\"okCount\":";
    body += String(okCount);
    body += "}";
    sendJson(200, body);
    return;
  }

  if (!ensurePsuSlotDetected(*slot)) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }
  const LiteonResult result = slot->device->clearInterrupt();
  if (result != LiteonResult::Ok) {
    sendApiError(502, resultName(result), "clear interrupt failed");
    return;
  }
  String body = "{\"ok\":true,\"result\":\"ok\",\"slot\":";
  body += String(slot->id);
  body += "}";
  sendJson(200, body);
}

static void handleApiReset() {
  PsuSlot *slot = nullptr;
  if (!parseApiSlotArg(slot)) {
    return;
  }
  const String target = apiServer.arg("target");

  const bool resetPeak = tokenEquals(target.c_str(), "peak");
  const bool resetAverage = tokenEquals(target.c_str(), "average") || tokenEquals(target.c_str(), "avg");
  if (!resetPeak && !resetAverage) {
    sendApiError(400, "bad-request", "target must be peak or average");
    return;
  }

  if (slot == nullptr) {
    uint8_t okCount = 0;
    for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
      PsuSlot &candidate = psuSlots[index];
      if (!ensurePsuSlotDetected(candidate)) {
        continue;
      }
      const LiteonResult result = resetPeak
          ? candidate.device->resetPeakOutputCurrent()
          : candidate.device->resetAverageInputPower();
      if (result == LiteonResult::Ok) {
        ++okCount;
      }
    }
    String body = "{\"ok\":true,\"result\":\"ok\",\"slot\":\"all\",\"okCount\":";
    body += String(okCount);
    body += "}";
    sendJson(200, body);
    return;
  }

  if (!ensurePsuSlotDetected(*slot)) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }

  const LiteonResult result = resetPeak
      ? slot->device->resetPeakOutputCurrent()
      : slot->device->resetAverageInputPower();
  if (result != LiteonResult::Ok) {
    sendApiError(502, resultName(result), "reset failed");
    return;
  }
  String body = "{\"ok\":true,\"result\":\"ok\",\"slot\":";
  body += String(slot->id);
  body += "}";
  sendJson(200, body);
}

static void handleApiStatus() {
  PsuSlot *slot = nullptr;
  const bool slotProvided = apiServer.hasArg("slot");
  if (slotProvided && !parseApiSlotArg(slot)) {
    return;
  }
  if (slotProvided && slot == nullptr) {
    sendApiError(400, "bad-request", "status requires a single slot");
    return;
  }

  if (slotProvided ? !ensurePsuSlotDetected(*slot) : !ensureLiteonAddressSelected()) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }

  LiteonStatus status;
  LiteonPsu &device = slotProvided ? *slot->device : psu;
  LiteonResult result = device.readStatus(status);
  if (result != LiteonResult::Ok) {
    sendApiError(502, resultName(result), "status read failed");
    return;
  }

  LiteonTelemetry telemetry;
  const LiteonResult telemetryResult = device.readTelemetry(telemetry);

  String body = "{\"ok\":true,\"address\":";
  body += jsonString(hex8(device.address7()));
  if (slotProvided) {
    body += ",\"slot\":";
    body += String(slot->id);
  }
  body += ",\"status\":{\"ucInfo\":";
  body += jsonString(hex16(status.ucInfo));
  body += ",\"psStatus\":";
  body += jsonString(hex16(status.psStatus));
  body += ",\"firmware\":";
  body += jsonString(String(status.majorFirmwareRevision()) + "." + String(status.minorFirmwareRevision()));
  body += ",\"ucType\":";
  body += String(status.ucType());
  body += ",\"vendor\":";
  body += String(status.vendorId());
  body += ",\"inputOk\":";
  body += jsonBool(status.inputOk());
  body += ",\"outputOk\":";
  body += jsonBool(status.outputDcOk());
  body += ",\"psuPsonAsserted\":";
  body += jsonBool(status.psonAsserted());
  body += ",\"range\":";
  body += jsonString(inputRangeName(status.inputRange()));
  body += ",\"shutdownEvent\":";
  body += jsonString(hex16(status.shutdownEvent));
  body += ",\"warningEvent\":";
  body += jsonString(hex16(status.warningEvent));
  body += ",\"psControl\":";
  body += jsonString(hex16(status.psControl));
  body += ",\"interruptLatched\":";
  body += jsonBool(status.interruptLatched());
  body += "},\"pins\":";
  body += pinsJson(slotProvided ? *slot : defaultPsuSlot());
  body += ",\"telemetryResult\":";
  body += jsonString(resultName(telemetryResult));
  if (telemetryResult == LiteonResult::Ok) {
    body += ",\"telemetry\":{\"inputVoltageRaw\":";
    body += jsonString(hex16(telemetry.inputVoltageRaw));
    body += ",\"inputVoltageVrms\":";
    body += String(telemetry.inputVoltageVrms, 3);
    body += ",\"inputCurrentRaw\":";
    body += jsonString(hex16(telemetry.inputCurrentRaw));
    body += ",\"inputCurrentA\":";
    body += String(telemetry.inputCurrentA, 3);
    body += ",\"inputPowerRaw\":";
    body += jsonString(hex16(telemetry.inputPowerRaw));
    body += ",\"inputPowerW\":";
    body += String(telemetry.inputPowerW, 3);
    body += ",\"outputVoltageRaw\":";
    body += jsonString(hex16(telemetry.outputVoltageRaw));
    body += ",\"outputVoltageV\":";
    body += String(telemetry.outputVoltageV, 3);
    body += ",\"outputCurrentRaw\":";
    body += jsonString(hex16(telemetry.outputCurrentRaw));
    body += ",\"outputCurrentA\":";
    body += String(telemetry.outputCurrentA, 3);
    body += ",\"outputPowerRaw\":";
    body += jsonString(hex16(telemetry.outputPowerRaw));
    body += ",\"outputPowerW\":";
    body += String(telemetry.outputPowerW, 3);
    body += ",\"inletTemperatureRaw\":";
    body += jsonString(hex16(telemetry.inletTemperatureRaw));
    body += ",\"inletTemperatureC\":";
    body += String(telemetry.inletTemperatureC, 3);
    body += ",\"internalTemperatureRaw\":";
    body += jsonString(hex16(telemetry.internalTemperatureRaw));
    body += ",\"internalTemperatureC\":";
    body += String(telemetry.internalTemperatureC, 3);
    body += ",\"fan1SpeedRaw\":";
    body += jsonString(hex16(telemetry.fan1SpeedRaw));
    body += ",\"fan1SpeedRpm\":";
    body += String(telemetry.fan1SpeedRpm);
    body += "}";
  }
  body += "}";
  sendJson(200, body);
}

static void stopMonitor() {
  monitor.active = false;
  monitor.cycleRunning = false;
}

static bool startMonitor(PsuSlot *selectedSlot, bool selectedAllSlots, uint32_t intervalMs, const uint8_t *registers, uint8_t registerCount) {
  if (selectedSlot == nullptr) {
    selectedSlot = &defaultPsuSlot();
  }

  if (registerCount == 0) {
    configureDefaultMonitorRegisters();
  } else {
    monitor.registerCount = registerCount;
    for (uint8_t index = 0; index < registerCount; ++index) {
      monitor.registers[index] = registers[index];
    }
  }

  monitor.intervalMs = clampMonitorInterval(intervalMs);
  monitor.registerIndex = 0;
  monitor.cycleRunning = false;
  monitor.allSlots = selectedAllSlots;
  monitor.slotIndex = selectedAllSlots ? 0 : psuSlotIndex(*selectedSlot);
  monitor.currentSlotIndex = monitor.slotIndex;
  monitor.nextCycleStartMs = millis();

  if (!selectedAllSlots && !ensurePsuSlotDetected(*selectedSlot)) {
    monitor.active = false;
    return false;
  }

  monitor.active = true;
  return true;
}

static void handleApiMonitor() {
  const String state = apiServer.arg("state");
  if (apiServer.method() == HTTP_GET || state.length() == 0 || tokenEquals(state.c_str(), "status")) {
    String body = "{\"ok\":true,\"monitor\":";
    body += monitorJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  if (tokenEquals(state.c_str(), "stop")) {
    stopMonitor();
    streamMonitorEvent("monitor", String("{\"type\":\"config\",\"monitor\":") + monitorJson() + "}");
    String body = "{\"ok\":true,\"monitor\":";
    body += monitorJson();
    body += "}";
    sendJson(200, body);
    return;
  }

  if (!tokenEquals(state.c_str(), "start")) {
    sendApiError(400, "bad-request", "state must be status, start, or stop");
    return;
  }

  PsuSlot *slot = nullptr;
  if (!parseApiSlotArg(slot)) {
    return;
  }
  const bool allSlots = slot == nullptr;
  PsuSlot *selectedSlot = allSlots ? &defaultPsuSlot() : slot;

  uint32_t intervalMs = kMonitorDefaultIntervalMs;
  const String intervalRaw = apiServer.hasArg("intervalMs") ? apiServer.arg("intervalMs") : apiServer.arg("interval");
  if (intervalRaw.length() > 0 && !parseUnsignedToken(intervalRaw.c_str(), kMonitorMaxIntervalMs, intervalMs)) {
    sendApiError(400, "bad-request", "intervalMs must be a positive millisecond value");
    return;
  }

  uint8_t registers[kMonitorMaxRegisters] = {};
  uint8_t registerCount = 0;
  if (!parseApiRegisterList(registers, kMonitorMaxRegisters, registerCount)) {
    sendApiError(400, "bad-request", "regs must contain one or more even registers");
    return;
  }

  if (!startMonitor(selectedSlot, allSlots, intervalMs, registers, registerCount)) {
    sendApiError(502, "i2c-nack", "no LiteOn monitor address responded");
    return;
  }

  streamMonitorEvent("monitor", String("{\"type\":\"config\",\"monitor\":") + monitorJson() + "}");
  String body = "{\"ok\":true,\"monitor\":";
  body += monitorJson();
  body += "}";
  sendJson(200, body);
}

static void handleApiNotFound() {
  // Preflight requests must not need a token: browsers send them without one.
  if (apiServer.method() == HTTP_OPTIONS) {
    sendCorsHeaders();
    apiServer.send(204, "text/plain", "");
    return;
  }
  const String requestPath = apiServer.uri();
  if (!requestPath.startsWith("/api/") && serveFrontendPath(requestPath)) {
    return;
  }
  if (!requestPath.startsWith("/api/")) {
    apiServer.send(404, "text/plain", "not found");
    return;
  }
  sendApiError(404, "not-found", "unknown API endpoint");
}

static void configureApi() {
  apiServer.collectHeaders(kApiHeaderKeys, sizeof(kApiHeaderKeys) / sizeof(kApiHeaderKeys[0]));
  apiServer.on("/", HTTP_GET, []() { (void)serveFrontendPath("/"); });

  // Authentication is always reachable; setting or clearing the password is a
  // session action, which is also how an open device accepts its first one.
  apiServer.on("/api/v1/auth", HTTP_GET, handleApiAuthStatus);
  apiServer.on("/api/v1/auth/challenge", HTTP_GET, handleApiAuthChallenge);
  apiServer.on("/api/v1/auth/login", HTTP_POST, handleApiAuthLogin);
  apiServer.on("/api/v1/auth/logout", HTTP_POST, handleApiAuthLogout);
  apiServer.on("/api/v1/auth/password", HTTP_POST, guarded(ApiAccess::Session, handleApiAuthSetPassword));
  apiServer.on("/api/v1/auth/password/clear", HTTP_POST, guarded(ApiAccess::Session, handleApiAuthClearPassword));
  apiServer.on("/api/v1/auth/telemetry", HTTP_POST, guarded(ApiAccess::Session, handleApiAuthTelemetryPublic));

  // Read-only reporting: hardware state and measurements.
  apiServer.on("/api/v1/info", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiInfo));
  apiServer.on("/api/v1/psus", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiPsus));
  apiServer.on("/api/v1/pins", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiPins));
  apiServer.on("/api/v1/status", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiStatus));
  apiServer.on("/api/v1/read", HTTP_GET, guarded(ApiAccess::Read, handleApiRead));
  apiServer.on("/api/v1/atx", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiAtx));
  apiServer.on("/api/v1/power-policy", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiPowerPolicy));
  apiServer.on("/api/v1/address", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiAddress));
  apiServer.on("/api/v1/i2c-enable", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiI2cEnable));
  apiServer.on("/api/v1/cadence", HTTP_GET, guarded(ApiAccess::Telemetry, handleApiCadence));
  // The register monitor only reads the PSUs, so a telemetry viewer may drive
  // it; it is how live data reaches a password-less dashboard.
  apiServer.on("/api/v1/monitor", HTTP_GET, guarded(ApiAccess::Read, handleApiMonitor));
  apiServer.on("/api/v1/monitor", HTTP_POST, guarded(ApiAccess::Read, handleApiMonitor));

  // Everything else changes device state and needs a session.
  apiServer.on("/api/v1/wifi", HTTP_GET, guarded(ApiAccess::Read, handleApiWifiStatus));
  apiServer.on("/api/v1/wifi/connect", HTTP_POST, guarded(ApiAccess::Session, handleApiWifiConnect));
  apiServer.on("/api/v1/wifi/reconnect", HTTP_POST, guarded(ApiAccess::Session, handleApiWifiReconnect));
  apiServer.on("/api/v1/wifi/disconnect", HTTP_POST, guarded(ApiAccess::Session, handleApiWifiDisconnect));
  apiServer.on("/api/v1/wifi/forget", HTTP_POST, guarded(ApiAccess::Session, handleApiWifiForget));
  apiServer.on("/api/v1/ota", HTTP_GET, guarded(ApiAccess::Session, handleApiOtaStatus));
  // The upload handler runs once per 1.4 KB body chunk, so it cannot be
  // wrapped: it checks access itself at UPLOAD_FILE_START, before Update.begin
  // touches flash. The completion handler runs once and is gated normally.
  apiServer.on("/api/v1/ota", HTTP_POST, guarded(ApiAccess::Session, handleApiOtaComplete),
               handleApiOtaUpload);
  apiServer.on("/api/v1/influx", HTTP_GET, guarded(ApiAccess::Read, handleApiInflux));
  apiServer.on("/api/v1/influx", HTTP_POST, guarded(ApiAccess::Session, handleApiInflux));
  apiServer.on("/api/v1/power-policy", HTTP_POST, guarded(ApiAccess::Session, handleApiPowerPolicy));
  apiServer.on("/api/v1/atx", HTTP_POST, guarded(ApiAccess::Session, handleApiAtx));
  apiServer.on("/api/v1/probe", HTTP_POST, guarded(ApiAccess::Session, handleApiProbe));
  apiServer.on("/api/v1/address", HTTP_POST, guarded(ApiAccess::Session, handleApiAddress));
  apiServer.on("/api/v1/write", HTTP_POST, guarded(ApiAccess::Session, handleApiWrite));
  apiServer.on("/api/v1/pson", HTTP_GET, guarded(ApiAccess::Read, handleApiPson));
  apiServer.on("/api/v1/pson", HTTP_POST, guarded(ApiAccess::Session, handleApiPson));
  apiServer.on("/api/v1/i2c-enable", HTTP_POST, guarded(ApiAccess::Session, handleApiI2cEnable));
  apiServer.on("/api/v1/cadence", HTTP_POST, guarded(ApiAccess::Session, handleApiCadence));
  apiServer.on("/api/v1/clear-interrupt", HTTP_POST, guarded(ApiAccess::Session, handleApiClearInterrupt));
  apiServer.on("/api/v1/reset", HTTP_POST, guarded(ApiAccess::Session, handleApiReset));

  apiServer.onNotFound(handleApiNotFound);
  apiServer.begin();
  monitorStreamServer.begin();
}

static bool monitorSlotIncluded(uint8_t index) {
  if (index >= kPsuSlotCount) {
    return false;
  }
  if (!monitor.allSlots) {
    return index == monitor.slotIndex;
  }

  const PsuSlot &slot = psuSlots[index];
  return slotIsPresent(slot) || slot.detected;
}

static bool findNextMonitorSlot(uint8_t startIndex, uint8_t &slotIndex) {
  for (uint8_t index = startIndex; index < kPsuSlotCount; ++index) {
    if (monitorSlotIncluded(index)) {
      slotIndex = index;
      return true;
    }
  }
  return false;
}

static void emitMonitorPinsForIncludedSlots() {
  for (uint8_t index = 0; index < kPsuSlotCount; ++index) {
    if (!monitorSlotIncluded(index)) {
      continue;
    }
    streamMonitorPins(monitor.cycle, psuSlots[index]);
  }
}

static void finishMonitorCycle() {
  monitor.cycleRunning = false;
  monitor.nextCycleStartMs = monitor.cycleStartMs + monitor.intervalMs;
  streamMonitorCycle(monitor.cycle, "end");
}

static bool beginMonitorCycle(uint32_t now) {
  if (!timeReached(now, monitor.nextCycleStartMs)) {
    return false;
  }

  monitor.cycleRunning = true;
  monitor.registerIndex = 0;
  monitor.cycle += 1;
  monitor.cycleStartMs = millis();
  streamMonitorCycle(monitor.cycle, "start");
  emitMonitorPinsForIncludedSlots();

  if (!findNextMonitorSlot(monitor.allSlots ? 0 : monitor.slotIndex, monitor.currentSlotIndex)) {
    finishMonitorCycle();
    return false;
  }
  return true;
}

static void advanceMonitorPosition() {
  if (monitor.allSlots) {
    uint8_t nextSlot = 0;
    if (findNextMonitorSlot(static_cast<uint8_t>(monitor.currentSlotIndex + 1), nextSlot)) {
      monitor.currentSlotIndex = nextSlot;
      return;
    }
  }

  monitor.registerIndex += 1;
  if (monitor.registerIndex >= monitor.registerCount) {
    return;
  }

  if (!findNextMonitorSlot(monitor.allSlots ? 0 : monitor.slotIndex, monitor.currentSlotIndex)) {
    monitor.registerIndex = monitor.registerCount;
  }
}

static void serviceMonitor() {
  if (!monitor.active || monitor.registerCount == 0 || otaUploadRunning || otaRestartPending) {
    return;
  }

  const uint32_t now = millis();
  if (!monitor.cycleRunning && !beginMonitorCycle(now)) {
    return;
  }

  if (monitor.registerIndex >= monitor.registerCount) {
    finishMonitorCycle();
    return;
  }

  PsuSlot &slot = psuSlots[monitor.currentSlotIndex];
  if (!ensurePsuSlotDetected(slot)) {
    streamMonitorError(monitor.cycle, slot, "i2c-nack");
    advanceMonitorPosition();
    return;
  }

  const uint8_t reg = monitor.registers[monitor.registerIndex];
  uint16_t value = 0;
  const LiteonResult result = slot.device->readRaw(static_cast<LiteonRegister>(reg), value);
  if (result == LiteonResult::I2cNack) {
    slot.detected = false;
    if (slot.id == 1) {
      liteonAddressDetected = false;
    }
  }
  streamMonitorRegister(monitor.cycle, slot, reg, result, value);
  advanceMonitorPosition();
}


void configureWebServer() {
  configureApi();
  configureDefaultMonitorRegisters();
}

void serviceWebServer() {
  apiServer.handleClient();
  serviceMonitorStreamServer();
  serviceOtaRestart();
  serviceMonitor();
}
