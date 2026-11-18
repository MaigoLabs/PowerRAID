#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "types.h"
#include "util.h"
#include "wifi_manager.h"
#include "psu_manager.h"
#include "power_policy.h"
#include "influx_client.h"
#include "serial_console.h"
#include "auth.h"
#include "web_server.h"

static uint32_t lastRunLedToggleMs = 0;
static bool runLedOn = false;

void serviceHeartbeat() {
  const uint32_t now = millis();
  if (now - lastRunLedToggleMs < kRunLedPeriodMs) {
    return;
  }
  lastRunLedToggleMs = now;
  runLedOn = !runLedOn;
  digitalWrite(PIN_RUN_LED, runLedOn ? kLedOn : kLedOff);
}

void setup() {
  configurePsuGpios();
  playBootLedAnimation();

  Wire.begin(PIN_PSU_SDA, PIN_PSU_SCL);
  Wire.setClock(kI2cClockHz);

  configurePowerPolicy();
  configureInflux();
  configureWifi();
  configureSerialConsole();
  auth::configureAuth();
  configureWebServer();
  connectRememberedWifi();
}

void loop() {
  serviceSerialConsole();
  serviceWebServer();
  serviceWifi();
  serviceHeartbeat();
  serviceWifiStatusLed();
  const bool prevAtx = atx.requestAsserted;
  if (serviceAtxPsOnRequest(millis()) != prevAtx) {
    servicePowerPolicy(true);
  }
  servicePowerPolicy();
  serviceImonMonitor();
  serviceInfluxUploadCompletions();
  serviceInfluxPsuSampler();
  serviceInfluxReporter();
  delay(20);
}
