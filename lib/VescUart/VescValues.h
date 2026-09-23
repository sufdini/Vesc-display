// ---------------------------------------------------------------------------
// COMM_GET_VALUES decoding
//
// Decodes the telemetry block that the VESC returns in response to
// COMM_GET_VALUES (firmware 3.x through 6.x). All multi-byte fields are
// big-endian fixed-point numbers; see buffer.c in the VESC firmware.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vesc {

// mc_fault_code from the VESC firmware (datatypes.h).
enum FaultCode : uint8_t {
    FAULT_NONE = 0,
    FAULT_OVER_VOLTAGE,
    FAULT_UNDER_VOLTAGE,
    FAULT_DRV,
    FAULT_ABS_OVER_CURRENT,
    FAULT_OVER_TEMP_FET,
    FAULT_OVER_TEMP_MOTOR,
    FAULT_GATE_DRIVER_OVER_VOLTAGE,
    FAULT_GATE_DRIVER_UNDER_VOLTAGE,
    FAULT_MCU_UNDER_VOLTAGE,
    FAULT_BOOTING_FROM_WATCHDOG_RESET,
    FAULT_ENCODER_SPI,
    FAULT_ENCODER_SINCOS_BELOW_MIN_AMPLITUDE,
    FAULT_ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE,
    FAULT_FLASH_CORRUPTION,
    FAULT_HIGH_OFFSET_CURRENT_SENSOR_1,
    FAULT_HIGH_OFFSET_CURRENT_SENSOR_2,
    FAULT_HIGH_OFFSET_CURRENT_SENSOR_3,
    FAULT_UNBALANCED_CURRENTS,
    FAULT_BRK,
    FAULT_RESOLVER_LOT,
    FAULT_RESOLVER_DOS,
    FAULT_RESOLVER_LOS,
    FAULT_FLASH_CORRUPTION_APP_CFG,
    FAULT_FLASH_CORRUPTION_MC_CFG,
    FAULT_ENCODER_NO_MAGNET,
    FAULT_ENCODER_MAGNET_TOO_STRONG,
    FAULT_PHASE_FILTER,
    FAULT_ENCODER_FAULT,
    FAULT_LV_OUTPUT_FAULT,
};

// Short human readable name of a fault code, e.g. "OVER VOLTAGE".
const char *faultName(uint8_t code);

struct Values {
    float tempFet = 0;          // deg C
    float tempMotor = 0;        // deg C
    float currentMotor = 0;     // A
    float currentInput = 0;     // A (battery current, negative = regen)
    float currentId = 0;        // A
    float currentIq = 0;        // A
    float dutyCycle = 0;        // -1.0 .. 1.0
    float erpm = 0;             // electrical RPM
    float voltageInput = 0;     // V
    float ampHours = 0;         // Ah drawn
    float ampHoursCharged = 0;  // Ah regenerated
    float wattHours = 0;        // Wh drawn
    float wattHoursCharged = 0; // Wh regenerated
    int32_t tachometer = 0;     // commutation steps, signed
    int32_t tachometerAbs = 0;  // commutation steps, absolute
    uint8_t fault = FAULT_NONE;
    float pidPos = 0;           // degrees
    uint8_t controllerId = 0;
    // Present on newer firmware only (fields default to 0 if absent).
    float tempMos1 = 0;
    float tempMos2 = 0;
    float tempMos3 = 0;
    float vd = 0;
    float vq = 0;
    uint8_t status = 0;

    // Derived at decode time
    float powerInput() const { return voltageInput * currentInput; }  // W
};

// Length of the mandatory part of the COMM_GET_VALUES payload, including the
// command id byte.
constexpr size_t kGetValuesMinLength = 1 + 2 + 2 + 4 + 4 + 4 + 4 + 2 + 4 + 2 + 4 + 4 + 4 + 4 + 4 + 4 + 1;  // 54

// Decode a COMM_GET_VALUES payload (payload[0] == COMM_GET_VALUES).
// Returns false if the payload is not a valid COMM_GET_VALUES response.
bool decodeGetValues(const uint8_t *payload, size_t len, Values &out);

// Big-endian fixed-point readers shared with other decoders.
int16_t readInt16(const uint8_t *buf, size_t &idx);
int32_t readInt32(const uint8_t *buf, size_t &idx);
float readFloat16(const uint8_t *buf, size_t &idx, float scale);
float readFloat32(const uint8_t *buf, size_t &idx, float scale);

}  // namespace vesc
