#include "wifi_manager.h"
#include <ESPmDNS.h>
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include "util.h"

extern void serviceHeartbeat();

static Preferences wifiPrefs;
static char wifiHostname[32] = {};
static bool wifiConnectStarted = false;
static uint32_t wifiConnectStartedMs = 0;
static bool mdnsStarted = false;
static bool ntpConfigured = false;
static uint32_t lastWifiStatusLedToggleMs = 0;
static bool wifiStatusLedOn = false;

const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED:
      return "connected";
    case WL_NO_SSID_AVAIL:
      return "no-ssid";
    case WL_CONNECT_FAILED:
      return "connect-failed";
    case WL_CONNECTION_LOST:
      return "connection-lost";
    case WL_DISCONNECTED:
      return "disconnected";
    case WL_IDLE_STATUS:
      return "idle";
    default:
      return "unknown";
  }
}

bool wifiIsConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool wifiCredentialsRemembered() {
  if (!wifiPrefs.isKey("ssid")) {
    return false;
  }
  return wifiPrefs.getString("ssid", "").length() > 0;
}

String wifiIpOrDash() {
  return wifiIsConnected() ? WiFi.localIP().toString() : String("-");
}

const char *getWifiHostname() {
  return wifiHostname;
}

bool isMdnsStarted() {
  return mdnsStarted;
}

void rememberWifiCredentials(const char *ssid, const char *password) {
  wifiPrefs.putString("ssid", ssid);
  wifiPrefs.putString("pass", password == nullptr ? "" : password);
}

bool getRememberedCredentials(String &ssid, String &password) {
  if (!wifiPrefs.isKey("ssid")) {
    return false;
  }
  ssid = wifiPrefs.getString("ssid", "");
  password = wifiPrefs.isKey("pass") ? wifiPrefs.getString("pass", "") : String("");
  return ssid.length() > 0;
}

void startNtpIfConnected() {
  if (!wifiIsConnected() || ntpConfigured) {
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  ntpConfigured = true;
}

bool currentEpochMs(uint64_t &epochMs) {
  timeval tv = {};
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1609459200) {
    return false;
  }
  epochMs = static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
  return true;
}

static void startMdnsIfConnected() {
  if (!wifiIsConnected() || mdnsStarted) {
    return;
  }
  startNtpIfConnected();
  if (MDNS.begin(wifiHostname)) {
    MDNS.addService("http", "tcp", kApiPort);
    MDNS.addServiceTxt("http", "tcp", "ota", "/api/v1/ota");
    const String monitorTxt = String(":") + String(kMonitorStreamPort) + kMonitorStreamPath;
    MDNS.addServiceTxt("http", "tcp", "monitor", monitorTxt.c_str());
    mdnsStarted = true;
  }
}

void startWifiConnect(const char *ssid, const char *password) {
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  WiFi.disconnect(false, false);
  delay(50);
  WiFi.begin(ssid, password == nullptr ? "" : password);
  wifiConnectStarted = true;
  wifiConnectStartedMs = millis();
}

bool waitForWifiConnect(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
    serviceHeartbeat();
    serviceWifiStatusLed();
    delay(25);
  }
  wifiConnectStarted = WiFi.status() != WL_CONNECTED;
  startMdnsIfConnected();
  return WiFi.status() == WL_CONNECTED;
}

void connectRememberedWifi() {
  String ssid, pass;
  if (getRememberedCredentials(ssid, pass)) {
    startWifiConnect(ssid.c_str(), pass.c_str());
  }
}

bool reconnectWifi(uint32_t timeoutMs) {
  String ssid, pass;
  if (!getRememberedCredentials(ssid, pass)) {
    return false;
  }
  startWifiConnect(ssid.c_str(), pass.c_str());
  return waitForWifiConnect(timeoutMs);
}

void disconnectWifi(bool eraseCredentials) {
  WiFi.disconnect(false, eraseCredentials);
  wifiConnectStarted = false;
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  if (eraseCredentials) {
    wifiPrefs.clear();
  }
}

void forgetWifi() {
  disconnectWifi(true);
}

void serviceWifiStatusLed() {
  if (wifiIsConnected()) {
    if (wifiStatusLedOn) {
      wifiStatusLedOn = false;
      digitalWrite(PIN_WIFI_STATUS_LED, kLedOff);
    }
    lastWifiStatusLedToggleMs = millis();
    return;
  }

  const uint32_t now = millis();
  if (now - lastWifiStatusLedToggleMs < kWifiStatusLedPeriodMs) {
    return;
  }
  lastWifiStatusLedToggleMs = now;
  wifiStatusLedOn = !wifiStatusLedOn;
  digitalWrite(PIN_WIFI_STATUS_LED, wifiStatusLedOn ? kLedOn : kLedOff);
}

void serviceWifi() {
  if (!wifiConnectStarted) {
    if (!wifiIsConnected() && mdnsStarted) {
      MDNS.end();
      mdnsStarted = false;
    }
    startMdnsIfConnected();
    return;
  }
  if (WiFi.status() == WL_CONNECTED || millis() - wifiConnectStartedMs > kWifiConnectTimeoutMs) {
    wifiConnectStarted = false;
    startMdnsIfConnected();
  }
}

String wifiJson() {
  String body = "{";
  body += "\"status\":";
  body += jsonString(wifiStatusName(WiFi.status()));
  body += ",\"connected\":";
  body += jsonBool(wifiIsConnected());
  body += ",\"remembered\":";
  body += jsonBool(wifiCredentialsRemembered());
  body += ",\"hostname\":";
  body += jsonString(wifiHostname);
  body += ",\"mdns\":";
  body += jsonBool(mdnsStarted);
  body += ",\"ssid\":";
  body += jsonString(wifiIsConnected() ? WiFi.SSID() : String(""));
  body += ",\"ip\":";
  body += jsonString(wifiIpOrDash());
  body += ",\"rssi\":";
  body += wifiIsConnected() ? String(WiFi.RSSI()) : String(0);
  body += "}";
  return body;
}

void configureWifi() {
  wifiPrefs.begin("pramon_wifi", false);
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(wifiHostname, sizeof(wifiHostname), "powerraid-%04X", static_cast<uint16_t>(mac & 0xFFFF));
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(wifiHostname);
  WiFi.setAutoReconnect(true);
}
