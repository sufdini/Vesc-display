// ---------------------------------------------------------------------------
// VESC Display - ESP32 firmware entry point
//
// Polls a VESC over UART for COMM_GET_VALUES, converts the telemetry into
// speed / distance / battery figures and draws the dashboard on the TFT.
//
// Buttons (T-Display):
//   right short  next page            right hold   open / close settings
//   left  short  previous page
//   in settings: right short = next row, left short = change value
// ---------------------------------------------------------------------------
#include <Arduino.h>

#include "Dashboard.h"
#include "Settings.h"
#include "VescMath.h"
#include "VescUart.h"
#include "config.h"

namespace {

HardwareSerial vescSerial(2);
vesc::VescUart vescClient(vescSerial);
Dashboard dashboard;
DashboardState state;
Settings settings;

vesc::VehicleConfig vehicle = {MOTOR_POLES, WHEEL_DIAMETER_MM, GEAR_RATIO, BATTERY_CELLS};

constexpr uint32_t kRenderIntervalMs = 50;  // ~20 fps
constexpr float kSpeedSmoothing = 0.35f;     // EMA factor for the speed readout
constexpr uint8_t kBacklightChannel = 0;

uint32_t lastPollMs = 0;
uint32_t lastRenderMs = 0;
float smoothedSpeedKmh = 0.0f;
float maxSpeedKmh = 0.0f;
float lastRawKmh = 0.0f;
uint32_t lastSpeedMs = 0;
float accelG = 0.0f;
float peakG = 0.0f;

// Debounced push button with short and long press detection (active low).
class Button {
public:
    enum class Event : uint8_t { None, Short, Long };

    explicit Button(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT_PULLUP);
        stable_ = digitalRead(pin_);
        lastRaw_ = stable_;
    }

    Event poll() {
        const int raw = digitalRead(pin_);
        const uint32_t now = millis();
        if (raw != lastRaw_) {
            lastChangeMs_ = now;
            lastRaw_ = raw;
        }
        if ((now - lastChangeMs_) > kDebounceMs && raw != stable_) {
            stable_ = raw;
            if (stable_ == LOW) {
                pressedMs_ = now;
                longFired_ = false;
            } else if (!longFired_) {
                return Event::Short;
            }
        }
        if (stable_ == LOW && !longFired_ && now - pressedMs_ >= kLongMs) {
            longFired_ = true;
            return Event::Long;
        }
        return Event::None;
    }

private:
    static constexpr uint32_t kDebounceMs = 30;
    static constexpr uint32_t kLongMs = 600;
    uint8_t pin_;
    int stable_ = HIGH;
    int lastRaw_ = HIGH;
    uint32_t lastChangeMs_ = 0;
    uint32_t pressedMs_ = 0;
    bool longFired_ = false;
};

Button nextButton(BUTTON_NEXT_PIN);
Button prevButton(BUTTON_PREV_PIN);

void nextPage(int delta) {
    const int count = static_cast<int>(Page::Count);
    int p = static_cast<int>(state.page) + delta;
    p = ((p % count) + count) % count;
    state.page = static_cast<Page>(p);
}

// Push the settings into everything that depends on them.
void applySettings() {
    vehicle.batteryCells = settings.cells;
    state.imperial = settings.imperial;
    state.settings = settings;
    ledcWrite(kBacklightChannel, 255UL * settings.brightness / 100);
}

// Convert raw telemetry to display units.
void updateDerivedState() {
    const vesc::Values &v = vescClient.values();
    state.values = v;

    const float rawKmh = vesc::erpmToKmh(v.erpm, vehicle);
    smoothedSpeedKmh += kSpeedSmoothing * (rawKmh - smoothedSpeedKmh);
    if (fabsf(smoothedSpeedKmh) < 0.3f) {
        smoothedSpeedKmh = 0.0f;  // kill jitter at standstill
    }
    if (fabsf(rawKmh) > maxSpeedKmh) {
        maxSpeedKmh = fabsf(rawKmh);
    }

    // Longitudinal acceleration from the change in speed (no IMU needed).
    const uint32_t nowMs = millis();
    if (lastSpeedMs != 0 && nowMs > lastSpeedMs) {
        const float dt = (nowMs - lastSpeedMs) / 1000.0f;
        const float a = (rawKmh - lastRawKmh) / 3.6f / dt / 9.81f;
        accelG += 0.25f * (a - accelG);
        if (fabsf(accelG) > peakG) peakG = fabsf(accelG);
    }
    lastRawKmh = rawKmh;
    lastSpeedMs = nowMs;
    state.accelG = accelG;
    state.peakG = peakG;

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

void handleButtons() {
    const Button::Event next = nextButton.poll();
    const Button::Event prev = prevButton.poll();

    if (state.settingsOpen) {
        if (next == Button::Event::Short) {
            state.settingsIndex = (state.settingsIndex + 1) % kSettingItemCount;
        } else if (prev == Button::Event::Short) {
            settingAdjust(settings, state.settingsIndex);
            applySettings();
        } else if (next == Button::Event::Long || prev == Button::Event::Long) {
            settings.save();
            state.settingsOpen = false;
        }
        return;
    }

    if (next == Button::Event::Long) {
        state.settingsOpen = true;
        state.settingsIndex = 0;
    } else if (next == Button::Event::Short) {
        nextPage(+1);
    } else if (prev == Button::Event::Short) {
        nextPage(-1);
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

    // Backlight PWM (TFT_eSPI only switches it on; we dim it ourselves).
    ledcSetup(kBacklightChannel, 5000, 8);
    ledcAttachPin(TFT_BL, kBacklightChannel);

    if (!settings.load()) {
        // First boot: seed the stored settings from config.h.
        settings.imperial = USE_IMPERIAL_UNITS != 0;
        settings.cells = BATTERY_CELLS;
        settings.save();
    }
    applySettings();

    dashboard.begin(SCREEN_ROTATION);
}

void loop() {
    const uint32_t now = millis();

    handleButtons();

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
        // Decay the readouts so a dropped link does not freeze a number.
        smoothedSpeedKmh = 0.0f;
        state.speed = 0.0f;
        state.powerW = 0.0f;
        state.accelG = 0.0f;
        accelG = 0.0f;
        lastSpeedMs = 0;
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
