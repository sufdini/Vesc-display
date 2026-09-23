#include "Dashboard.h"

#include <math.h>
#include <stdio.h>

#include "config.h"

namespace {
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;
constexpr uint16_t kDim = 0x7BEF;      // mid grey (labels)
constexpr uint16_t kTrack = 0x2124;    // dark grey (gauge track)
constexpr uint16_t kGood = 0x07E0;     // green
constexpr uint16_t kWarn = 0xFD20;     // amber
constexpr uint16_t kBad = 0xF800;      // red
constexpr uint16_t kAccent = 0x05FF;   // cyan
constexpr uint16_t kAccent2 = 0x3D9F;  // light blue
constexpr uint16_t kRegen = 0x3D9F;

constexpr int kStatusBarH = 18;

// Gauge geometry (main page)
constexpr int kGaugeCx = 60;
constexpr int kGaugeCy = 62;
constexpr float kGaugeRIn = 42.0f;
constexpr float kGaugeROut = 50.0f;
constexpr float kGaugeStart = 135.0f;  // bottom left
constexpr float kGaugeSweep = 270.0f;  // clockwise to bottom right

constexpr uint32_t kBootSweepMs = 1400;
constexpr uint32_t kSlideMs = 240;

constexpr float kDegToRad = 3.14159265f / 180.0f;

const char *speedUnit(bool imperial) { return imperial ? "mph" : SPEED_UNIT_LABEL; }

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
}  // namespace

// ---------------------------------------------------------------------------
// Setup and frame loop
// ---------------------------------------------------------------------------

void Dashboard::begin(uint8_t rotation) {
    tft_.init();
    tft_.setRotation(rotation);
    tft_.fillScreen(kBg);
    w_ = tft_.width();
    h_ = tft_.height();
    spr_.setColorDepth(16);
    spr_.createSprite(w_, h_);
    spr_.setTextDatum(TL_DATUM);
    bootMs_ = millis();
    lastFrameMs_ = bootMs_;
}

float Dashboard::smooth(float &state, float target, float rate) {
    // Exponential approach, frame-rate independent.
    const float a = 1.0f - expf(-rate * dt_);
    state += (target - state) * a;
    return state;
}

void Dashboard::render(const DashboardState &s) {
    const uint32_t now = millis();
    dt_ = (now - lastFrameMs_) / 1000.0f;
    if (dt_ <= 0.0f || dt_ > 0.5f) dt_ = 0.05f;
    lastFrameMs_ = now;

    // Page change: start a slide.
    if (s.page != shownPage_) {
        const int count = static_cast<int>(Page::Count);
        const int delta = (static_cast<int>(s.page) - static_cast<int>(shownPage_) + count) % count;
        slideDir_ = delta == 1 ? 1 : -1;
        fromPage_ = shownPage_;
        shownPage_ = s.page;
        slideStartMs_ = now;
        sliding_ = true;
    }

    spr_.fillSprite(kBg);

    if (sliding_) {
        const float p = clamp01((now - slideStartMs_) / static_cast<float>(kSlideMs));
        const int off = static_cast<int>(roundf(easeOutCubic(p) * w_));
        spr_.setViewport(-off * slideDir_, 0, w_, h_);
        drawPage(fromPage_, s);
        spr_.setViewport((w_ - off) * slideDir_, 0, w_, h_);
        drawPage(shownPage_, s);
        spr_.resetViewport();
        if (p >= 1.0f) sliding_ = false;
    } else {
        drawPage(shownPage_, s);
    }

    spr_.pushSprite(0, 0);
}

void Dashboard::drawPage(Page page, const DashboardState &s) {
    switch (page) {
        case Page::Main: drawPageMain(s); break;
        case Page::Power: drawPagePower(s); break;
        case Page::Trip: drawPageTrip(s); break;
        case Page::System: drawPageSystem(s); break;
        default: break;
    }
    drawPageDots(s);
}

// ---------------------------------------------------------------------------
// Colour helpers
// ---------------------------------------------------------------------------

