#include "Dashboard.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace {
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kAmber = 0xFD80;    // 255,176,0
constexpr uint16_t kAmberDim = 0x7280; // darker amber for secondary text
constexpr uint16_t kGrey = 0x39E7;     // tracks and outlines
constexpr uint16_t kTeal = 0x07F9;     // 0,255,200
constexpr uint16_t kCyan = 0x07FF;
constexpr uint16_t kMagenta = 0xF81F;
constexpr uint16_t kBlue = 0x3D9F;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kYellow = 0xFFE0;
constexpr uint16_t kWhite = TFT_WHITE;

constexpr uint32_t kBootMs = 2200;
constexpr uint32_t kSlideMs = 240;
constexpr uint32_t kGearMs = 700;
constexpr float kPi = 3.14159265f;
constexpr float kDegToRad = kPi / 180.0f;

float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float easeOutCubic(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
const char *speedUnit(bool imperial) { return imperial ? "mph" : SPEED_UNIT_LABEL; }
const char *distUnit(bool imperial) { return imperial ? "mi" : "km"; }
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
    mask_.setColorDepth(8);  // glow mask: colour is enough at 8 bits, saves RAM
    mask_.createSprite(w_, h_);
    bootMs_ = millis();
    lastFrameMs_ = bootMs_;
}

float Dashboard::smooth(float &state, float target, float rate) {
    state += (target - state) * (1.0f - expf(-rate * dt_));
    return state;
}

void Dashboard::render(const DashboardState &s) {
    const uint32_t now = millis();
    dt_ = (now - lastFrameMs_) / 1000.0f;
    if (dt_ <= 0.0f || dt_ > 0.5f) dt_ = 0.05f;
    lastFrameMs_ = now;
    glowEnabled_ = s.settings.glow;

    spr_.fillSprite(kBg);
    mask_.fillSprite(kBg);

    // Boot logo
    const uint32_t sinceBoot = now - bootMs_;
    if (s.settings.bootAnimation && sinceBoot < kBootMs) {
        float fade = 1.0f;
        if (sinceBoot < 500) fade = sinceBoot / 500.0f;
        else if (sinceBoot > kBootMs - 400) fade = (kBootMs - sinceBoot) / 400.0f;
        drawBoot(clamp01(fade));
        compositeGlow(0.9f);
        spr_.pushSprite(0, 0);
        return;
    }

    // Settings overlay (with a spinning gear while it opens)
    if (s.settingsOpen) {
        if (!settingsShown_) {
            settingsShown_ = true;
            settingsOpenMs_ = now;
        }
        const uint32_t t = now - settingsOpenMs_;
        if (s.settings.pageAnimations && t < kGearMs) {
            const float p = t / static_cast<float>(kGearMs);
            drawGear(w_ / 2, h_ / 2, easeOutCubic(p) * 180.0f, p < 0.8f ? 1.0f : (1.0f - p) / 0.2f);
        } else {
            drawSettings(s);
        }
        compositeGlow(0.9f);
        spr_.pushSprite(0, 0);
        return;
    }
    settingsShown_ = false;

    // Page change: start a slide.
    if (s.page != shownPage_) {
        const int count = static_cast<int>(Page::Count);
        const int delta = (static_cast<int>(s.page) - static_cast<int>(shownPage_) + count) % count;
        slideDir_ = delta == 1 ? 1 : -1;
        fromPage_ = shownPage_;
        shownPage_ = s.page;
        slideStartMs_ = now;
        sliding_ = s.settings.pageAnimations;
    }

    if (sliding_) {
        const float p = clamp01((now - slideStartMs_) / static_cast<float>(kSlideMs));
        const int off = static_cast<int>(roundf(easeOutCubic(p) * w_));
        spr_.setViewport(-off * slideDir_, 0, w_, h_);
        mask_.setViewport(-off * slideDir_, 0, w_, h_);
        drawScreen(fromPage_, s);
        spr_.setViewport((w_ - off) * slideDir_, 0, w_, h_);
        mask_.setViewport((w_ - off) * slideDir_, 0, w_, h_);
        drawScreen(shownPage_, s);
        spr_.resetViewport();
        mask_.resetViewport();
        if (p >= 1.0f) sliding_ = false;
    } else {
        drawScreen(shownPage_, s);
    }

    compositeGlow(0.9f);
    spr_.pushSprite(0, 0);
}

