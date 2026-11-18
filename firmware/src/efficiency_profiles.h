#pragma once

#include <stddef.h>
#include <stdint.h>

struct EfficiencyCurvePoint {
  float loadPercent;
  float efficiencyPercent;
};

struct EfficiencyCurve {
  const EfficiencyCurvePoint *points;
  uint8_t pointCount;
};

struct EfficiencyProfile {
  const char *id;
  const char *label;
  const char *source;
  EfficiencyCurve curves[3];
};

template <size_t N>
static constexpr uint8_t efficiencyPointCount(const EfficiencyCurvePoint (&)[N]) {
  return static_cast<uint8_t>(N);
}

// Compatible-family LiteOn PS-2751-7H-LF minimum efficiency at 115 VAC. This
// was the provisional policy curve through firmware 0.8.1 and remains
// available as a legacy fallback; it is not a PS-2122-3H characterization.
static const EfficiencyCurvePoint kDatasheet115VEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {10.0f, 85.0f},
    {20.0f, 89.0f},
    {50.0f, 92.0f},
    {100.0f, 89.0f},
};

// Measured at 120 VAC on 2026-07-17. Source:
// firmware/efficiency-results/efficiency_loss_20260717T185220Z.csv
//
// Load percentage is measured system output divided by 900 W per active PSU,
// matching the power policy's rated-capacity calculation. Efficiency is
// measured system output divided by total AC input from all three smart plugs,
// so each active-count curve includes the inactive supplies' standby draw.
// Repeated 1-PSU load steps are combined as sum(output) / sum(input).
static const EfficiencyCurvePoint kMeasured120V1PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {6.561403f, 81.451906f},
    {10.407407f, 85.932722f},
    {20.816931f, 89.215420f},
    {29.900000f, 89.662641f},
    {39.011696f, 90.724874f},
    {49.367150f, 90.859785f},
    {58.333333f, 90.206186f},
    {66.537037f, 90.358939f},
    {75.494949f, 89.874940f},
    {84.518518f, 89.322060f},
    {91.888889f, 88.785868f},
};

static const EfficiencyCurvePoint kMeasured120V2PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {6.045752f, 91.737432f},
    {10.736111f, 89.153119f},
    {19.788889f, 90.457111f},
    {29.555556f, 91.252144f},
    {39.322223f, 91.941074f},
    {47.978395f, 91.727149f},
    {57.711111f, 91.604938f},
    {66.339869f, 91.015065f},
    {75.062500f, 90.521573f},
    {84.709877f, 89.934652f},
    {94.169591f, 88.922561f},
};

static const EfficiencyCurvePoint kMeasured120V3PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {6.003898f, 93.163944f},
    {10.761317f, 92.829251f},
    {19.896297f, 91.205433f},
    {29.414815f, 91.629651f},
    {38.851852f, 91.695804f},
    {47.874074f, 92.159299f},
    {57.228395f, 91.755740f},
    {66.423611f, 91.083672f},
    {75.592593f, 90.711111f},
    {81.199294f, 90.311697f},
};

// Measured at the load terminals at 120 VAC on 2026-07-25. Source:
// firmware/efficiency-results/efficiency_loss_20260725T195731Z.csv
//
// Load output uses the STM32 terminal-voltage measurements: V * I for the
// Chroma and V^2 / R for each enabled resistor group, with R = 12 V / 24.9 A.
// Load percentage uses the simultaneous PMBus system-output reading divided
// by 900 W per active PSU because that is the load signal available to the
// firmware at runtime. Efficiency uses STM32-derived terminal output divided
// by total AC input from all three smart plugs, including standby draw from
// inactive supplies.
static const EfficiencyCurvePoint kMeasuredVoltage120V1PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {6.527777f, 69.164355f},
    {10.388889f, 79.428339f},
    {20.763889f, 87.342720f},
    {29.819444f, 88.960221f},
    {40.111111f, 90.414150f},
    {50.472223f, 90.889559f},
    {58.333333f, 90.354894f},
    {69.111111f, 91.599237f},
    {79.513889f, 90.593334f},
    {88.375000f, 89.847538f},
    {99.555556f, 88.914793f},
};

static const EfficiencyCurvePoint kMeasuredVoltage120V2PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {5.992424f, 73.338960f},
    {10.666667f, 83.055952f},
    {20.470899f, 89.293360f},
    {29.600000f, 90.337094f},
    {39.200000f, 92.127646f},
    {47.991228f, 93.178007f},
    {58.370370f, 92.782911f},
    {68.351852f, 92.806152f},
    {78.404762f, 92.295753f},
    {89.122223f, 91.021659f},
    {97.510417f, 91.023169f},
};

static const EfficiencyCurvePoint kMeasuredVoltage120V3PsuEfficiencyCurve[] = {
    {0.0f, 0.0f},
    {5.976852f, 77.578390f},
    {10.733333f, 85.821901f},
    {19.900673f, 89.736538f},
    {29.469136f, 92.097657f},
    {38.920988f, 93.516256f},
    {47.935802f, 94.569185f},
    {57.348148f, 94.462219f},
    {66.551657f, 94.124269f},
    {75.748148f, 93.643677f},
    {81.333333f, 92.876363f},
};

static const EfficiencyProfile kEfficiencyProfiles[] = {
    {
        "datasheet-115v",
        "Legacy compatible-family minimum at 115 V",
        "Compatible-family LiteOn PS-2751-7H-LF Table 8",
        {
            {kDatasheet115VEfficiencyCurve, efficiencyPointCount(kDatasheet115VEfficiencyCurve)},
            {kDatasheet115VEfficiencyCurve, efficiencyPointCount(kDatasheet115VEfficiencyCurve)},
            {kDatasheet115VEfficiencyCurve, efficiencyPointCount(kDatasheet115VEfficiencyCurve)},
        },
    },
    {
        "measured-120v-20260717",
        "Measured at 120 V on 2026-07-17",
        "efficiency-results/efficiency_loss_20260717T185220Z.csv",
        {
            {kMeasured120V1PsuEfficiencyCurve, efficiencyPointCount(kMeasured120V1PsuEfficiencyCurve)},
            {kMeasured120V2PsuEfficiencyCurve, efficiencyPointCount(kMeasured120V2PsuEfficiencyCurve)},
            {kMeasured120V3PsuEfficiencyCurve, efficiencyPointCount(kMeasured120V3PsuEfficiencyCurve)},
        },
    },
    {
        "measured-voltage-120v-20260725",
        "Measured load-terminal voltage at 120 V on 2026-07-25",
        "efficiency-results/efficiency_loss_20260725T195731Z.csv",
        {
            {kMeasuredVoltage120V1PsuEfficiencyCurve, efficiencyPointCount(kMeasuredVoltage120V1PsuEfficiencyCurve)},
            {kMeasuredVoltage120V2PsuEfficiencyCurve, efficiencyPointCount(kMeasuredVoltage120V2PsuEfficiencyCurve)},
            {kMeasuredVoltage120V3PsuEfficiencyCurve, efficiencyPointCount(kMeasuredVoltage120V3PsuEfficiencyCurve)},
        },
    },
};

static constexpr uint8_t kEfficiencyProfileCount =
    static_cast<uint8_t>(sizeof(kEfficiencyProfiles) / sizeof(kEfficiencyProfiles[0]));
static constexpr uint8_t kDefaultEfficiencyProfileIndex = 2;
static const char kDefaultEfficiencyProfileId[] = "measured-voltage-120v-20260725";
static const char kPreviousDefaultEfficiencyProfileId[] = "measured-120v-20260717";