uint16_t Dashboard::blend(uint16_t a, uint16_t b, float t) {
    t = clamp01(t);
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = ar + static_cast<int>(roundf((br - ar) * t));
    const int g = ag + static_cast<int>(roundf((bg - ag) * t));
    const int bl = ab + static_cast<int>(roundf((bb - ab) * t));
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

uint16_t Dashboard::batteryColor(float percent) const {
    if (percent > 40.0f) return kGood;
    if (percent > 20.0f) return kWarn;
    return kBad;
}

uint16_t Dashboard::tempColor(float degC, float warn, float hot) const {
    if (degC >= hot) return kBad;
    if (degC >= warn) return kWarn;
    return kFg;
}

// Colour along the gauge: cyan, then amber, then red near full scale.
uint16_t Dashboard::gaugeColor(float t, bool alert) const {
    if (alert) return kBad;
    if (t < 0.6f) return blend(kAccent2, kAccent, t / 0.6f);
    if (t < 0.8f) return blend(kAccent, kWarn, (t - 0.6f) / 0.2f);
    return blend(kWarn, kBad, (t - 0.8f) / 0.2f);
}

// ---------------------------------------------------------------------------
// Arc drawing (anti-aliased, no trig per pixel outside the ring band)
// ---------------------------------------------------------------------------

void Dashboard::drawArc(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg, uint16_t color,
                        uint16_t bg) {
    const int r = static_cast<int>(rOut) + 2;
    const float rIn2 = (rIn - 1.0f) * (rIn - 1.0f);
    const float rOut2 = (rOut + 1.0f) * (rOut + 1.0f);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const float d2 = static_cast<float>(x * x + y * y);
            if (d2 < rIn2 || d2 > rOut2) continue;
            const float d = sqrtf(d2);
            const float ar = clamp01(fminf(d - rIn + 0.5f, rOut + 0.5f - d));
            if (ar <= 0.0f) continue;
            float rel = atan2f(static_cast<float>(y), static_cast<float>(x)) / kDegToRad - startDeg;
            while (rel < 0) rel += 360.0f;
            while (rel >= 360.0f) rel -= 360.0f;
            // Soft angular ends: distance to the end in pixels along the arc.
            const float aa = clamp01(fminf(rel, sweepDeg - rel) * kDegToRad * d + 0.5f);
            if (aa <= 0.0f) continue;
            spr_.drawPixel(cx + x, cy + y, blend(bg, color, ar * aa));
        }
    }
}

void Dashboard::drawGaugeRing(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg,
                              float valueFraction, bool alert) {
    const float valueSweep = clamp01(valueFraction) * sweepDeg;
    const int r = static_cast<int>(rOut) + 2;
    const float rIn2 = (rIn - 1.0f) * (rIn - 1.0f);
    const float rOut2 = (rOut + 1.0f) * (rOut + 1.0f);
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            const float d2 = static_cast<float>(x * x + y * y);
            if (d2 < rIn2 || d2 > rOut2) continue;
            const float d = sqrtf(d2);
            const float ar = clamp01(fminf(d - rIn + 0.5f, rOut + 0.5f - d));
            if (ar <= 0.0f) continue;
            float rel = atan2f(static_cast<float>(y), static_cast<float>(x)) / kDegToRad - startDeg;
            while (rel < 0) rel += 360.0f;
            while (rel >= 360.0f) rel -= 360.0f;
            const float at = clamp01(fminf(rel, sweepDeg - rel) * kDegToRad * d + 0.5f);
            if (at <= 0.0f) continue;
            uint16_t c = blend(kBg, kTrack, ar * at);
            if (valueSweep > 0.0f) {
                const float av = clamp01((valueSweep - rel) * kDegToRad * d + 0.5f);
                if (av > 0.0f) {
                    c = blend(c, gaugeColor(rel / sweepDeg, alert), ar * av);
                }
            }
            spr_.drawPixel(cx + x, cy + y, c);
        }
    }
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void Dashboard::drawCell(int x, int y, const char *label, const char *value, uint16_t color) {
    spr_.setTextDatum(TL_DATUM);
    spr_.setTextColor(kDim, kBg);
    spr_.drawString(label, x, y, 1);
    spr_.setTextColor(color, kBg);
    spr_.drawString(value, x, y + 9, 4);
}

void Dashboard::drawBar(int x, int y, int w, int h, float fraction, uint16_t color) {
    spr_.fillRect(x, y, w, h, kTrack);
    const int fillW = static_cast<int>(roundf(w * clamp01(fraction)));
    if (fillW > 0) spr_.fillRect(x, y, fillW, h, color);
}

