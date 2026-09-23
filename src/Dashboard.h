// ---------------------------------------------------------------------------
// Dashboard renderer for the 240x135 T-Display screen (TFT_eSPI).
//
// Style: black background, glowing amber text in a digital font, teal and
// cyan accents. Everything is drawn into an off-screen sprite and pushed at
// once. Glow is real: glowing elements are also drawn into a mask sprite,
// which is blurred at half resolution and added under the crisp drawing.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "Settings.h"
#include "VescValues.h"

enum class Page : uint8_t { Main = 0, Stats, Trip, GForce, Battery, Count };

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
    float accelG = 0;           // longitudinal g, + = accelerating
    float peakG = 0;

    vesc::Values values;        // raw telemetry
    uint8_t lastFault = 0;      // latched since boot
    uint32_t packetsOk = 0;
    uint32_t crcErrors = 0;
    uint32_t uptimeS = 0;

    Page page = Page::Main;
    bool settingsOpen = false;
    int settingsIndex = 0;
    Settings settings;
};

class Dashboard {
public:
    void begin(uint8_t rotation);
    void render(const DashboardState &s);

private:
    // Screens
    void drawScreen(Page page, const DashboardState &s);
    void drawBoot(float fade);
    void drawMain(const DashboardState &s);
    void drawStats(const DashboardState &s);
    void drawTrip(const DashboardState &s);
    void drawGForce(const DashboardState &s);
    void drawBattery(const DashboardState &s);
    void drawSettings(const DashboardState &s);
    void drawGear(int cx, int cy, float angleDeg, float fade);
    void drawPageDots(const DashboardState &s);

    // Glowing primitives (drawn to the screen and to the glow mask)
    void text(const char *str, int x, int y, uint8_t font, uint8_t datum, uint16_t color, bool glow = true);
    void pill(int x, int y, int w, int h, uint16_t color, bool glow = true);
    void dot(int x, int y, int r, uint16_t color, bool glow = true);
    void ring(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg, uint16_t color, bool glow);
    void box(int x, int y, int w, int h, uint16_t color, bool filled, bool glow = true);
    void compositeGlow(float gain);

    static void arcInto(TFT_eSprite &spr, int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg,
                        uint16_t color);
    static uint16_t blend(uint16_t a, uint16_t b, float t);
    static uint16_t add(uint16_t base, uint16_t glow, float t);
    uint16_t tempColor(float degC, float warn) const;
    uint16_t cellColor(float cellV) const;
    float smooth(float &state, float target, float rate);

    TFT_eSPI tft_;
    TFT_eSprite spr_ = TFT_eSprite(&tft_);
    TFT_eSprite mask_ = TFT_eSprite(&tft_);
    int w_ = 240;
    int h_ = 135;

    // Glow buffers at half resolution
    static constexpr int kGW = 120;
    static constexpr int kGH = 68;
    uint8_t gr_[kGW * kGH];
    uint8_t gg_[kGW * kGH];
    uint8_t gb_[kGW * kGH];
    uint8_t tmp_[kGW * kGH];
    bool glowEnabled_ = true;

    // Animation state
    uint32_t bootMs_ = 0;
    uint32_t lastFrameMs_ = 0;
    float dt_ = 0.05f;
    Page shownPage_ = Page::Main;
    Page fromPage_ = Page::Main;
    int slideDir_ = 1;
    uint32_t slideStartMs_ = 0;
    bool sliding_ = false;
    bool settingsShown_ = false;
    uint32_t settingsOpenMs_ = 0;
    float batteryAnim_ = 0;
    float gAnim_ = 0;
};
