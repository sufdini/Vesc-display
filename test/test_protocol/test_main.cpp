// Host-side unit tests for the VESC protocol library.
//   pio test -e native
#include <unity.h>

#include <string.h>

#include "VescMath.h"
#include "VescPacket.h"
#include "VescValues.h"

using namespace vesc;

namespace {

// Helpers to build a COMM_GET_VALUES payload the way the VESC firmware does.
void putInt16(uint8_t *buf, size_t &i, int16_t v) {
    buf[i++] = static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF);
    buf[i++] = static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF);
}
void putInt32(uint8_t *buf, size_t &i, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    buf[i++] = static_cast<uint8_t>((u >> 24) & 0xFF);
    buf[i++] = static_cast<uint8_t>((u >> 16) & 0xFF);
    buf[i++] = static_cast<uint8_t>((u >> 8) & 0xFF);
    buf[i++] = static_cast<uint8_t>(u & 0xFF);
}
void putFloat16(uint8_t *buf, size_t &i, float v, float scale) {
    putInt16(buf, i, static_cast<int16_t>(v * scale));
}
void putFloat32(uint8_t *buf, size_t &i, float v, float scale) {
    putInt32(buf, i, static_cast<int32_t>(v * scale));
}

// Build a full firmware-6.x style COMM_GET_VALUES payload. Returns length.
size_t buildGetValues(uint8_t *buf) {
    size_t i = 0;
    buf[i++] = COMM_GET_VALUES;
    putFloat16(buf, i, 41.5f, 10);        // temp fet
    putFloat16(buf, i, 55.2f, 10);        // temp motor
    putFloat32(buf, i, 23.45f, 100);      // motor current
    putFloat32(buf, i, -4.5f, 100);       // input current (regen)
    putFloat32(buf, i, 0.5f, 100);        // id
    putFloat32(buf, i, 22.0f, 100);       // iq
    putFloat16(buf, i, 0.734f, 1000);     // duty
    putFloat32(buf, i, 12345.0f, 1);      // erpm
    putFloat16(buf, i, 39.8f, 10);        // voltage
    putFloat32(buf, i, 1.2345f, 10000);   // Ah
    putFloat32(buf, i, 0.1111f, 10000);   // Ah charged
    putFloat32(buf, i, 45.6789f, 10000);  // Wh
    putFloat32(buf, i, 3.21f, 10000);     // Wh charged
    putInt32(buf, i, -4200);              // tachometer
    putInt32(buf, i, 4200);               // tachometer abs
    buf[i++] = FAULT_OVER_TEMP_FET;       // fault
    putFloat32(buf, i, 123.456f, 1000000);// pid pos
    buf[i++] = 7;                         // controller id
    putFloat16(buf, i, 40.0f, 10);        // mos1
    putFloat16(buf, i, 41.0f, 10);        // mos2
    putFloat16(buf, i, 42.0f, 10);        // mos3
    putFloat32(buf, i, 1.5f, 1000);       // vd
    putFloat32(buf, i, -2.5f, 1000);      // vq
    buf[i++] = 0x02;                      // status
    return i;
}

}  // namespace

// ---- CRC ------------------------------------------------------------------

void test_crc16_known_vector() {
    // CRC-16/XMODEM check value for "123456789".
    const uint8_t data[] = "123456789";
    TEST_ASSERT_EQUAL_HEX16(0x31C3, crc16(data, 9));
}

void test_crc16_empty_is_zero() {
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc16(nullptr, 0));
}

// ---- Framing --------------------------------------------------------------

void test_encode_get_values_request() {
    const uint8_t payload[] = {COMM_GET_VALUES};
    uint8_t out[16];
    const size_t n = encodePacket(payload, 1, out, sizeof(out));
    // Well known frame for COMM_GET_VALUES: 02 01 04 40 84 03
    const uint8_t expected[] = {0x02, 0x01, 0x04, 0x40, 0x84, 0x03};
    TEST_ASSERT_EQUAL_size_t(6, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 6);
}

