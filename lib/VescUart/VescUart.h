// ---------------------------------------------------------------------------
// High level VESC client over an Arduino Stream (HardwareSerial).
// Polls COMM_GET_VALUES and keeps the latest decoded telemetry.
// Arduino only; the pieces underneath (VescPacket / VescValues / VescMath)
// are host-testable.
// ---------------------------------------------------------------------------
#pragma once

#ifndef VESC_NATIVE_TEST

#include <Arduino.h>

#include "VescPacket.h"
#include "VescValues.h"

namespace vesc {

class VescUart {
public:
    explicit VescUart(Stream &serial) : serial_(serial) {}

    // Send COMM_GET_VALUES. The reply is picked up by update().
    void requestValues();

    // Pump the serial port. Returns true if a new Values block arrived.
    bool update();

    const Values &values() const { return values_; }

    // Milliseconds since the last valid Values packet (millis()-based).
    uint32_t msSinceLastValues() const { return millis() - lastValuesMs_; }
    bool everConnected() const { return lastValuesMs_ != 0; }

    uint32_t packetsOk() const { return packetsOk_; }
    uint32_t crcErrors() const { return decoder_.crcErrors(); }

private:
    Stream &serial_;
    PacketDecoder decoder_;
    Values values_;
    uint32_t lastValuesMs_ = 0;
    uint32_t packetsOk_ = 0;
};

}  // namespace vesc

#endif  // VESC_NATIVE_TEST
