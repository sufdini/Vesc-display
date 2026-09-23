#include "Arduino.h"

#include <stdarg.h>
#include <string.h>

#include <map>

#include "VescPacket.h"

namespace {

uint32_t g_millis = 0;

struct ButtonState {
    uint32_t releaseAt = 0;  // virtual time when the press ends
};
std::map<uint8_t, ButtonState> g_buttons;

bool g_vescOnline = true;
uint8_t g_vescFault = 0;
int g_motorPoles = 14;

int32_t g_lastTacho = 0;
bool g_quiet = false;

}  // namespace

uint32_t millis() { return g_millis; }

void pinMode(uint8_t, uint8_t) {}

int digitalRead(uint8_t pin) {
    auto it = g_buttons.find(pin);
    if (it != g_buttons.end() && g_millis < it->second.releaseAt) {
        return LOW;
    }
    return HIGH;
}

// ---- Fake VESC --------------------------------------------------------------

namespace sim {

void advanceMillis(uint32_t ms) { g_millis += ms; }
void setQuiet(bool quiet) { g_quiet = quiet; }

void pressButton(uint8_t pin, uint32_t ms) { g_buttons[pin].releaseAt = g_millis + ms; }

void setVescOnline(bool online) { g_vescOnline = online; }
void setVescFault(uint8_t fault) { g_vescFault = fault; }
void setMotorPoles(int poles) { g_motorPoles = poles; }

RideSample rideAt(float t, int poles) {
    // 40 second cycle: accelerate, cruise, brake with regen, stand still.
    const float phase = fmodf(t, 40.0f);
    float motorRpm;
    if (phase < 10) {
        motorRpm = 900.0f * phase / 10.0f;
    } else if (phase < 25) {
        motorRpm = 900.0f + 100.0f * sinf(phase);
    } else if (phase < 32) {
        motorRpm = 900.0f * (32.0f - phase) / 7.0f;
    } else {
        motorRpm = 0.0f;
    }
    const bool braking = phase >= 25 && phase < 32 && motorRpm > 50.0f;
    const float duty = fminf(motorRpm / 1000.0f, 0.95f);

    RideSample s;
    s.erpm = motorRpm * poles / 2.0f;
    s.duty = duty;
    s.currentIn = braking ? -6.0f : 3.0f + 25.0f * duty;
    s.currentMotor = s.currentIn * 1.6f;
    s.voltage = 41.5f - 0.02f * t - 0.05f * fmaxf(s.currentIn, 0.0f);
    s.tempFet = 32.0f + 20.0f * duty + 0.05f * t;
    s.tempMotor = 28.0f + 35.0f * duty + 0.08f * t;
    s.ah = 0.0004f * t;
    s.ahCharged = 0.00003f * t;
    s.wh = s.ah * 40.0f;
    s.whCharged = s.ahCharged * 40.0f;
    // Integrate distance: tachometer counts 3 * poles steps per motor rev.
    s.tacho = g_lastTacho;
    return s;
}

}  // namespace sim

namespace {

void put16(std::deque<uint8_t> &q, int16_t v) {
    q.push_back(static_cast<uint8_t>((static_cast<uint16_t>(v) >> 8) & 0xFF));
    q.push_back(static_cast<uint8_t>(static_cast<uint16_t>(v) & 0xFF));
}
void put32(std::deque<uint8_t> &q, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    q.push_back(static_cast<uint8_t>((u >> 24) & 0xFF));
    q.push_back(static_cast<uint8_t>((u >> 16) & 0xFF));
    q.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    q.push_back(static_cast<uint8_t>(u & 0xFF));
}

// Build the COMM_GET_VALUES reply exactly as VESC firmware 6.x lays it out.
void buildGetValuesReply(std::deque<uint8_t> &out) {
    static uint32_t lastMs = 0;
    const float t = g_millis / 1000.0f;
    sim::RideSample s = sim::rideAt(t, g_motorPoles);

    // Advance the tachometer by the motor revolutions since the last reply.
    const float dt = (g_millis - lastMs) / 1000.0f;
    lastMs = g_millis;
    const float motorRpm = s.erpm / (g_motorPoles / 2.0f);
    g_lastTacho += static_cast<int32_t>(motorRpm / 60.0f * dt * 3.0f * g_motorPoles);
    s.tacho = g_lastTacho;

    std::deque<uint8_t> p;
    p.push_back(vesc::COMM_GET_VALUES);
    put16(p, static_cast<int16_t>(s.tempFet * 10));
    put16(p, static_cast<int16_t>(s.tempMotor * 10));
    put32(p, static_cast<int32_t>(s.currentMotor * 100));
    put32(p, static_cast<int32_t>(s.currentIn * 100));
    put32(p, 0);
    put32(p, static_cast<int32_t>(s.currentMotor * 100));
    put16(p, static_cast<int16_t>(s.duty * 1000));
    put32(p, static_cast<int32_t>(s.erpm));
    put16(p, static_cast<int16_t>(s.voltage * 10));
    put32(p, static_cast<int32_t>(s.ah * 10000));
    put32(p, static_cast<int32_t>(s.ahCharged * 10000));
    put32(p, static_cast<int32_t>(s.wh * 10000));
    put32(p, static_cast<int32_t>(s.whCharged * 10000));
    put32(p, s.tacho);
    put32(p, s.tacho < 0 ? -s.tacho : s.tacho);
    p.push_back(g_vescFault);
    put32(p, 0);       // pid pos
    p.push_back(42);   // controller id
    put16(p, static_cast<int16_t>(s.tempFet * 10));
    put16(p, static_cast<int16_t>(s.tempFet * 10 + 5));
    put16(p, static_cast<int16_t>(s.tempFet * 10 - 5));
    put32(p, 0);
    put32(p, 0);
    p.push_back(0);    // status

    uint8_t payload[128];
    size_t n = 0;
    for (uint8_t b : p) payload[n++] = b;
    uint8_t framed[160];
    const size_t len = vesc::encodePacket(payload, n, framed, sizeof(framed));
    for (size_t i = 0; i < len; i++) out.push_back(framed[i]);
}

}  // namespace

// ---- HardwareSerial ---------------------------------------------------------

void HardwareSerial::begin(unsigned long, uint32_t, int8_t, int8_t) {}

int HardwareSerial::available() { return static_cast<int>(rx_.size()); }

int HardwareSerial::read() {
    if (rx_.empty()) return -1;
    const uint8_t b = rx_.front();
    rx_.pop_front();
    return b;
}

size_t HardwareSerial::write(const uint8_t *buf, size_t len) {
    if (uartNum_ == 0) {
        if (!g_quiet) fwrite(buf, 1, len, stdout);
        return len;
    }
    for (size_t i = 0; i < len; i++) tx_.push_back(buf[i]);

    // Answer each complete COMM_GET_VALUES request: 02 01 04 40 84 03
    static const uint8_t kRequest[6] = {2, 1, 4, 0x40, 0x84, 3};
    while (tx_.size() >= 6) {
        bool match = true;
        for (size_t i = 0; i < 6; i++) {
            if (tx_[i] != kRequest[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            tx_.erase(tx_.begin(), tx_.begin() + 6);
            if (g_vescOnline) buildGetValuesReply(rx_);
        } else {
            tx_.pop_front();
        }
    }
    return len;
}

void HardwareSerial::println(const char *s) {
    if (uartNum_ == 0 && !g_quiet) {
        fputs(s, stdout);
        fputc('\n', stdout);
    }
}

void HardwareSerial::printf(const char *fmt, ...) {
    if (uartNum_ != 0 || g_quiet) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

HardwareSerial Serial(0);
