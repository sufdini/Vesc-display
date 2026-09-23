// ---------------------------------------------------------------------------
// VESC Display - ESP32 firmware entry point
//
// Polls a VESC over UART for COMM_GET_VALUES, converts the telemetry into
// speed / distance / battery figures and draws a dashboard on the TFT.
// Two buttons cycle through the pages.
// ---------------------------------------------------------------------------
#include <Arduino.h>

#include "Dashboard.h"
#include "VescMath.h"
#include "VescUart.h"
#include "config.h"

namespace {

HardwareSerial vescSerial(2);
vesc::VescUart vescClient(vescSerial);
Dashboard dashboard;
DashboardState state;

const vesc::VehicleConfig vehicle = {MOTOR_POLES, WHEEL_DIAMETER_MM, GEAR_RATIO, BATTERY_CELLS};

constexpr uint32_t kRenderIntervalMs = 50;  // ~20 fps
constexpr float kSpeedSmoothing = 0.35f;     // EMA factor for the speed readout

uint32_t lastPollMs = 0;
uint32_t lastRenderMs = 0;
float smoothedSpeedKmh = 0.0f;
float maxSpeedKmh = 0.0f;

// Simple debounced push-button helper. T-Display buttons pull the pin low.
class Button {
public:
    explicit Button(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        stable_ = digitalRead(pin_);
        lastRaw_ = stable_;
    }

    // Returns true once per press (on release edge -> press edge).
    bool pressed() {
        const int raw = digitalRead(pin_);
        const uint32_t now = millis();
        if (raw != lastRaw_) {
            lastChangeMs_ = now;
            lastRaw_ = raw;
        }
        if ((now - lastChangeMs_) > kDebounceMs && raw != stable_) {
            stable_ = raw;
            if (stable_ == LOW) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr uint32_t kDebounceMs = 30;
    uint8_t pin_;
    int stable_ = HIGH;
    int lastRaw_ = HIGH;
    uint32_t lastChangeMs_ = 0;
};

Button nextButton(BUTTON_NEXT_PIN);
Button prevButton(BUTTON_PREV_PIN);

void nextPage(int delta) {
    const int count = static_cast<int>(Page::Count);
    int p = static_cast<int>(state.page) + delta;
    p = ((p % count) + count) % count;
    state.page = static_cast<Page>(p);
}

// Convert raw telemetry to display units.
void updateDerivedState() {
    const vesc::Values &v = vescClient.values();
    state.values = v;
    state.imperial = USE_IMPERIAL_UNITS != 0;

    const float rawKmh = vesc::erpmToKmh(v.erpm, vehicle);
    smoothedSpeedKmh += kSpeedSmoothing * (rawKmh - smoothedSpeedKmh);
    if (fabsf(smoothedSpeedKmh) < 0.3f) {
        smoothedSpeedKmh = 0.0f;  // kill jitter at standstill
    }
    if (fabsf(rawKmh) > maxSpeedKmh) {
        maxSpeedKmh = fabsf(rawKmh);
    }

    const float tripKm = vesc::tachoToKm(v.tachometerAbs, vehicle);
    const float whNet = v.wattHours - v.wattHoursCharged;

    if (state.imperial) {
        state.speed = vesc::kmhToMph(smoothedSpeedKmh);
        state.maxSpeed = vesc::kmhToMph(maxSpeedKmh);
        state.tripDistance = vesc::kmToMiles(tripKm);
        state.whPerDistance = vesc::whPerKm(whNet, vesc::kmToMiles(tripKm));
    } else {
        state.speed = smoothedSpeedKmh;
        state.maxSpeed = maxSpeedKmh;
        state.tripDistance = tripKm;
        state.whPerDistance = vesc::whPerKm(whNet, tripKm);
    }

    state.batteryVoltage = v.voltageInput;
    state.cellVoltage = vesc::cellVoltage(v.voltageInput, vehicle);
    state.batteryPercent = vesc::batteryPercent(v.voltageInput, vehicle);
    state.powerW = v.powerInput();

    if (v.fault != vesc::FAULT_NONE) {
        state.lastFault = v.fault;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.println();
    Serial.println("VESC Display starting");

    vescSerial.begin(VESC_UART_BAUD, SERIAL_8N1, VESC_RX_PIN, VESC_TX_PIN);

    nextButton.begin();
    prevButton.begin();

    dashboard.begin(SCREEN_ROTATION);

    state.imperial = USE_IMPERIAL_UNITS != 0;
}

void loop() {
    const uint32_t now = millis();

    // Buttons
    if (nextButton.pressed()) {
        nextPage(+1);
    }
    if (prevButton.pressed()) {
        nextPage(-1);
    }

    // Poll the VESC
    if (now - lastPollMs >= VESC_POLL_INTERVAL_MS) {
        lastPollMs = now;
        vescClient.requestValues();
    }
    if (vescClient.update()) {
        updateDerivedState();
    }

    state.everConnected = vescClient.everConnected();
    state.connected = state.everConnected && vescClient.msSinceLastValues() < VESC_TIMEOUT_MS;
    if (!state.connected) {
        // Decay the speed readout so a dropped link does not freeze a number.
        smoothedSpeedKmh = 0.0f;
        state.speed = 0.0f;
        state.powerW = 0.0f;
    }
    state.packetsOk = vescClient.packetsOk();
    state.crcErrors = vescClient.crcErrors();
    state.uptimeS = now / 1000;

    // Draw
    if (now - lastRenderMs >= kRenderIntervalMs) {
        lastRenderMs = now;
        dashboard.render(state);
    }

    // Periodic serial log for debugging without the screen
    static uint32_t lastLogMs = 0;
    if (now - lastLogMs >= 1000) {
        lastLogMs = now;
        const vesc::Values &v = state.values;
        Serial.printf("link=%d speed=%.1f V=%.1f I=%.1f P=%.0fW Tfet=%.0f Tmot=%.0f fault=%s pk=%lu crc=%lu\n",
                      state.connected, state.speed, v.voltageInput, v.currentInput, state.powerW, v.tempFet,
                      v.tempMotor, vesc::faultName(v.fault), static_cast<unsigned long>(state.packetsOk),
                      static_cast<unsigned long>(state.crcErrors));
    }
}
