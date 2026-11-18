#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "config.h"
#include "types.h"

extern WebServer apiServer;
extern WiFiServer monitorStreamServer;
extern WiFiClient monitorStreamClient;

extern bool otaUploadRunning;
extern bool otaRestartPending;
extern MonitorState monitor;

// Subsystem Initialization & Servicing
void configureWebServer();
void serviceWebServer();
