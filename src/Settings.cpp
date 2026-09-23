#include "Settings.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace {
const char *kNamespace = "vescdisp";
}

const SettingItem kSettingItems[] = {
    {"Brightness", SettingKind::Number, &Settings::brightness, nullptr, 10, 100, 10, "%"},
    {"Battery Series", SettingKind::Number, &Settings::cells, nullptr, 3, 24, 1, " S"},
    {"ESC Warning", SettingKind::Number, &Settings::escWarn, nullptr, 40, 110, 5, " C"},
    {"Motor Warning", SettingKind::Number, &Settings::motorWarn, nullptr, 40, 140, 5, " C"},
    {"Units", SettingKind::Units, nullptr, &Settings::imperial, 0, 0, 0, ""},
    {"Boot Animation", SettingKind::Toggle, nullptr, &Settings::bootAnimation, 0, 0, 0, ""},
    {"Page Animations", SettingKind::Toggle, nullptr, &Settings::pageAnimations, 0, 0, 0, ""},
    {"Glow Effect", SettingKind::Toggle, nullptr, &Settings::glow, 0, 0, 0, ""},
};
const int kSettingItemCount = sizeof(kSettingItems) / sizeof(kSettingItems[0]);

bool Settings::load() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, true)) return false;
    const bool stored = prefs.getUChar("ver", 0) != 0;
    brightness = prefs.getUChar("bright", brightness);
    cells = prefs.getUChar("cells", cells);
    escWarn = prefs.getUChar("escwarn", escWarn);
    motorWarn = prefs.getUChar("motwarn", motorWarn);
    imperial = prefs.getBool("imperial", imperial);
    bootAnimation = prefs.getBool("bootanim", bootAnimation);
    pageAnimations = prefs.getBool("pageanim", pageAnimations);
    glow = prefs.getBool("glow", glow);
    prefs.end();
    return stored;
}

void Settings::save() const {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return;
    prefs.putUChar("ver", 1);
    prefs.putUChar("bright", brightness);
    prefs.putUChar("cells", cells);
    prefs.putUChar("escwarn", escWarn);
    prefs.putUChar("motwarn", motorWarn);
    prefs.putBool("imperial", imperial);
    prefs.putBool("bootanim", bootAnimation);
    prefs.putBool("pageanim", pageAnimations);
    prefs.putBool("glow", glow);
    prefs.end();
}

void settingAdjust(Settings &s, int index) {
    if (index < 0 || index >= kSettingItemCount) return;
    const SettingItem &it = kSettingItems[index];
    if (it.kind == SettingKind::Number) {
        int v = s.*(it.number) + it.step;
        if (v > it.maxValue) v = it.minValue;
        s.*(it.number) = static_cast<uint8_t>(v);
    } else {
        s.*(it.flag) = !(s.*(it.flag));
    }
}

void settingFormat(const Settings &s, int index, char *buf, int bufLen) {
    if (index < 0 || index >= kSettingItemCount) {
        buf[0] = 0;
        return;
    }
    const SettingItem &it = kSettingItems[index];
    switch (it.kind) {
        case SettingKind::Number: snprintf(buf, bufLen, "%d%s", s.*(it.number), it.unit); break;
        case SettingKind::Toggle: snprintf(buf, bufLen, "%s", (s.*(it.flag)) ? "ON" : "OFF"); break;
        case SettingKind::Units: snprintf(buf, bufLen, "%s", (s.*(it.flag)) ? "mph" : "km/h"); break;
    }
}
