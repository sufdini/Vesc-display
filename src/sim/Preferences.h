// Preferences (ESP32 NVS) stand-in for the PC simulator: in-memory only.
#pragma once

#include <stdint.h>

#include <map>
#include <string>

class Preferences {
public:
    bool begin(const char *, bool = false) { return true; }
    void end() {}
    uint8_t getUChar(const char *key, uint8_t def = 0) { return static_cast<uint8_t>(get(key, def)); }
    size_t putUChar(const char *key, uint8_t v) { store()[key] = v; return 1; }
    bool getBool(const char *key, bool def = false) { return get(key, def ? 1 : 0) != 0; }
    size_t putBool(const char *key, bool v) { store()[key] = v ? 1 : 0; return 1; }

private:
    static std::map<std::string, int> &store() {
        static std::map<std::string, int> s;
        return s;
    }
    int get(const char *key, int def) {
        auto it = store().find(key);
        return it == store().end() ? def : it->second;
    }
};
