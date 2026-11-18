#pragma once

#include <Arduino.h>
#include <stdint.h>
#include "config.h"
#include "types.h"
#include "liteon_psu.h"

extern InfluxState influx;

// Subsystem Initialization & Servicing
void configureInflux();
void configureInfluxUploadWorker();
void configureImonMonitor();
void serviceImonMonitor();
void serviceInfluxUploadCompletions();
void serviceInfluxPsuSampler();
void serviceInfluxReporter();

// Influx Configuration
void loadInfluxConfig();
void saveInfluxConfig();
bool influxConfigured();
String normalizedInfluxUrl(String url);
bool influxUrlUsesHttps();
bool urlUsesHttps(const String &url);
String influxWriteUrl(const String &url, const String &org, const String &bucket);
String influxWriteUrl();

// JSON & Diagnostic Helpers
String influxJson();
ImonSnapshot snapshotImonWindow(bool resetAfterSnapshot);
void enqueueInfluxImonPoint(const ImonSnapshot &imon, uint32_t sampleMs);
void clearInfluxImonQueue();
bool influxHasPendingTelemetry();
