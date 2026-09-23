#include "Dashboard.h"

#include <math.h>
#include <stdio.h>

namespace {
constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kFg = TFT_WHITE;
constexpr uint16_t kDim = 0x7BEF;      // mid grey
constexpr uint16_t kGood = 0x07E0;     // green
constexpr uint16_t kWarn = 0xFD20;     // orange
constexpr uint16_t kBad = 0xF800;      // red
constexpr uint16_t kAccent = 0x05FF;   // cyan
constexpr uint16_t kRegen = 0x3D9F;    // light blue

constexpr int kStatusBarH = 18;

const char *speedUnit(bool imperial) { return imperial ? "mph" : "km/h"; }
const char *distUnit(bool imperial) { return imperial ? "mi" : "km"; }
}  // namespace

void Dashboard::begin(uint8_t rotation) {
    tft_.init();
    tft_.setRotation(rotation);
    tft_.fillScreen(kBg);
    w_ = tft_.width();
    h_ = tft_.height();
    spr_.setColorDepth(16);
    spr_.createSprite(w_, h_);
    spr_.setTextDatum(TL_DATUM);
}

void Dashboard::showSplash(const char *line1, const char *line2) {
    spr_.fillSprite(kBg);
    spr_.setTextColor(kAccent, kBg);
    spr_.setTextDatum(MC_DATUM);
    spr_.drawString(line1, w_ / 2, h_ / 2 - 14, 4);
    spr_.setTextColor(kDim, kBg);
    spr_.drawString(line2, w_ / 2, h_ / 2 + 14, 2);
    spr_.setTextDatum(TL_DATUM);
    spr_.pushSprite(0, 0);
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

void Dashboard::drawBattery(int x, int y, int w, int h, float percent, bool connected) {
    const uint16_t color = connected ? batteryColor(percent) : kDim;
    spr_.drawRect(x, y, w, h, kFg);
    spr_.fillRect(x + w, y + h / 4, 2, h / 2, kFg);  // terminal nub
    const int innerW = w - 4;
    const int fillW = static_cast<int>(roundf(innerW * constrain(percent, 0.0f, 100.0f) / 100.0f));
    if (fillW > 0) {
        spr_.fillRect(x + 2, y + 2, fillW, h - 4, color);
    }
}

void Dashboard::drawBar(int x, int y, int w, int h, float fraction, uint16_t color) {
    spr_.drawRect(x, y, w, h, kDim);
    const int fillW = static_cast<int>(roundf((w - 2) * constrain(fraction, 0.0f, 1.0f)));
    if (fillW > 0) {
        spr_.fillRect(x + 1, y + 1, fillW, h - 2, color);
    }
}

void Dashboard::drawLabelValue(int x, int y, const char *label, const char *value, uint16_t color) {
    spr_.setTextColor(kDim, kBg);
    spr_.drawString(label, x, y, 2);
    spr_.setTextColor(color, kBg);
    spr_.drawString(value, x, y + 14, 4);
}

void Dashboard::drawStatusBar(const DashboardState &s) {
    // Connection indicator + fault text on the left
    const uint16_t dot = s.connected ? kGood : (s.everConnected ? kBad : kDim);
    spr_.fillCircle(6, kStatusBarH / 2, 4, dot);

    char buf[32];
    spr_.setTextDatum(TL_DATUM);
    if (!s.connected) {
        spr_.setTextColor(s.everConnected ? kBad : kDim, kBg);
        spr_.drawString(s.everConnected ? "LINK LOST" : "NO VESC", 16, 2, 2);
    } else if (s.values.fault != vesc::FAULT_NONE) {
        spr_.setTextColor(kBad, kBg);
        snprintf(buf, sizeof(buf), "FAULT %s", vesc::faultName(s.values.fault));
        spr_.drawString(buf, 16, 2, 2);
    } else {
        spr_.setTextColor(kDim, kBg);
        snprintf(buf, sizeof(buf), "%.1fV  %.2fV/c", s.batteryVoltage, s.cellVoltage);
        spr_.drawString(buf, 16, 2, 2);
    }

    // Battery on the right
    spr_.setTextDatum(TR_DATUM);
    spr_.setTextColor(s.connected ? batteryColor(s.batteryPercent) : kDim, kBg);
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.batteryPercent)));
    spr_.drawString(buf, w_ - 36, 2, 2);
    drawBattery(w_ - 32, 3, 28, 12, s.batteryPercent, s.connected);
    spr_.setTextDatum(TL_DATUM);

    spr_.drawFastHLine(0, kStatusBarH, w_, kDim);
}