void Dashboard::drawScreen(Page page, const DashboardState &s) {
    switch (page) {
        case Page::Main: drawMain(s); break;
        case Page::Stats: drawStats(s); break;
        case Page::Trip: drawTrip(s); break;
        case Page::GForce: drawGForce(s); break;
        case Page::Battery: drawBattery(s); break;
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

// Additive blend: base + glow * t, clamped per channel.
uint16_t Dashboard::add(uint16_t base, uint16_t glow, float t) {
    int r = ((base >> 11) & 0x1F) + static_cast<int>(((glow >> 11) & 0x1F) * t);
    int g = ((base >> 5) & 0x3F) + static_cast<int>(((glow >> 5) & 0x3F) * t);
    int b = (base & 0x1F) + static_cast<int>((glow & 0x1F) * t);
    if (r > 31) r = 31;
    if (g > 63) g = 63;
    if (b > 31) b = 31;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

uint16_t Dashboard::tempColor(float degC, float warn) const {
    if (degC >= warn + 15.0f) return kRed;
    if (degC >= warn) return kYellow;
    return kCyan;
}

uint16_t Dashboard::cellColor(float cellV) const {
    if (cellV >= 3.75f) return kGreen;
    if (cellV >= 3.45f) return kYellow;
    return kRed;
}

// ---------------------------------------------------------------------------
// Glow pipeline
// ---------------------------------------------------------------------------

void Dashboard::text(const char *str, int x, int y, uint8_t font, uint8_t datum, uint16_t color, bool glow) {
    spr_.setTextDatum(datum);
    spr_.setTextColor(color, color);  // transparent background
    spr_.drawString(str, x, y, font);
    if (glow && glowEnabled_) {
        mask_.setTextDatum(datum);
        mask_.setTextColor(color, color);
        mask_.drawString(str, x, y, font);
    }
}

void Dashboard::pill(int x, int y, int w, int h, uint16_t color, bool glow) {
    const int r = h / 2;
    spr_.fillRoundRect(x, y, w, h, r, color);
    if (glow && glowEnabled_) mask_.fillRoundRect(x, y, w, h, r, color);
}

void Dashboard::dot(int x, int y, int r, uint16_t color, bool glow) {
    spr_.fillCircle(x, y, r, color);
    if (glow && glowEnabled_) mask_.fillCircle(x, y, r, color);
}

void Dashboard::box(int x, int y, int w, int h, uint16_t color, bool filled, bool glow) {
    if (filled) spr_.fillRoundRect(x, y, w, h, 4, color);
    else spr_.drawRoundRect(x, y, w, h, 4, color);
    if (glow && glowEnabled_) {
        if (filled) mask_.fillRoundRect(x, y, w, h, 4, color);
        else mask_.drawRoundRect(x, y, w, h, 4, color);
    }
}

void Dashboard::ring(int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg, uint16_t color,
                     bool glow) {
    arcInto(spr_, cx, cy, rIn, rOut, startDeg, sweepDeg, color);
    if (glow && glowEnabled_) arcInto(mask_, cx, cy, rIn, rOut, startDeg, sweepDeg, color);
}

// Anti-aliased ring segment blended against black. Angles in degrees, 0 =
// right, clockwise on screen.
void Dashboard::arcInto(TFT_eSprite &spr, int cx, int cy, float rIn, float rOut, float startDeg, float sweepDeg,
                        uint16_t color) {
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
            float aa = 1.0f;
            if (sweepDeg < 360.0f) {
                aa = clamp01(fminf(rel, sweepDeg - rel) * kDegToRad * d + 0.5f);
                if (aa <= 0.0f) continue;
            }
            spr.drawPixel(cx + x, cy + y, blend(kBg, color, ar * aa));
        }
    }
}

// Blur the mask at half resolution and add it under the frame.
void Dashboard::compositeGlow(float gain) {
    if (!glowEnabled_) return;

    // 1. Downsample: average of each 2x2 block, per channel, scaled to 0..255.
    for (int j = 0; j < kGH; j++) {
        for (int i = 0; i < kGW; i++) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    const uint16_t c = mask_.readPixel(2 * i + dx, 2 * j + dy);
                    r += (c >> 11) & 0x1F;
                    g += (c >> 5) & 0x3F;
                    b += c & 0x1F;
                }
            }
            gr_[j * kGW + i] = static_cast<uint8_t>(r * 2);  // 4 * 31 * 2 = 248
            gg_[j * kGW + i] = static_cast<uint8_t>(g);      // 4 * 63 = 252
            gb_[j * kGW + i] = static_cast<uint8_t>(b * 2);
        }
    }

    // 2. Two passes of a separable box blur (radius 3) per channel.
    uint8_t *channels[3] = {gr_, gg_, gb_};
    for (int ch = 0; ch < 3; ch++) {
        uint8_t *a = channels[ch];
        for (int pass = 0; pass < 2; pass++) {
            for (int j = 0; j < kGH; j++) {
                for (int i = 0; i < kGW; i++) {
                    int sum = 0;
                    for (int k = -3; k <= 3; k++) {
                        int ii = i + k;
                        if (ii < 0) ii = 0;
                        if (ii >= kGW) ii = kGW - 1;
                        sum += a[j * kGW + ii];
                    }
                    tmp_[j * kGW + i] = static_cast<uint8_t>(sum / 7);
                }
            }
            for (int j = 0; j < kGH; j++) {
                for (int i = 0; i < kGW; i++) {
                    int sum = 0;
                    for (int k = -3; k <= 3; k++) {
                        int jj = j + k;
                        if (jj < 0) jj = 0;
                        if (jj >= kGH) jj = kGH - 1;
                        sum += tmp_[jj * kGW + i];
                    }
                    a[j * kGW + i] = static_cast<uint8_t>(sum / 7);
                }
            }
        }
    }

    // 3. Upsample (bilinear) and add under the frame.
    for (int y = 0; y < h_; y++) {
        const float v = y * 0.5f - 0.25f;
        int j0 = static_cast<int>(floorf(v));
        const float fy = v - j0;
        if (j0 < 0) j0 = 0;
        int j1 = j0 + 1 < kGH ? j0 + 1 : j0;
        for (int x = 0; x < w_; x++) {
            const float u = x * 0.5f - 0.25f;
            int i0 = static_cast<int>(floorf(u));
            const float fx = u - i0;
            if (i0 < 0) i0 = 0;
            int i1 = i0 + 1 < kGW ? i0 + 1 : i0;
            const int a00 = j0 * kGW + i0, a01 = j0 * kGW + i1, a10 = j1 * kGW + i0, a11 = j1 * kGW + i1;
            const float w00 = (1 - fx) * (1 - fy), w01 = fx * (1 - fy), w10 = (1 - fx) * fy, w11 = fx * fy;
            const float r = gr_[a00] * w00 + gr_[a01] * w01 + gr_[a10] * w10 + gr_[a11] * w11;
            const float g = gg_[a00] * w00 + gg_[a01] * w01 + gg_[a10] * w10 + gg_[a11] * w11;
            const float b = gb_[a00] * w00 + gb_[a01] * w01 + gb_[a10] * w10 + gb_[a11] * w11;
            if (r + g + b < 3.0f) continue;
            const uint16_t glowColor = static_cast<uint16_t>((static_cast<int>(r / 8) << 11) |
                                                             (static_cast<int>(g / 4) << 5) |
                                                             static_cast<int>(b / 8));
            spr_.drawPixel(x, y, add(spr_.readPixel(x, y), glowColor, gain));
        }
    }
}