void test_encode_rejects_small_buffer() {
    // 3 byte payload frames to 8 bytes; one byte short must be rejected.
    const uint8_t payload[] = {1, 2, 3};
    uint8_t out[7];
    TEST_ASSERT_EQUAL_size_t(0, encodePacket(payload, 3, out, sizeof(out)));
    uint8_t ok[8];
    TEST_ASSERT_EQUAL_size_t(8, encodePacket(payload, 3, ok, sizeof(ok)));
}

void test_encode_long_packet_uses_two_byte_length() {
    uint8_t payload[300];
    memset(payload, 0xAB, sizeof(payload));
    uint8_t out[320];
    const size_t n = encodePacket(payload, 300, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(300 + 6, n);
    TEST_ASSERT_EQUAL_HEX8(3, out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x2C, out[2]);
    TEST_ASSERT_EQUAL_HEX8(3, out[n - 1]);
}

void test_decoder_roundtrip() {
    const uint8_t payload[] = {0x04, 0x11, 0x22, 0x33};
    uint8_t frame[16];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));

    PacketDecoder dec;
    bool got = false;
    for (size_t i = 0; i < n; i++) {
        const bool r = dec.feed(frame[i]);
        TEST_ASSERT_TRUE_MESSAGE(!(got && r), "packet reported twice");
        if (r) {
            got = true;
            TEST_ASSERT_EQUAL_size_t(i, n - 1);
        }
    }
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_size_t(sizeof(payload), dec.payloadLength());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, dec.payload(), sizeof(payload));
    TEST_ASSERT_EQUAL_UINT32(0, dec.crcErrors());
    TEST_ASSERT_EQUAL_UINT32(0, dec.framingErrors());
}

void test_decoder_roundtrip_long_packet() {
    uint8_t payload[400];
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = static_cast<uint8_t>(i * 7);
    uint8_t frame[420];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));

    PacketDecoder dec;
    bool got = false;
    for (size_t i = 0; i < n; i++) got |= dec.feed(frame[i]);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_size_t(400, dec.payloadLength());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(payload, dec.payload(), sizeof(payload));
}

void test_decoder_rejects_bad_crc() {
    const uint8_t payload[] = {0x04, 0x11, 0x22, 0x33};
    uint8_t frame[16];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));
    frame[3] ^= 0xFF;  // corrupt a payload byte

    PacketDecoder dec;
    bool got = false;
    for (size_t i = 0; i < n; i++) got |= dec.feed(frame[i]);
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_EQUAL_UINT32(1, dec.crcErrors());
}

void test_decoder_rejects_bad_end_byte() {
    const uint8_t payload[] = {0x04};
    uint8_t frame[16];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));
    frame[n - 1] = 0x00;

    PacketDecoder dec;
    bool got = false;
    for (size_t i = 0; i < n; i++) got |= dec.feed(frame[i]);
    TEST_ASSERT_FALSE(got);
    TEST_ASSERT_EQUAL_UINT32(1, dec.framingErrors());
}

void test_decoder_resyncs_after_noise() {
    const uint8_t payload[] = {0x04, 0xAA};
    uint8_t frame[16];
    const size_t n = encodePacket(payload, sizeof(payload), frame, sizeof(frame));

    PacketDecoder dec;
    // Garbage before the frame, including a stray start byte with a bogus
    // zero length which must be discarded.
    const uint8_t noise[] = {0x00, 0xFF, 0x02, 0x00, 0x99};
    bool got = false;
    for (uint8_t b : noise) got |= dec.feed(b);
    TEST_ASSERT_FALSE(got);
    for (size_t i = 0; i < n; i++) got |= dec.feed(frame[i]);
    TEST_ASSERT_TRUE(got);
    TEST_ASSERT_EQUAL_size_t(2, dec.payloadLength());
    TEST_ASSERT_EQUAL_HEX8(0xAA, dec.payload()[1]);
}