void Dashboard::drawPageDots(const DashboardState &s) {
    const int n = static_cast<int>(Page::Count);
    const int spacing = 8;
    const int x0 = w_ / 2 - (n - 1) * spacing / 2;
    for (int i = 0; i < n; i++) {
        const bool active = i == static_cast<int>(s.page);
        if (active) {
            spr_.fillCircle(x0 + i * spacing, h_ - 4, 2, kFg);
        } else {
            spr_.drawCircle(x0 + i * spacing, h_ - 4, 2, kDim);
        }
    }
}

void Dashboard::drawPageMain(const DashboardState &s) {
    char buf[32];
    const uint16_t numColor = s.connected ? kFg : kDim;

    // Big speed, 7-segment style font (48 px tall)
    const float speed = fabsf(s.speed);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(roundf(speed)));
    spr_.setTextColor(numColor, kBg);
    spr_.setTextDatum(TR_DATUM);
    spr_.drawString(buf, 150, kStatusBarH + 10, 7);
    spr_.setTextDatum(TL_DATUM);

    // Units and direction next to it
    spr_.setTextColor(kDim, kBg);
    spr_.drawString(speedUnit(s.imperial), 158, kStatusBarH + 12, 2);
    if (s.speed < -0.5f) {
        spr_.setTextColor(kWarn, kBg);
        spr_.drawString("REV", 158, kStatusBarH + 28, 2);
    }
    snprintf(buf, sizeof(buf), "max %d", static_cast<int>(roundf(s.maxSpeed)));
    spr_.setTextColor(kDim, kBg);
    spr_.drawString(buf, 158, kStatusBarH + 46, 2);

    // Power bar: right of centre = drive, left of centre = regen.
    const int barY = kStatusBarH + 66;
    const int barX = 8;
    const int barW = w_ - 16;
    const int barH = 8;
    spr_.drawRect(barX, barY, barW, barH, kDim);
    const int mid = barX + barW / 2;
    const float maxW = 2000.0f;  // full scale watts for the bar
    const float frac = constrain(s.powerW / maxW, -1.0f, 1.0f);
    const int len = static_cast<int>(roundf(fabsf(frac) * (barW / 2 - 1)));
    if (len > 0) {
        if (frac >= 0) {
            spr_.fillRect(mid, barY + 1, len, barH - 2, kAccent);
        } else {
            spr_.fillRect(mid - len, barY + 1, len, barH - 2, kRegen);
        }
    }
    spr_.drawFastVLine(mid, barY - 2, barH + 4, kFg);

    // Bottom row: power, current, motor temp, FET temp
    const int rowY = h_ - 34;
    const int colW = w_ / 4;
    const uint16_t powerColor = !s.connected ? kDim : (s.powerW < -5.0f ? kRegen : kFg);

    snprintf(buf, sizeof(buf), "%dW", static_cast<int>(roundf(s.powerW)));
    drawLabelValue(4, rowY, "POWER", buf, powerColor);

    snprintf(buf, sizeof(buf), "%.1fA", s.values.currentInput);
    drawLabelValue(4 + colW, rowY, "BATT", buf, s.connected ? kFg : kDim);

    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempMotor)));
    drawLabelValue(4 + colW * 2, rowY, "MOTOR", buf, s.connected ? tempColor(s.values.tempMotor, 80, 100) : kDim);

    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempFet)));
    drawLabelValue(4 + colW * 3, rowY, "ESC", buf, s.connected ? tempColor(s.values.tempFet, 70, 85) : kDim);
}

void Dashboard::drawPagePower(const DashboardState &s) {
    char buf[32];
    const uint16_t c = s.connected ? kFg : kDim;
    const int y0 = kStatusBarH + 6;
    const int y1 = kStatusBarH + 50;
    const int colW = w_ / 3;

    snprintf(buf, sizeof(buf), "%.1fA", s.values.currentMotor);
    drawLabelValue(4, y0, "MOTOR A", buf, c);
    snprintf(buf, sizeof(buf), "%.1fA", s.values.currentInput);
    drawLabelValue(4 + colW, y0, "BATT A", buf, s.values.currentInput < -0.5f ? kRegen : c);
    snprintf(buf, sizeof(buf), "%dW", static_cast<int>(roundf(s.powerW)));
    drawLabelValue(4 + colW * 2, y0, "POWER", buf, s.powerW < -5.0f ? kRegen : c);

    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(roundf(s.values.dutyCycle * 100.0f)));
    drawLabelValue(4, y1, "DUTY", buf, fabsf(s.values.dutyCycle) > 0.9f ? kWarn : c);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(s.values.erpm));
    drawLabelValue(4 + colW, y1, "ERPM", buf, c);
    snprintf(buf, sizeof(buf), "%.1fV", s.batteryVoltage);
    drawLabelValue(4 + colW * 2, y1, "VOLTAGE", buf, c);

    // Duty cycle bar along the bottom
    drawBar(8, h_ - 22, w_ - 16, 10, fabsf(s.values.dutyCycle), fabsf(s.values.dutyCycle) > 0.9f ? kWarn : kAccent);
}