void Dashboard::drawBipolarBar(int x, int y, int w, int h, float fraction, uint16_t posColor, uint16_t negColor) {
    spr_.fillRect(x, y, w, h, kTrack);
    const int mid = x + w / 2;
    const float f = constrain(fraction, -1.0f, 1.0f);
    const int len = static_cast<int>(roundf(fabsf(f) * (w / 2)));
    if (len > 0) {
        if (f >= 0) spr_.fillRect(mid, y, len, h, posColor);
        else spr_.fillRect(mid - len, y, len, h, negColor);
    }
    spr_.drawFastVLine(mid, y - 1, h + 2, kFg);
}

void Dashboard::drawBatteryIcon(int x, int y, int w, int h, float percent, uint16_t color) {
    spr_.drawRect(x, y, w, h, kDim);
    spr_.fillRect(x + w, y + h / 4, 2, h / 2, kDim);
    const int fillW = static_cast<int>(roundf((w - 4) * clamp01(percent / 100.0f)));
    if (fillW > 0) spr_.fillRect(x + 2, y + 2, fillW, h - 4, color);
}

void Dashboard::drawPageDots(const DashboardState &s) {
    // Small vertical column of dots at the bottom right.
    const int n = static_cast<int>(Page::Count);
    const int spacing = 7;
    const int x = w_ - 4;
    const int y0 = h_ - 4 - (n - 1) * spacing;
    for (int i = 0; i < n; i++) {
        if (i == static_cast<int>(s.page)) spr_.fillCircle(x, y0 + i * spacing, 2, kFg);
        else spr_.fillCircle(x, y0 + i * spacing, 1, kDim);
    }
}

void Dashboard::drawStatusBar(const DashboardState &s) {
    char buf[32];
    const uint32_t now = millis();
    spr_.setTextDatum(TL_DATUM);

    const uint16_t dot = s.connected ? kGood : (s.everConnected ? kBad : kDim);
    spr_.fillCircle(7, kStatusBarH / 2 - 1, 3, dot);

    if (!s.connected) {
        spr_.setTextColor(s.everConnected ? kBad : kDim, kBg);
        spr_.drawString(s.everConnected ? "NO LINK" : "NO VESC", 16, 1, 2);
    } else if (s.values.fault != vesc::FAULT_NONE) {
        const bool on = (now / 350) % 2 == 0;
        spr_.setTextColor(on ? kBad : kDim, kBg);
        snprintf(buf, sizeof(buf), "FAULT  %s", vesc::faultName(s.values.fault));
        spr_.drawString(buf, 16, 1, 2);
    } else {
        spr_.setTextColor(kDim, kBg);
        snprintf(buf, sizeof(buf), "%.1fV   %.2fV/c", s.batteryVoltage, s.cellVoltage);
        spr_.drawString(buf, 16, 1, 2);
    }

    spr_.setTextDatum(TR_DATUM);
    spr_.setTextColor(s.connected ? batteryColor(s.batteryPercent) : kDim, kBg);
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.batteryPercent)));
    spr_.drawString(buf, w_ - 34, 1, 2);
    drawBatteryIcon(w_ - 30, 3, 24, 11, s.batteryPercent, s.connected ? batteryColor(s.batteryPercent) : kDim);
    spr_.setTextDatum(TL_DATUM);

    spr_.drawFastHLine(0, kStatusBarH, w_, kTrack);
}

// ---------------------------------------------------------------------------
// Main page
// ---------------------------------------------------------------------------

void Dashboard::drawSpeedGauge(float arcFraction, float peakFraction, bool alert) {
    drawGaugeRing(kGaugeCx, kGaugeCy, kGaugeRIn, kGaugeROut, kGaugeStart, kGaugeSweep, arcFraction, alert);

    // Peak marker just outside the ring.
    if (peakFraction > 0.01f) {
        drawArc(kGaugeCx, kGaugeCy, kGaugeROut + 2.0f, kGaugeROut + 5.0f,
                kGaugeStart + peakFraction * kGaugeSweep - 1.5f, 3.0f, kFg, kBg);
    }

    // Tick marks at 0, 25, 50, 75, 100 %.
    for (int i = 0; i <= 4; i++) {
        const float a = (kGaugeStart + kGaugeSweep * i / 4.0f) * kDegToRad;
        const int x0 = kGaugeCx + static_cast<int>(roundf(cosf(a) * (kGaugeRIn - 3.0f)));
        const int y0 = kGaugeCy + static_cast<int>(roundf(sinf(a) * (kGaugeRIn - 3.0f)));
        spr_.drawPixel(x0, y0, kDim);
    }
}