void test_decoder_two_back_to_back_packets() {
    const uint8_t p1[] = {0x04, 0x01};
    const uint8_t p2[] = {0x04, 0x02};
    uint8_t frame[32];
    size_t n = encodePacket(p1, sizeof(p1), frame, sizeof(frame));
    n += encodePacket(p2, sizeof(p2), frame + n, sizeof(frame) - n);

    PacketDecoder dec;
    int count = 0;
    uint8_t last = 0;
    for (size_t i = 0; i < n; i++) {
        if (dec.feed(frame[i])) {
            count++;
            last = dec.payload()[1];
        }
    }
    TEST_ASSERT_EQUAL_INT(2, count);
    TEST_ASSERT_EQUAL_HEX8(0x02, last);
}

// ---- COMM_GET_VALUES decoding --------------------------------------------

void test_decode_get_values_full() {
    uint8_t buf[128];
    const size_t len = buildGetValues(buf);
    Values v;
    TEST_ASSERT_TRUE(decodeGetValues(buf, len, v));

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 41.5f, v.tempFet);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.2f, v.tempMotor);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 23.45f, v.currentMotor);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -4.5f, v.currentInput);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, v.currentId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 22.0f, v.currentIq);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.734f, v.dutyCycle);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 12345.0f, v.erpm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 39.8f, v.voltageInput);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.2345f, v.ampHours);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.1111f, v.ampHoursCharged);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 45.6789f, v.wattHours);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.21f, v.wattHoursCharged);
    TEST_ASSERT_EQUAL_INT32(-4200, v.tachometer);
    TEST_ASSERT_EQUAL_INT32(4200, v.tachometerAbs);
    TEST_ASSERT_EQUAL_UINT8(FAULT_OVER_TEMP_FET, v.fault);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 123.456f, v.pidPos);
    TEST_ASSERT_EQUAL_UINT8(7, v.controllerId);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, v.tempMos1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 41.0f, v.tempMos2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 42.0f, v.tempMos3);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, v.vd);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.5f, v.vq);
    TEST_ASSERT_EQUAL_UINT8(0x02, v.status);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 39.8f * -4.5f, v.powerInput());
}

void test_decode_get_values_old_firmware_short_payload() {
    // Older firmware stops right after the fault byte.
    uint8_t buf[128];
    buildGetValues(buf);
    Values v;
    TEST_ASSERT_TRUE(decodeGetValues(buf, kGetValuesMinLength, v));
    TEST_ASSERT_EQUAL_UINT8(FAULT_OVER_TEMP_FET, v.fault);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 39.8f, v.voltageInput);
    // Optional fields stay at their defaults.
    TEST_ASSERT_EQUAL_UINT8(0, v.controllerId);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, v.pidPos);
    TEST_ASSERT_EQUAL_UINT8(0, v.status);
}

void test_decode_get_values_rejects_truncated() {
    uint8_t buf[128];
    buildGetValues(buf);
    Values v;
    TEST_ASSERT_FALSE(decodeGetValues(buf, kGetValuesMinLength - 1, v));
}

void test_decode_get_values_rejects_other_command() {
    uint8_t buf[128];
    const size_t len = buildGetValues(buf);
    buf[0] = COMM_FW_VERSION;
    Values v;
    TEST_ASSERT_FALSE(decodeGetValues(buf, len, v));
}

void test_fault_names() {
    TEST_ASSERT_EQUAL_STRING("NONE", faultName(FAULT_NONE));
    TEST_ASSERT_EQUAL_STRING("OVER TEMP FET", faultName(FAULT_OVER_TEMP_FET));
    TEST_ASSERT_EQUAL_STRING("LV OUTPUT", faultName(FAULT_LV_OUTPUT_FAULT));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", faultName(200));
}

// ---- Math -----------------------------------------------------------------

