#ifndef VESC_NATIVE_TEST

#include "VescUart.h"

namespace vesc {

void VescUart::requestValues() {
    const uint8_t payload[1] = {COMM_GET_VALUES};
    uint8_t frame[8];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));
    if (n > 0) {
        serial_.write(frame, n);
    }
}

bool VescUart::update() {
    bool gotValues = false;
    while (serial_.available() > 0) {
        const int b = serial_.read();
        if (b < 0) {
            break;
        }
        if (decoder_.feed(static_cast<uint8_t>(b))) {
            if (decodeGetValues(decoder_.payload(), decoder_.payloadLength(), values_)) {
                lastValuesMs_ = millis();
                if (lastValuesMs_ == 0) {
                    lastValuesMs_ = 1;  // keep everConnected() meaningful
                }
                packetsOk_++;
                gotValues = true;
            }
        }
    }
    return gotValues;
}

}  // namespace vesc

#endif  // VESC_NATIVE_TEST