void Dashboard::drawPageMain(const DashboardState &s) {
    char buf[32];
    const uint32_t now = millis();
    const bool live = s.connected;
    const bool fault = live && s.values.fault != vesc::FAULT_NONE;
    const bool blinkOn = (now / 350) % 2 == 0;
    const float pulse = 0.5f + 0.5f * sinf(now * (2.0f * 3.14159265f / 1200.0f));

    // --- Boot sweep: arc and digits run up to full scale and back --------
    const uint32_t sinceBoot = now - bootMs_;
    const bool booting = sinceBoot < kBootSweepMs;
    float speedShown = fabsf(s.speed);
    float arcTarget = clamp01(speedShown / SPEED_GAUGE_MAX);
    if (booting) {
        const float t = sinceBoot / static_cast<float>(kBootSweepMs);
        const float f = sinf(t * 3.14159265f);
        arcTarget = f;
        arcAnim_ = f;
        speedShown = f * SPEED_GAUGE_MAX;
    } else {
        smooth(arcAnim_, live ? arcTarget : 0.0f, 12.0f);
    }
    const float peak = live ? clamp01(s.maxSpeed / SPEED_GAUGE_MAX) : 0.0f;

    // --- Gauge ------------------------------------------------------------
    drawSpeedGauge(arcAnim_, peak, fault);

    const uint16_t digitColor = !live && !booting ? kDim : (fault && !blinkOn ? kDim : kFg);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(speedShown)));
    spr_.setTextColor(digitColor, kBg);
    spr_.setTextDatum(TC_DATUM);
    spr_.drawString(buf, kGaugeCx, kGaugeCy - 21, 6);

    // Unit / status word inside the gauge opening.
    if (!live && !booting) {
        spr_.setTextColor(s.everConnected ? blend(kBad, kDim, pulse) : kDim, kBg);
        spr_.drawString(s.everConnected ? "NO LINK" : "NO VESC", kGaugeCx, kGaugeCy + 24, 2);
    } else if (fault) {
        spr_.setTextColor(blinkOn ? kBad : kDim, kBg);
        spr_.drawString("FAULT", kGaugeCx, kGaugeCy + 24, 2);
    } else if (s.speed < -0.5f) {
        spr_.setTextColor(kWarn, kBg);
        spr_.drawString("REVERSE", kGaugeCx, kGaugeCy + 24, 2);
    } else {
        spr_.setTextColor(kDim, kBg);
        spr_.drawString(speedUnit(s.imperial), kGaugeCx, kGaugeCy + 24, 2);
    }
    spr_.setTextDatum(TL_DATUM);

    // --- Battery bar under the gauge ---------------------------------------
    smooth(batteryAnim_, live ? s.batteryPercent / 100.0f : 0.0f, 6.0f);
    const uint16_t battColor = live ? batteryColor(s.batteryPercent) : kDim;
    const bool lowBatt = live && s.batteryPercent < 15.0f;
    drawBar(12, 120, 96, 8, batteryAnim_, lowBatt ? blend(kBad, kTrack, pulse * 0.6f) : battColor);
    spr_.setTextColor(kDim, kBg);
    snprintf(buf, sizeof(buf), "MAX %d", static_cast<int>(roundf(s.maxSpeed)));
    spr_.drawString(buf, 12, 110, 1);
    snprintf(buf, sizeof(buf), "%.1fV", s.batteryVoltage);
    spr_.setTextDatum(TR_DATUM);
    spr_.drawString(buf, 108, 110, 1);
    spr_.setTextDatum(TL_DATUM);

    // --- Divider ------------------------------------------------------------
    spr_.drawFastVLine(114, 8, h_ - 16, kTrack);

    // --- Right column: battery percent (or fault banner), then readings ----
    const int cx = 120;
    if (fault) {
        // Red banner with the fault name; the percent is still on the bar.
        spr_.fillRect(cx - 2, 2, w_ - cx - 6, 25, blinkOn ? kBad : blend(kBad, kBg, 0.5f));
        spr_.setTextDatum(TC_DATUM);
        spr_.setTextColor(kFg, blinkOn ? kBad : blend(kBad, kBg, 0.5f));
        spr_.drawString("FAULT", cx - 2 + (w_ - cx - 6) / 2, 4, 1);
        spr_.drawString(vesc::faultName(s.values.fault), cx - 2 + (w_ - cx - 6) / 2, 15, 1);
        spr_.setTextDatum(TL_DATUM);
    } else {
        snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.batteryPercent)));
        spr_.setTextColor(lowBatt ? blend(kBad, kDim, pulse) : battColor, kBg);
        spr_.drawString(buf, cx, 2, 4);

        spr_.setTextDatum(TR_DATUM);
        spr_.setTextColor(live ? kFg : kDim, kBg);
        snprintf(buf, sizeof(buf), "%.1fV", s.batteryVoltage);
        spr_.drawString(buf, w_ - 4, 2, 2);
        spr_.setTextColor(kDim, kBg);
        snprintf(buf, sizeof(buf), "%.2fV/cell", s.cellVoltage);
        spr_.drawString(buf, w_ - 4, 18, 1);
        spr_.setTextDatum(TL_DATUM);
    }

    const uint16_t num = live ? kFg : kDim;
    const int col2 = cx + 62;
    const int r0 = 31, r1 = 65, r2 = 99;

    snprintf(buf, sizeof(buf), "%.2f", s.values.ampHours - s.values.ampHoursCharged);
    drawCell(cx, r0, "AH USED", buf, num);
    if (s.tripDistance < 100.0f) snprintf(buf, sizeof(buf), "%.2f", s.tripDistance);
    else snprintf(buf, sizeof(buf), "%.1f", s.tripDistance);
    drawCell(col2, r0, s.imperial ? "TRIP MI" : "TRIP KM", buf, num);

    snprintf(buf, sizeof(buf), "%.1f", s.values.currentInput);
    const uint16_t currentColor = !live ? kDim : (s.values.currentInput < -0.5f ? kRegen : kFg);
    drawCell(cx, r1, "BATTERY A", buf, currentColor);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(s.powerW)));
    const uint16_t powerColor = !live ? kDim : (s.powerW < -5.0f ? kRegen : kFg);
    drawCell(col2, r1, "POWER W", buf, powerColor);

    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempMotor)));
    drawCell(cx, r2, "MOTOR", buf, live ? tempColor(s.values.tempMotor, 80, 100) : kDim);
    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempFet)));
    drawCell(col2, r2, "ESC", buf, live ? tempColor(s.values.tempFet, 70, 85) : kDim);
}

