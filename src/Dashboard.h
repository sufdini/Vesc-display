// ---------------------------------------------------------------------------
// Dashboard renderer for the 240x135 T-Display screen (TFT_eSPI).
// Everything is drawn into an off-screen sprite and pushed at once so the
// screen never flickers.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "VescValues.h"

enum class Page : uint8_t { Main = 0, Power, Trip, System, Count };

// Everything the UI needs, already converted to display units.
struct DashboardState {
    bool connected = false;
    bool everConnected = false;
    bool imperial = false;

    float speed = 0;            // km/h or mph
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
    void showSplash(const char *line1, const char *line2);

private:
    void drawStatusBar(const DashboardState &s);
    void drawPageMain(const DashboardState &s);
    void drawPagePower(const DashboardState &s);
    void drawPageTrip(const DashboardState &s);
    void drawPageSystem(const DashboardState &s);
    void drawPageDots(const DashboardState &s);
    void drawBattery(int x, int y, int w, int h, float percent, bool connected);
    void drawLabelValue(int x, int y, const char *label, const char *value, uint16_t color);
    void drawBar(int x, int y, int w, int h, float fraction, uint16_t color);

    uint16_t batteryColor(float percent) const;
    uint16_t tempColor(float degC, float warn, float hot) const;

    TFT_eSPI tft_;
    TFT_eSprite spr_ = TFT_eSprite(&tft_);
    int w_ = 240;
    int h_ = 135;
};