// ---------------------------------------------------------------------------
// Boot logo and settings gear
// ---------------------------------------------------------------------------

void Dashboard::drawBoot(float fade) {
    // Emblem: a thick chevron with a bar across the top, glowing amber.
    const uint16_t c = blend(kBg, kAmber, fade);
    const int cx = w_ / 2, cy = h_ / 2 - 6;
    for (int t = -2; t <= 2; t++) {
        spr_.drawLine(cx - 30, cy - 18 + t, cx + 30, cy - 18 + t, c);
        spr_.drawLine(cx - 26 + t, cy - 12, cx + t, cy + 26, c);
        spr_.drawLine(cx + 26 + t, cy - 12, cx + t, cy + 26, c);
        if (glowEnabled_) {
            mask_.drawLine(cx - 30, cy - 18 + t, cx + 30, cy - 18 + t, c);
            mask_.drawLine(cx - 26 + t, cy - 12, cx + t, cy + 26, c);
            mask_.drawLine(cx + 26 + t, cy - 12, cx + t, cy + 26, c);
        }
    }
    dot(cx - 30, cy - 24, 3, c);
    dot(cx + 30, cy - 24, 3, c);
    text("VESC DISPLAY", cx, cy + 36, 2, TC_DATUM, blend(kBg, kAmberDim, fade));
}

