#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <stdint.h>

// Core WiFi Lifecycle
void configureWifi();
void serviceWifi();
void serviceWifiStatusLed();

// Status and Diagnostics
bool wifiIsConnected();
bool wifiCredentialsRemembered();
String wifiIpOrDash();
const char *getWifiHostname();
bool isMdnsStarted();
const char *wifiStatusName(wl_status_t status);
String wifiJson();

// Connect / Reconnect / Disconnect
void startWifiConnect(const char *ssid, const char *password);
bool waitForWifiConnect(uint32_t timeoutMs);
void connectRememberedWifi();
bool reconnectWifi(uint32_t timeoutMs);
void rememberWifiCredentials(const char *ssid, const char *password);
void disconnectWifi(bool eraseCredentials = false);
void forgetWifi();
bool getRememberedCredentials(String &ssid, String &password);

// Time Synchronization
void startNtpIfConnected();
bool currentEpochMs(uint64_t &epochMs);
