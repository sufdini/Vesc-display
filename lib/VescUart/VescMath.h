// ---------------------------------------------------------------------------
// Conversions from raw VESC telemetry to rider-facing numbers.
// Pure functions, no state, no Arduino dependency.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

namespace vesc {

struct VehicleConfig {
    int motorPoles = 14;           // magnet poles (not pole pairs)
    float wheelDiameterMm = 90.0f; // driven wheel outer diameter
    float gearRatio = 1.0f;        // wheel revolutions per motor revolution
    int batteryCells = 10;         // series cell count
};

constexpr float kKmPerMile = 1.609344f;

// Wheel circumference in metres.
float wheelCircumferenceM(const VehicleConfig &cfg);

// Vehicle speed in km/h from electrical RPM. Sign follows direction.
float erpmToKmh(float erpm, const VehicleConfig &cfg);

// Distance in km from a tachometer count (commutation steps).
float tachoToKm(int32_t tacho, const VehicleConfig &cfg);

// Battery state of charge 0..100 from pack voltage, using a Li-ion
// discharge curve per cell.
float batteryPercent(float packVoltage, const VehicleConfig &cfg);

// Per-cell voltage.
float cellVoltage(float packVoltage, const VehicleConfig &cfg);

// Energy consumption in Wh/km for the given trip, or 0 if no distance yet.
float whPerKm(float wattHoursNet, float km);

inline float kmhToMph(float kmh) { return kmh / kKmPerMile; }
inline float kmToMiles(float km) { return km / kKmPerMile; }

}  // namespace vesc