void Dashboard::drawGear(int cx, int cy, float angleDeg, float fade) {
    const uint16_t c = blend(kBg, kAmber, fade);
    ring(cx, cy, 10.0f, 16.0f, 0.0f, 360.0f, c, true);
    for (int i = 0; i < 8; i++) {
        ring(cx, cy, 15.0f, 23.0f, angleDeg + i * 45.0f - 11.0f, 22.0f, c, true);
    }
    text("SETTINGS", cx, cy + 34, 2, TC_DATUM, blend(kBg, kAmberDim, fade));
}

void Dashboard::drawPageDots(const DashboardState &s) {
    const int n = static_cast<int>(Page::Count);
    const int spacing = 8;
    const int x0 = w_ / 2 - (n - 1) * spacing / 2;
    for (int i = 0; i < n; i++) {
        const bool active = i == static_cast<int>(s.page);
        spr_.fillCircle(x0 + i * spacing, h_ - 3, active ? 2 : 1, active ? kAmber : kGrey);
    }
}

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------

void Dashboard::drawMain(const DashboardState &s) {
    char buf[32];
    const uint32_t now = millis();
    const bool live = s.connected;
    const bool fault = live && s.values.fault != vesc::FAULT_NONE;
    const bool blinkOn = (now / 350) % 2 == 0;
    const float pulse = 0.5f + 0.5f * sinf(now * (2.0f * kPi / 1200.0f));
    const bool lowBatt = live && s.batteryPercent < 15.0f;

    // Battery pill, top centre.
    smooth(batteryAnim_, live ? s.batteryPercent / 100.0f : 0.0f, 6.0f);
    const int barW = 100, barX = w_ / 2 - barW / 2, barY = 6, barH = 6;
    spr_.fillRoundRect(barX, barY, barW, barH, 3, kGrey);
    const int fillW = static_cast<int>(roundf(barW * batteryAnim_));
    if (fillW >= barH) pill(barX, barY, fillW, barH, lowBatt ? blend(kRed, kGrey, pulse * 0.6f) : kTeal);

    // Corner readouts: trip distance and battery current.
    snprintf(buf, sizeof(buf), "%.2f %s", s.tripDistance, distUnit(s.imperial));
    text(buf, 6, 2, 2, TL_DATUM, live ? kAmberDim : kGrey);
    snprintf(buf, sizeof(buf), "%.1f A", s.values.currentInput);
    text(buf, w_ - 6, 2, 2, TR_DATUM, !live ? kGrey : (s.values.currentInput < -0.5f ? kBlue : kAmberDim));

    // Speed
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(fabsf(s.speed))));
    text(buf, w_ / 2, 22, 7, TC_DATUM, live ? kAmber : kGrey, live);

    if (!live) {
        text(s.everConnected ? "NO LINK" : "NO VESC", w_ / 2, 76, 4, TC_DATUM,
             s.everConnected ? blend(kRed, kGrey, pulse) : kGrey, false);
    } else if (s.speed < -0.5f) {
        text("REVERSE", w_ / 2, 76, 4, TC_DATUM, kYellow);
    } else {
        text(speedUnit(s.imperial), w_ / 2, 76, 4, TC_DATUM, kAmber);
    }

    // Bottom row: ESC temp, motor temp, voltage, each with a status dot.
    if (fault) {
        snprintf(buf, sizeof(buf), "FAULT  %s", vesc::faultName(s.values.fault));
        text(buf, w_ / 2, 110, 2, TC_DATUM, blinkOn ? kRed : blend(kRed, kBg, 0.5f));
    } else {
        const int y = 110;
        const int cx[3] = {40, 120, 200};
        dot(cx[0] - 26, y + 7, 3, live ? tempColor(s.values.tempFet, s.settings.escWarn) : kGrey);
        snprintf(buf, sizeof(buf), "%d`C", static_cast<int>(roundf(s.values.tempFet)));
        text(buf, cx[0] - 18, y, 2, TL_DATUM, live ? kAmber : kGrey);

        dot(cx[1] - 26, y + 7, 3, live ? tempColor(s.values.tempMotor, s.settings.motorWarn) : kGrey);
        snprintf(buf, sizeof(buf), "%d`C", static_cast<int>(roundf(s.values.tempMotor)));
        text(buf, cx[1] - 18, y, 2, TL_DATUM, live ? kAmber : kGrey);

        dot(cx[2] - 26, y + 7, 3, live ? (lowBatt ? blend(kRed, kGrey, pulse) : kMagenta) : kGrey);
        snprintf(buf, sizeof(buf), "%.1fV", s.batteryVoltage);
        text(buf, cx[2] - 18, y, 2, TL_DATUM, live ? kAmber : kGrey);
    }
}