void Dashboard::drawPageTrip(const DashboardState &s) {
    char buf[32];
    const uint16_t c = s.connected ? kFg : kDim;
    const int y0 = kStatusBarH + 6;
    const int y1 = kStatusBarH + 50;
    const int colW = w_ / 3;

    snprintf(buf, sizeof(buf), "%.2f%s", s.tripDistance, distUnit(s.imperial));
    drawLabelValue(4, y0, "TRIP", buf, c);
    snprintf(buf, sizeof(buf), "%.2fAh", s.values.ampHours - s.values.ampHoursCharged);
    drawLabelValue(4 + colW, y0, "USED", buf, c);
    snprintf(buf, sizeof(buf), "%.1fWh", s.values.wattHours - s.values.wattHoursCharged);
    drawLabelValue(4 + colW * 2, y0, "ENERGY", buf, c);

    snprintf(buf, sizeof(buf), "%.1f", s.whPerDistance);
    drawLabelValue(4, y1, s.imperial ? "WH/MI" : "WH/KM", buf, c);
    snprintf(buf, sizeof(buf), "%.2fAh", s.values.ampHoursCharged);
    drawLabelValue(4 + colW, y1, "REGEN", buf, s.values.ampHoursCharged > 0.001f ? kRegen : c);
    snprintf(buf, sizeof(buf), "%d%s", static_cast<int>(roundf(s.maxSpeed)), speedUnit(s.imperial));
    drawLabelValue(4 + colW * 2, y1, "MAX", buf, c);

    // Trip counters reset when the VESC reboots; say so.
    spr_.setTextColor(kDim, kBg);
    spr_.drawString("Counters reset with the VESC", 4, h_ - 22, 2);
}

void Dashboard::drawPageSystem(const DashboardState &s) {
    char buf[40];
    const uint16_t c = s.connected ? kFg : kDim;
    const int y0 = kStatusBarH + 6;
    const int y1 = kStatusBarH + 50;
    const int colW = w_ / 3;

    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempFet)));
    drawLabelValue(4, y0, "ESC TEMP", buf, s.connected ? tempColor(s.values.tempFet, 70, 85) : kDim);
    snprintf(buf, sizeof(buf), "%d`", static_cast<int>(roundf(s.values.tempMotor)));
    drawLabelValue(4 + colW, y0, "MOTOR TEMP", buf, s.connected ? tempColor(s.values.tempMotor, 80, 100) : kDim);
    snprintf(buf, sizeof(buf), "%d", s.values.controllerId);
    drawLabelValue(4 + colW * 2, y0, "VESC ID", buf, c);

    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.packetsOk));
    drawLabelValue(4, y1, "PACKETS", buf, c);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.crcErrors));
    drawLabelValue(4 + colW, y1, "CRC ERR", buf, s.crcErrors > 0 ? kWarn : c);
    snprintf(buf, sizeof(buf), "%lu:%02lu", static_cast<unsigned long>(s.uptimeS / 60),
             static_cast<unsigned long>(s.uptimeS % 60));
    drawLabelValue(4 + colW * 2, y1, "UPTIME", buf, kFg);

    // Last fault seen since boot (the live fault is in the status bar)
    if (s.lastFault != vesc::FAULT_NONE) {
        spr_.setTextColor(kWarn, kBg);
        snprintf(buf, sizeof(buf), "Last fault: %s", vesc::faultName(s.lastFault));
    } else {
        spr_.setTextColor(kDim, kBg);
        snprintf(buf, sizeof(buf), "No faults since boot");
    }
    spr_.drawString(buf, 4, h_ - 22, 2);
}

void Dashboard::render(const DashboardState &s) {
    spr_.fillSprite(kBg);
    drawStatusBar(s);
    switch (s.page) {
        case Page::Main: drawPageMain(s); break;
        case Page::Power: drawPagePower(s); break;
        case Page::Trip: drawPageTrip(s); break;
        case Page::System: drawPageSystem(s); break;
        default: break;
    }
    drawPageDots(s);
    spr_.pushSprite(0, 0);
}
