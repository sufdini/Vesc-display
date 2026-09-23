#include "VescMath.h"

namespace vesc {

namespace {
constexpr float kPi = 3.14159265358979f;

// Open-circuit voltage vs state of charge for a generic Li-ion / Li-Po cell.
// Points must be sorted by voltage ascending.
struct CurvePoint {
    float volts;
    float percent;
};
const CurvePoint kLiIonCurve[] = {
    {3.30f, 0.0f},  {3.40f, 3.0f},  {3.50f, 8.0f},   {3.60f, 15.0f}, {3.70f, 30.0f},
    {3.80f, 48.0f}, {3.90f, 65.0f}, {4.00f, 80.0f},  {4.10f, 92.0f}, {4.20f, 100.0f},
};
constexpr int kCurvePoints = sizeof(kLiIonCurve) / sizeof(kLiIonCurve[0]);
}  // namespace

float wheelCircumferenceM(const VehicleConfig &cfg) {
    return cfg.wheelDiameterMm * kPi / 1000.0f;
}

float erpmToKmh(float erpm, const VehicleConfig &cfg) {
    if (cfg.motorPoles <= 0) {
        return 0.0f;
    }
    const float polePairs = static_cast<float>(cfg.motorPoles) / 2.0f;
    const float motorRpm = erpm / polePairs;
    const float wheelRpm = motorRpm * cfg.gearRatio;
    const float metersPerMinute = wheelRpm * wheelCircumferenceM(cfg);
    return metersPerMinute * 60.0f / 1000.0f;
}

float tachoToKm(int32_t tacho, const VehicleConfig &cfg) {
    if (cfg.motorPoles <= 0) {
        return 0.0f;
    }
    // The tachometer counts commutation steps: 6 per electrical revolution,
    // so 3 * poles per mechanical motor revolution.
    const float motorRevs = static_cast<float>(tacho) / (3.0f * static_cast<float>(cfg.motorPoles));
    const float wheelRevs = motorRevs * cfg.gearRatio;
    return wheelRevs * wheelCircumferenceM(cfg) / 1000.0f;
}

float cellVoltage(float packVoltage, const VehicleConfig &cfg) {
    if (cfg.batteryCells <= 0) {
        return 0.0f;
    }
    return packVoltage / static_cast<float>(cfg.batteryCells);
}

float batteryPercent(float packVoltage, const VehicleConfig &cfg) {
    const float v = cellVoltage(packVoltage, cfg);
    if (v <= kLiIonCurve[0].volts) {
        return 0.0f;
    }
    if (v >= kLiIonCurve[kCurvePoints - 1].volts) {
        return 100.0f;
    }
    for (int i = 1; i < kCurvePoints; i++) {
        if (v <= kLiIonCurve[i].volts) {
            const CurvePoint &a = kLiIonCurve[i - 1];
            const CurvePoint &b = kLiIonCurve[i];
            const float t = (v - a.volts) / (b.volts - a.volts);
            return a.percent + t * (b.percent - a.percent);
        }
    }
    return 100.0f;
}

float whPerKm(float wattHoursNet, float km) {
    if (km <= 0.001f) {
        return 0.0f;
    }
    return wattHoursNet / km;
}

}  // namespace vesc
