// ---------------------------------------------------------------------------
// VESC UART packet framing
//
// Every message on the VESC serial link is framed as:
//
//   [2] [len]        [payload ...] [crc hi] [crc lo] [3]     len < 256
//   [3] [len hi] [len lo] [payload ...] [crc hi] [crc lo] [3] len >= 256
//
// The CRC is CRC-16/XMODEM (poly 0x1021, init 0) over the payload only.
// This file is plain C++ with no Arduino dependency so it can be unit tested
// on the host.
// ---------------------------------------------------------------------------
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace vesc {

// VESC command ids (subset of COMM_PACKET_ID from the VESC firmware).
enum CommPacketId : uint8_t {
    COMM_FW_VERSION = 0,
    COMM_GET_VALUES = 4,
    COMM_SET_DUTY = 5,
    COMM_SET_CURRENT = 6,
    COMM_SET_CURRENT_BRAKE = 7,
    COMM_ALIVE = 30,
    COMM_GET_VALUES_SELECTIVE = 50,
};

// CRC-16/XMODEM as used by the VESC firmware (crc.c).
uint16_t crc16(const uint8_t *data, size_t len);

// Wrap a payload in a VESC frame. Returns the number of bytes written to
// `out`, or 0 if `out` is too small. `outSize` must be >= payloadLen + 6.
size_t encodePacket(const uint8_t *payload, size_t payloadLen, uint8_t *out, size_t outSize);

// Incremental packet decoder. Feed it bytes as they arrive from the serial
// port; when a complete frame with a valid CRC has been received, feed()
// returns true and payload()/payloadLength() describe the message.
class PacketDecoder {
public:
    static constexpr size_t kMaxPayload = 512;

    PacketDecoder() { reset(); }

    // Returns true when a complete, CRC-valid packet has just been decoded.
    bool feed(uint8_t byte);

    const uint8_t *payload() const { return payload_; }
    size_t payloadLength() const { return payloadLen_; }

    // Diagnostics
    uint32_t crcErrors() const { return crcErrors_; }
    uint32_t framingErrors() const { return framingErrors_; }

    void reset();

private:
    enum class State : uint8_t { Start, LenHi, LenLo, Payload, CrcHi, CrcLo, End };

    State state_;
    size_t expectedLen_;
    size_t payloadLen_;
    uint16_t crc_;
    uint32_t crcErrors_;
    uint32_t framingErrors_;
    uint8_t payload_[kMaxPayload];
};

}  // namespace vesc