void Dashboard::drawStats(const DashboardState &s) {
    char buf[32];
    const bool live = s.connected;
    const uint16_t val = live ? kAmber : kGrey;
    const int cx[3] = {40, 120, 200};
    const int label0 = 10, value0 = 28, label1 = 72, value1 = 90;

    text("ESC", cx[0], label0, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempFet)));
    text(buf, cx[0], value0, 4, TC_DATUM, live ? (tempColor(s.values.tempFet, s.settings.escWarn) == kCyan ? kAmber : tempColor(s.values.tempFet, s.settings.escWarn)) : kGrey);

    text("MOTOR", cx[1], label0, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempMotor)));
    text(buf, cx[1], value0, 4, TC_DATUM, live ? (tempColor(s.values.tempMotor, s.settings.motorWarn) == kCyan ? kAmber : tempColor(s.values.tempMotor, s.settings.motorWarn)) : kGrey);

    text("VOLT", cx[2], label0, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%.1f", s.batteryVoltage);
    text(buf, cx[2], value0, 4, TC_DATUM, val);

    text("POWER", cx[0], label1, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(s.powerW)));
    text(buf, cx[0], value1, 4, TC_DATUM, !live ? kGrey : (s.powerW < -5.0f ? kBlue : kAmber));

    text("AMPS", cx[1], label1, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%.1f", s.values.currentInput);
    text(buf, cx[1], value1, 4, TC_DATUM, !live ? kGrey : (s.values.currentInput < -0.5f ? kBlue : kAmber));

    text("AH USED", cx[2], label1, 2, TC_DATUM, kAmberDim);
    snprintf(buf, sizeof(buf), "%.2f", s.values.ampHours - s.values.ampHoursCharged);
    text(buf, cx[2], value1, 4, TC_DATUM, val);
}

void Dashboard::drawTrip(const DashboardState &s) {
    char buf[48];
    const bool live = s.connected;
    if (s.tripDistance < 100.0f) snprintf(buf, sizeof(buf), "%.2f", s.tripDistance);
    else snprintf(buf, sizeof(buf), "%.1f", s.tripDistance);
    text(buf, w_ / 2, 16, 7, TC_DATUM, live ? kAmber : kGrey, live);
    text(distUnit(s.imperial), w_ / 2, 70, 4, TC_DATUM, kAmber);
    snprintf(buf, sizeof(buf), "MAX %d   %.1f WH/%s   %.2f AH", static_cast<int>(roundf(s.maxSpeed)), s.whPerDistance,
             s.imperial ? "MI" : "KM", s.values.ampHours - s.values.ampHoursCharged);
    text(buf, w_ / 2, 108, 2, TC_DATUM, kAmberDim);
}

void Dashboard::drawGForce(const DashboardState &s) {
    char buf[32];
    const bool live = s.connected;
    text("G-FORCE", w_ / 2, 4, 2, TC_DATUM, kAmber);

    smooth(gAnim_, live ? s.accelG : 0.0f, 10.0f);
    snprintf(buf, sizeof(buf), "%.2f", fabsf(gAnim_));
    text(buf, 62, 30, 7, TC_DATUM, live ? kAmber : kGrey, live);
    snprintf(buf, sizeof(buf), "PEAK %.2f G", s.peakG);
    text(buf, 62, 94, 2, TC_DATUM, kAmberDim);

    // Crosshair: the dot moves up when accelerating, down when braking.
    const int cx = 180, cy = 72, r = 32;
    ring(cx, cy, r - 1.5f, r + 0.5f, 0.0f, 360.0f, kBlue, true);
    spr_.drawFastHLine(cx - r + 6, cy, 2 * r - 12, blend(kBg, kBlue, 0.5f));
    spr_.drawFastVLine(cx, cy - r + 6, 2 * r - 12, blend(kBg, kBlue, 0.5f));
    const float g = gAnim_ < -1.0f ? -1.0f : (gAnim_ > 1.0f ? 1.0f : gAnim_);
    dot(cx, cy - static_cast<int>(roundf(g * (r - 6))), 4, live ? kTeal : kGrey);
}

