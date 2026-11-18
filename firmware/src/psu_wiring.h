#pragma once

#include <Arduino.h>

// PowerRAID V1 ESP32-S3-WROOM-1-N8R8 wiring from V1/PowerRAID.kicad_pcb.
// PSU connector pin numbers follow Docs/PSU_Pinout.md.

static constexpr uint8_t PIN_PSU1_PRESENT_N = 9;     // PSU1 pin 36 PRESENT#
static constexpr uint8_t PIN_PSU1_PSOK = 10;         // PSU1 pin 35 PSOK
static constexpr uint8_t PIN_PSU1_PSON_N = 11;       // PSU1 pin 33 PSON#

static constexpr uint8_t PIN_PSU2_PRESENT_N = 12;    // PSU2 pin 36 PRESENT#
static constexpr uint8_t PIN_PSU2_PSOK = 13;         // PSU2 pin 35 PSOK
static constexpr uint8_t PIN_PSU2_PSON_N = 39;       // PSU2 pin 33 PSON#

static constexpr uint8_t PIN_PSU3_PRESENT_N = 41;    // PSU3 pin 36 PRESENT#
static constexpr uint8_t PIN_PSU3_PSOK = 42;         // PSU3 pin 35 PSOK
static constexpr uint8_t PIN_PSU3_PSON_N = 38;       // PSU3 pin 33 PSON#

static constexpr uint8_t PIN_PSU_INTERRUPT_N = 40;   // Shared PSU pin 38 PS_INTERRUPT
static constexpr uint8_t PIN_PSU_IMON_ADC = 8;       // Shared PSU pin 34 I-MON through R13/R12 divider
static constexpr uint8_t PIN_I2C_ENABLE = 21;        // TXS0102 OE, active high
static constexpr uint8_t PIN_PSU_SDA = 47;           // PSU_SDA_3.3 through TXS0102 A1
static constexpr uint8_t PIN_PSU_SCL = 48;           // PSU_SCL_3.3 through TXS0102 A2

static constexpr uint8_t PIN_LED_1 = 4;              // LED1 yellow, active high
static constexpr uint8_t PIN_LED_2 = 5;              // LED2 yellow, active high
static constexpr uint8_t PIN_LED_3 = 6;              // LED3 yellow, active high
static constexpr uint8_t PIN_LED_FAULT = 7;          // LED_FAULT1 red, active high

// ATX interface (V1 net ATX_ON = J1 pin 16 PS_ON#).
// ATX_ON is open-drain: R612 10k pulls it to +3.3VS, the motherboard pulls it low.
static constexpr uint8_t PIN_ATX_PS_ON_N = 14;       // net ATX_ON, J1 pin 16, open-drain request

// ATX main-rail converter enables. U4 generates +5VP, U5 generates +3.3VP.
// The NAE12S20-C EN pin has an internal 10k pull-up to Vin and an external
// divider to GND, so leaving the GPIO high-Z leaves the converter enabled.
// Drive low (open-drain) to hold a main rail off.
static constexpr uint8_t PIN_ATX_5V_EN_N = 17;       // net U4_EN, U4 pin 13
static constexpr uint8_t PIN_ATX_3V3_EN_N = 15;      // net U5_EN, U5 pin 13

// ATX main-rail power-good feedback from the converter PG pins.
static constexpr uint8_t PIN_ATX_5V_PG = 18;         // net U4_PG, U4 pin 14
static constexpr uint8_t PIN_ATX_3V3_PG = 16;        // net U5_PG, U5 pin 14

static constexpr uint8_t PIN_RUN_LED = PIN_LED_1;
static constexpr uint8_t PIN_WIFI_STATUS_LED = PIN_LED_3;