// ---------------------------------------------------------------------------
// Secondary pages
// ---------------------------------------------------------------------------

void Dashboard::drawPagePower(const DashboardState &s) {
    char buf[32];
    drawStatusBar(s);
    const bool live = s.connected;
    const uint16_t num = live ? kFg : kDim;
    const int colW = 78;
    const int r0 = kStatusBarH + 6;
    const int r1 = r0 + 36;

    snprintf(buf, sizeof(buf), "%.1f", s.values.currentMotor);
    drawCell(6, r0, "MOTOR A", buf, num);
    snprintf(buf, sizeof(buf), "%.1f", s.values.currentInput);
    drawCell(6 + colW, r0, "BATTERY A", buf, s.values.currentInput < -0.5f ? kRegen : num);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(s.powerW)));
    drawCell(6 + colW * 2, r0, "POWER W", buf, s.powerW < -5.0f ? kRegen : num);

    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.values.dutyCycle * 100.0f)));
    drawCell(6, r1, "DUTY", buf, fabsf(s.values.dutyCycle) > 0.9f ? kWarn : num);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.values.erpm));
    drawCell(6 + colW, r1, "ERPM", buf, num);
    snprintf(buf, sizeof(buf), "%.1f", s.batteryVoltage);
    drawCell(6 + colW * 2, r1, "VOLTAGE", buf, num);

    // Animated bars: duty (0..1), battery current (bipolar, 40 A full scale).
    smooth(dutyAnim_, live ? fabsf(s.values.dutyCycle) : 0.0f, 10.0f);
    smooth(battAAnim_, live ? s.values.currentInput / 40.0f : 0.0f, 10.0f);
    spr_.setTextColor(kDim, kBg);
    spr_.drawString("DUTY", 6, h_ - 30, 1);
    drawBar(40, h_ - 30, w_ - 52, 7, dutyAnim_, dutyAnim_ > 0.9f ? kWarn : kAccent);
    spr_.drawString("AMPS", 6, h_ - 18, 1);
    drawBipolarBar(40, h_ - 18, w_ - 52, 7, battAAnim_, kAccent, kRegen);
}

