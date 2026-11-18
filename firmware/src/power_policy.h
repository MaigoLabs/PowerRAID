#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include "config.h"
#include "types.h"
#include "efficiency_profiles.h"
#include "psu_manager.h"

extern PowerPolicyState powerPolicy;
extern const EfficiencyProfile *efficiencyProfile;

// Lifecycle & Configuration
void configurePowerPolicy();
void loadPowerPolicyPreferences();
void servicePowerPolicy(bool force = false);
void servicePowerPolicyLeds();

// Policy Mode & Parameters
bool setEfficiencyProfile(const char *id, bool persist = true);
const EfficiencyProfile *findEfficiencyProfile(const char *id);
void setPowerPolicyAutomatic(bool automatic, bool persist = true);
void setPowerPolicyManualPson(PsuSlot *slot, bool asserted, bool persist = true);
void setPowerPolicyNRedundancy(uint8_t nRedundancy, bool persist = true);

// Candidate / Status Evaluation
bool slotPowerPolicyCandidate(const PsuSlot &slot, uint32_t now);
bool slotPowerPolicyFailed(const PsuSlot &slot, uint32_t now);
const EfficiencyCurve &efficiencyCurveForActiveCount(uint8_t activeCount);

// Diagnostics & JSON
String powerPolicyJson();
String powerPolicySlotJson(const PsuSlot &slot, uint32_t now);
String powerPolicySlotsJson(uint32_t now);
String availableEfficiencyProfilesJson();
String efficiencyCurvesJson();
String efficiencyCurveJson(const EfficiencyCurve &curve);