void Dashboard::drawBattery(const DashboardState &s) {
    char buf[40];
    const bool live = s.connected;
    text("BATTERY", 10, 8, 2, TL_DATUM, kAmber);
    snprintf(buf, sizeof(buf), "%d S", s.settings.cells);
    box(178, 4, 48, 20, kAmber, false);
    text(buf, 202, 6, 2, TC_DATUM, kAmber);

    snprintf(buf, sizeof(buf), "Pack: %.1f V", s.batteryVoltage);
    text(buf, 10, 34, 2, TL_DATUM, live ? kAmber : kGrey);
    snprintf(buf, sizeof(buf), "Cell: %.3f V", s.cellVoltage);
    text(buf, 10, 52, 2, TL_DATUM, live ? kAmber : kGrey);
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.batteryPercent)));
    text(buf, 10, 72, 4, TL_DATUM, live ? cellColor(s.cellVoltage) : kGrey);

    text("GREEN   >= 3.75V", 10, 104, 1, TL_DATUM, kGreen, false);
    text("YELLOW  3.45-3.74V", 10, 114, 1, TL_DATUM, kYellow, false);
    text("RED     < 3.45V", 10, 124, 1, TL_DATUM, kRed, false);

    // Vertical level bar on the right.
    smooth(batteryAnim_, live ? s.batteryPercent / 100.0f : 0.0f, 6.0f);
    const int bx = 214, by = 32, bw = 10, bh = 96;
    spr_.fillRoundRect(bx, by, bw, bh, 4, kGrey);
    const int fillH = static_cast<int>(roundf(bh * batteryAnim_));
    if (fillH >= bw) {
        const uint16_t c = live ? cellColor(s.cellVoltage) : kGrey;
        spr_.fillRoundRect(bx, by + bh - fillH, bw, fillH, 4, c);
        if (glowEnabled_) mask_.fillRoundRect(bx, by + bh - fillH, bw, fillH, 4, c);
    }
}

void Dashboard::drawSettings(const DashboardState &s) {
    char buf[32];
    const int pages = settingsPageCount();
    const int page = s.settingsIndex / kSettingsPerPage;
    snprintf(buf, sizeof(buf), "SETTINGS %d/%d", page + 1, pages);
    text(buf, w_ / 2, 8, 2, TC_DATUM, kAmber);

    // Button hints, like the touch buttons on the reference design.
    box(6, 4, 22, 18, kAmber, false);
    text(">", 17, 5, 2, TC_DATUM, kAmber);
    box(w_ - 28, 4, 22, 18, kAmber, false);
    text("X", w_ - 17, 5, 2, TC_DATUM, kAmber);

    for (int row = 0; row < kSettingsPerPage; row++) {
        const int index = page * kSettingsPerPage + row;
        if (index >= kSettingItemCount) break;
        const SettingItem &it = kSettingItems[index];
        const bool selected = index == s.settingsIndex;
        const int y = 30 + row * 23;
        text(it.name, 14, y + 3, 2, TL_DATUM, selected ? kAmber : kAmberDim, selected);

        settingFormat(s.settings, index, buf, sizeof(buf));
        if (it.kind == SettingKind::Toggle) {
            // Pill toggle: filled when on, knob at the right.
            const bool on = s.settings.*(it.flag);
            const int tx = 184, tw = 40, th = 16;
            if (on) {
                pill(tx, y + 3, tw, th, selected ? kAmber : kAmberDim);
                spr_.fillCircle(tx + tw - 8, y + 3 + th / 2, 5, kBg);
            } else {
                spr_.drawRoundRect(tx, y + 3, tw, th, th / 2, selected ? kAmber : kAmberDim);
                dot(tx + 8, y + 3 + th / 2, 5, selected ? kAmber : kAmberDim);
            }
        } else {
            box(166, y, 60, 22, selected ? kAmber : kAmberDim, selected, selected);
            text(buf, 196, y + 3, 2, TC_DATUM, selected ? kBg : kAmber, false);
        }
    }

    // Scroll indicator
    const int sx = w_ - 4, sy = 30, sh = 92;
    spr_.drawFastVLine(sx, sy, sh, kGrey);
    const int thumbH = sh / pages;
    spr_.fillRect(sx - 1, sy + thumbH * page, 3, thumbH, kAmber);

    text("LEFT: CHANGE   RIGHT: NEXT   HOLD: EXIT", w_ / 2, 125, 1, TC_DATUM, kGrey, false);
}