void Dashboard::drawPageTrip(const DashboardState &s) {
    char buf[32];
    drawStatusBar(s);
    const uint16_t num = s.connected ? kFg : kDim;
    const int colW = 78;
    const int r0 = kStatusBarH + 6;
    const int r1 = r0 + 36;
    const int r2 = r1 + 36;

    if (s.tripDistance < 100.0f) snprintf(buf, sizeof(buf), "%.2f", s.tripDistance);
    else snprintf(buf, sizeof(buf), "%.1f", s.tripDistance);
    drawCell(6, r0, s.imperial ? "TRIP MI" : "TRIP KM", buf, num);
    snprintf(buf, sizeof(buf), "%.2f", s.values.ampHours - s.values.ampHoursCharged);
    drawCell(6 + colW, r0, "USED AH", buf, num);
    snprintf(buf, sizeof(buf), "%.1f", s.values.wattHours - s.values.wattHoursCharged);
    drawCell(6 + colW * 2, r0, "USED WH", buf, num);

    snprintf(buf, sizeof(buf), "%.1f", s.whPerDistance);
    drawCell(6, r1, s.imperial ? "WH PER MI" : "WH PER KM", buf, num);
    snprintf(buf, sizeof(buf), "%.2f", s.values.ampHoursCharged);
    drawCell(6 + colW, r1, "REGEN AH", buf, s.values.ampHoursCharged > 0.001f ? kRegen : num);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(s.maxSpeed)));
    drawCell(6 + colW * 2, r1, s.imperial ? "MAX MPH" : "MAX KM/H", buf, num);

    spr_.setTextColor(kDim, kBg);
    spr_.drawString("COUNTERS RESET WHEN THE VESC POWERS OFF", 6, r2 + 2, 1);
}

void Dashboard::drawPageSystem(const DashboardState &s) {
    char buf[40];
    drawStatusBar(s);
    const bool live = s.connected;
    const uint16_t num = live ? kFg : kDim;
    const int colW = 78;
    const int r0 = kStatusBarH + 6;
    const int r1 = r0 + 36;
    const int r2 = r1 + 36;

    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempFet)));
    drawCell(6, r0, "ESC TEMP", buf, live ? tempColor(s.values.tempFet, 70, 85) : kDim);
    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempMotor)));
    drawCell(6 + colW, r0, "MOTOR TEMP", buf, live ? tempColor(s.values.tempMotor, 80, 100) : kDim);
    snprintf(buf, sizeof(buf), "%d", s.values.controllerId);
    drawCell(6 + colW * 2, r0, "VESC ID", buf, num);

    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.packetsOk));
    drawCell(6, r1, "PACKETS", buf, num);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.crcErrors));
    drawCell(6 + colW, r1, "CRC ERRORS", buf, s.crcErrors > 0 ? kWarn : num);
    snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(s.uptimeS / 60),
             static_cast<unsigned long>(s.uptimeS % 60));
    drawCell(6 + colW * 2, r1, "UPTIME", buf, kFg);

    // Temperature bars (0..100 C) and the last fault since boot.
    smooth(motorAnim_, live ? s.values.tempMotor / 100.0f : 0.0f, 6.0f);
    spr_.setTextColor(kDim, kBg);
    spr_.drawString("MOTOR", 6, r2 + 2, 1);
    drawBar(46, r2 + 2, 120, 7, motorAnim_, tempColor(s.values.tempMotor, 80, 100) == kFg ? kAccent : tempColor(s.values.tempMotor, 80, 100));

    if (s.lastFault != vesc::FAULT_NONE) {
        spr_.setTextColor(kWarn, kBg);
        snprintf(buf, sizeof(buf), "LAST FAULT: %s", vesc::faultName(s.lastFault));
    } else {
        spr_.setTextColor(kDim, kBg);
        snprintf(buf, sizeof(buf), "NO FAULTS SINCE BOOT");
    }
    spr_.drawString(buf, 6, r2 + 14, 1);
}
