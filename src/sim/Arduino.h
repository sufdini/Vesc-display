// ---------------------------------------------------------------------------
// Arduino stand-in for the PC simulator.
//
// Provides just enough of the Arduino API for src/main.cpp and the VescUart
// library to compile and run on a desktop: a virtual millisecond clock,
// simulated buttons, a Serial that prints to stdout and a HardwareSerial
// that behaves like a VESC answering COMM_GET_VALUES.
// ---------------------------------------------------------------------------
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <deque>

#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 5
#define SERIAL_8N1 0x800001c

template <class T>
T constrain(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

uint32_t millis();
void pinMode(uint8_t pin, uint8_t mode);
int digitalRead(uint8_t pin);
inline void ledcSetup(uint8_t, double, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, uint32_t) {}

class Stream {
public:
    virtual ~Stream() {}
    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t write(const uint8_t *buf, size_t len) = 0;
};

// The VESC end of the UART link. Bytes written to it are parsed for a
// COMM_GET_VALUES request and answered with synthetic telemetry.
class HardwareSerial : public Stream {
public:
    explicit HardwareSerial(int uartNum) : uartNum_(uartNum) {}
    void begin(unsigned long baud, uint32_t config = SERIAL_8N1, int8_t rx = -1, int8_t tx = -1);
    int available() override;
    int read() override;
    size_t write(const uint8_t *buf, size_t len) override;

    // Console-style helpers used by main.cpp on the USB serial.
    void println(const char *s = "");
    void printf(const char *fmt, ...);

private:
    int uartNum_;
    std::deque<uint8_t> rx_;   // bytes the ESP32 will read
    std::deque<uint8_t> tx_;   // bytes the ESP32 wrote, waiting to be parsed
};

extern HardwareSerial Serial;

// ---- Simulator control (used by sim_main.cpp) ------------------------------
namespace sim {

// Advance the virtual clock.
void advanceMillis(uint32_t ms);

// Silence the firmware's Serial (USB console) output.
void setQuiet(bool quiet);

// Hold a button (active low) down for `ms` virtual milliseconds.
void pressButton(uint8_t pin, uint32_t ms);

// Fake VESC behaviour.
void setVescOnline(bool online);
void setVescFault(uint8_t fault);
void setMotorPoles(int poles);
void setVoltageOverride(float volts);  // <= 0 disables

// Synthetic ride model: what the fake VESC reports at virtual time t.
struct RideSample {
    float erpm, duty, currentIn, currentMotor, voltage, tempFet, tempMotor;
    float ah, ahCharged, wh, whCharged;
    int32_t tacho;
};
RideSample rideAt(float tSeconds, int motorPoles);

}  // namespace sim
