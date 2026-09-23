#include "VescValues.h"

#include "VescPacket.h"

namespace vesc {

int16_t readInt16(const uint8_t *buf, size_t &idx) {
    const int16_t v = static_cast<int16_t>((static_cast<uint16_t>(buf[idx]) << 8) | buf[idx + 1]);
    idx += 2;
    return v;
}

int32_t readInt32(const uint8_t *buf, size_t &idx) {
    const uint32_t v = (static_cast<uint32_t>(buf[idx]) << 24) | (static_cast<uint32_t>(buf[idx + 1]) << 16) |
                       (static_cast<uint32_t>(buf[idx + 2]) << 8) | static_cast<uint32_t>(buf[idx + 3]);
    idx += 4;
    return static_cast<int32_t>(v);
}

float readFloat16(const uint8_t *buf, size_t &idx, float scale) {
    return static_cast<float>(readInt16(buf, idx)) / scale;
}

float readFloat32(const uint8_t *buf, size_t &idx, float scale) {
    return static_cast<float>(readInt32(buf, idx)) / scale;
}

bool decodeGetValues(const uint8_t *payload, size_t len, Values &out) {
    if (len < kGetValuesMinLength || payload[0] != COMM_GET_VALUES) {
        return false;
    }

    size_t i = 1;
    out.tempFet = readFloat16(payload, i, 10.0f);
    out.tempMotor = readFloat16(payload, i, 10.0f);
    out.currentMotor = readFloat32(payload, i, 100.0f);
    out.currentInput = readFloat32(payload, i, 100.0f);
    out.currentId = readFloat32(payload, i, 100.0f);
    out.currentIq = readFloat32(payload, i, 100.0f);
    out.dutyCycle = readFloat16(payload, i, 1000.0f);
    out.erpm = readFloat32(payload, i, 1.0f);
    out.voltageInput = readFloat16(payload, i, 10.0f);
    out.ampHours = readFloat32(payload, i, 10000.0f);
    out.ampHoursCharged = readFloat32(payload, i, 10000.0f);
    out.wattHours = readFloat32(payload, i, 10000.0f);
    out.wattHoursCharged = readFloat32(payload, i, 10000.0f);
    out.tachometer = readInt32(payload, i);
    out.tachometerAbs = readInt32(payload, i);
    out.fault = payload[i++];

    // Everything below was added in later firmware versions; decode only
    // what is present so older VESCs still work.
    if (len >= i + 4) {
        out.pidPos = readFloat32(payload, i, 1000000.0f);
    }
    if (len >= i + 1) {
        out.controllerId = payload[i++];
    }
    if (len >= i + 6) {
        out.tempMos1 = readFloat16(payload, i, 10.0f);
        out.tempMos2 = readFloat16(payload, i, 10.0f);
        out.tempMos3 = readFloat16(payload, i, 10.0f);
    }
    if (len >= i + 8) {
        out.vd = readFloat32(payload, i, 1000.0f);
        out.vq = readFloat32(payload, i, 1000.0f);
    }
    if (len >= i + 1) {
        out.status = payload[i++];
    }
    return true;
}

const char *faultName(uint8_t code) {
    switch (code) {
        case FAULT_NONE: return "NONE";
        case FAULT_OVER_VOLTAGE: return "OVER VOLTAGE";
        case FAULT_UNDER_VOLTAGE: return "UNDER VOLTAGE";
        case FAULT_DRV: return "DRV";
        case FAULT_ABS_OVER_CURRENT: return "ABS OVER CURRENT";
        case FAULT_OVER_TEMP_FET: return "OVER TEMP FET";
        case FAULT_OVER_TEMP_MOTOR: return "OVER TEMP MOTOR";
        case FAULT_GATE_DRIVER_OVER_VOLTAGE: return "GATE DRV OVER V";
        case FAULT_GATE_DRIVER_UNDER_VOLTAGE: return "GATE DRV UNDER V";
        case FAULT_MCU_UNDER_VOLTAGE: return "MCU UNDER VOLTAGE";
        case FAULT_BOOTING_FROM_WATCHDOG_RESET: return "WATCHDOG RESET";
        case FAULT_ENCODER_SPI: return "ENCODER SPI";
        case FAULT_ENCODER_SINCOS_BELOW_MIN_AMPLITUDE: return "ENC SINCOS LOW";
        case FAULT_ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE: return "ENC SINCOS HIGH";
        case FAULT_FLASH_CORRUPTION: return "FLASH CORRUPTION";
        case FAULT_HIGH_OFFSET_CURRENT_SENSOR_1: return "CUR SENSOR 1 OFFSET";
        case FAULT_HIGH_OFFSET_CURRENT_SENSOR_2: return "CUR SENSOR 2 OFFSET";
        case FAULT_HIGH_OFFSET_CURRENT_SENSOR_3: return "CUR SENSOR 3 OFFSET";
        case FAULT_UNBALANCED_CURRENTS: return "UNBALANCED CURRENTS";
        case FAULT_BRK: return "BRK";
        case FAULT_RESOLVER_LOT: return "RESOLVER LOT";
        case FAULT_RESOLVER_DOS: return "RESOLVER DOS";
        case FAULT_RESOLVER_LOS: return "RESOLVER LOS";
        case FAULT_FLASH_CORRUPTION_APP_CFG: return "FLASH CORRUPT APP";
        case FAULT_FLASH_CORRUPTION_MC_CFG: return "FLASH CORRUPT MC";
        case FAULT_ENCODER_NO_MAGNET: return "ENCODER NO MAGNET";
        case FAULT_ENCODER_MAGNET_TOO_STRONG: return "ENC MAGNET STRONG";
        case FAULT_PHASE_FILTER: return "PHASE FILTER";
        case FAULT_ENCODER_FAULT: return "ENCODER FAULT";
        case FAULT_LV_OUTPUT_FAULT: return "LV OUTPUT";
        default: return "UNKNOWN";
    }
}

}  // namespace vesc