void test_speed_direct_drive() {
    // 14 pole hub motor, 90 mm wheel, direct drive.
    VehicleConfig cfg;
    cfg.motorPoles = 14;
    cfg.wheelDiameterMm = 90.0f;
    cfg.gearRatio = 1.0f;
    // 7000 ERPM -> 1000 motor rpm -> 1000 * 0.2827 m * 60 / 1000 = 16.96 km/h
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 16.96f, erpmToKmh(7000.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -16.96f, erpmToKmh(-7000.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, erpmToKmh(0.0f, cfg));
}

void test_speed_belt_drive() {
    VehicleConfig cfg;
    cfg.motorPoles = 14;
    cfg.wheelDiameterMm = 97.0f;
    cfg.gearRatio = 15.0f / 36.0f;
    // 21000 ERPM -> 3000 motor rpm -> 1250 wheel rpm -> 1250 * 0.30473 * 0.06 = 22.85 km/h
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 22.85f, erpmToKmh(21000.0f, cfg));
}

void test_speed_guards_zero_poles() {
    VehicleConfig cfg;
    cfg.motorPoles = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, erpmToKmh(5000.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tachoToKm(5000, cfg));
}

void test_distance_from_tachometer() {
    VehicleConfig cfg;
    cfg.motorPoles = 14;
    cfg.wheelDiameterMm = 90.0f;
    cfg.gearRatio = 1.0f;
    // 42 steps per motor revolution; 4200 steps = 100 revs = 28.27 m
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, 0.02827f, tachoToKm(4200, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.0005f, -0.02827f, tachoToKm(-4200, cfg));
}

void test_unit_conversion() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 62.14f, kmhToMph(100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.21f, kmToMiles(10.0f));
}

void test_battery_percent_curve() {
    VehicleConfig cfg;
    cfg.batteryCells = 10;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, batteryPercent(42.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, batteryPercent(43.0f, cfg));  // above full clamps
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, batteryPercent(33.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, batteryPercent(30.0f, cfg));    // below empty clamps
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 48.0f, batteryPercent(38.0f, cfg));   // on a curve point
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 56.5f, batteryPercent(38.5f, cfg));   // interpolated
    // Monotonic
    float prev = -1.0f;
    for (float v = 30.0f; v <= 43.0f; v += 0.1f) {
        const float p = batteryPercent(v, cfg);
        TEST_ASSERT_TRUE(p >= prev);
        prev = p;
    }
}

void test_battery_guards_zero_cells() {
    VehicleConfig cfg;
    cfg.batteryCells = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, cellVoltage(40.0f, cfg));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, batteryPercent(40.0f, cfg));
}

void test_wh_per_km() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, whPerKm(100.0f, 0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.5f, whPerKm(100.0f, 8.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_crc16_empty_is_zero);
    RUN_TEST(test_encode_get_values_request);
    RUN_TEST(test_encode_rejects_small_buffer);
    RUN_TEST(test_encode_long_packet_uses_two_byte_length);
    RUN_TEST(test_decoder_roundtrip);
    RUN_TEST(test_decoder_roundtrip_long_packet);
    RUN_TEST(test_decoder_rejects_bad_crc);
    RUN_TEST(test_decoder_rejects_bad_end_byte);
    RUN_TEST(test_decoder_resyncs_after_noise);
    RUN_TEST(test_decoder_two_back_to_back_packets);
    RUN_TEST(test_decode_get_values_full);
    RUN_TEST(test_decode_get_values_old_firmware_short_payload);
    RUN_TEST(test_decode_get_values_rejects_truncated);
    RUN_TEST(test_decode_get_values_rejects_other_command);
    RUN_TEST(test_fault_names);
    RUN_TEST(test_speed_direct_drive);
    RUN_TEST(test_speed_belt_drive);
    RUN_TEST(test_speed_guards_zero_poles);
    RUN_TEST(test_distance_from_tachometer);
    RUN_TEST(test_unit_conversion);
    RUN_TEST(test_battery_percent_curve);
    RUN_TEST(test_battery_guards_zero_cells);
    RUN_TEST(test_wh_per_km);
    return UNITY_END();
}
