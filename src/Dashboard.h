// ---------------------------------------------------------------------------
// Dashboard renderer for the 240x135 T-Display screen (TFT_eSPI).
//
// Everything is drawn into an off-screen sprite and pushed at once, so the
// screen never flickers. The renderer keeps a little animation state of its
// own (page slide, boot sweep, smoothed bars); all vehicle data arrives in
// DashboardState, already converted to display units.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "VescValues.h"

enum class Page : uint8_t { Main = 0, Power, Trip, System, Count };

struct DashboardState {
    bool connected = false;
    bool everConnected = false;
    bool imperial = false;

    float speed = 0;            // km/h or mph, signed (negative = reverse)
    float maxSpeed = 0;
    float batteryPercent = 0;   // 0..100
    float batteryVoltage = 0;   // V
    float cellVoltage = 0;      // V
    float powerW = 0;           // W (negative = regen)
    float tripDistance = 0;     // km or mi
    float whPerDistance = 0;    // Wh/km or Wh/mi
    float odometer = 0;         // km or mi (absolute tachometer)

    vesc::Values values;        // raw telemetry
    uint8_t lastFault = 0;      // latched since boot
    uint32_t packetsOk = 0;
    uint32_t crcErrors = 0;
    uint32_t uptimeS = 0;
    Page page = Page::Main;
};

class Dashboard {
public:
    void begin(uint8_t rotation);
    void render(const DashboardState &s);

private:
    // Pages
    void drawPage(Page page, const DashboardState &s);
    void drawPageMain(const DashboardState &s);
    void drawPagePower(const DashboardState &s);
    void drawPageTrip(const DashboardState &s);
    void drawPageSystem(const DashboardState &s);

    // Widgets
    void drawStatusBar(const DashboardState &s);
    void drawSpeedGauge(float arcFraction, float peakFraction, bool alert);
    void drawPageDots(const DashboardState &s);
    void drawCell(int x, int y, const char *label, const char *value, uint16_t color);
    void drawBar(int x, int y, int w, int h, float fraction, uint16_t color);
    void drawBipolarBar(int x, int y, int w, int h, float fraction, uint16_t posColor, uint16_t negColor);
    void drawBatteryIcon(int x, int y, int w, int h, float percent, uint16_t color);

    // Anti-aliased ring segment. Angles in degrees, 0 = right, clockwise on
    // screen. `sweep` may exceed 180. Blends against `bg`.
    void drawArc(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg, uint16_t color, uint16_t bg);
    // The gauge: full track plus a colour graded value arc in one pass.
    void drawGaugeRing(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg, float valueFraction,
                       bool alert);

    static uint16_t blend(uint16_t a, uint16_t b, float t);
    uint16_t batteryColor(float percent) const;
    uint16_t tempColor(float degC, float warn, float hot) const;
    uint16_t gaugeColor(float t, bool alert) const;
    float smooth(float &state, float target, float rate);

    TFT_eSPI tft_;
    TFT_eSprite spr_ = TFT_eSprite(&tft_);
    int w_ = 240;
    int h_ = 135;

    // Animation state
    uint32_t bootMs_ = 0;
    uint32_t lastFrameMs_ = 0;
    float dt_ = 0.05f;
    Page shownPage_ = Page::Main;
    Page fromPage_ = Page::Main;
    int slideDir_ = 1;
    uint32_t slideStartMs_ = 0;
    bool sliding_ = false;
    float arcAnim_ = 0;      // smoothed arc fraction
    float batteryAnim_ = 0;  // smoothed battery bar fraction
    float powerAnim_ = 0;    // smoothed power bar fraction
    float dutyAnim_ = 0;
    float motorAnim_ = 0;
    float battAAnim_ = 0;
};
