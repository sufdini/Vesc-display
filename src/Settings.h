// ---------------------------------------------------------------------------
// User settings, editable on the display and stored in ESP32 flash (NVS).
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

struct Settings {
    uint8_t brightness = 100;   // % backlight
    uint8_t cells = 10;         // battery series cell count
    uint8_t escWarn = 70;       // ESC temperature warning, C
    uint8_t motorWarn = 80;     // motor temperature warning, C
    bool imperial = false;      // mph / mi instead of km/h / km
    bool bootAnimation = true;
    bool pageAnimations = true;
    bool glow = true;           // glow effect around text

    // Returns false when nothing was stored yet (first boot).
    bool load();
    void save() const;
};

// Metadata that drives the settings screens.
enum class SettingKind : uint8_t { Number, Toggle, Units };

struct SettingItem {
    const char *name;
    SettingKind kind;
    uint8_t Settings::*number;  // for Number
    bool Settings::*flag;       // for Toggle / Units
    int minValue;
    int maxValue;
    int step;
    const char *unit;
};

constexpr int kSettingsPerPage = 4;
extern const SettingItem kSettingItems[];
extern const int kSettingItemCount;
inline int settingsPageCount() { return (kSettingItemCount + kSettingsPerPage - 1) / kSettingsPerPage; }

// Cycle the value of item `index` (wraps around).
void settingAdjust(Settings &s, int index);
// Format the current value of item `index` into buf, e.g. "100%", "ON", "km/h".
void settingFormat(const Settings &s, int index, char *buf, int bufLen);
