// ---------------------------------------------------------------------------
// PC simulator entry point.
//
// Runs the real firmware (setup()/loop() from src/main.cpp) against the
// Arduino and TFT_eSPI stand-ins, drives a scripted ride, presses the page
// buttons on a schedule and writes every rendered frame as a PPM image.
//
//   vesc_sim [--out DIR] [--seconds N] [--fps N] [--quiet]
//
// tools/run_sim.py wraps this and turns the frames into a GIF.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "VescValues.h"
#include "config.h"

void setup();
void loop();

namespace {

bool writePpm(const std::string &path) {
    const std::vector<uint16_t> &fb = sim::lastFrame();
    const int w = sim::frameWidth();
    const int h = sim::frameHeight();
    if (fb.empty()) return false;
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        const uint16_t c = fb[i];
        const uint8_t rgb[3] = {
            static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31),
            static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63),
            static_cast<uint8_t>((c & 0x1F) * 255 / 31),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return true;
}

// Scripted events on the virtual timeline (seconds).
struct Event {
    float at;
    const char *what;
};
const Event kScript[] = {
    {0.0f, "vesc-offline"},   // display boots before the VESC answers
    {2.0f, "vesc-online"},
    {14.0f, "next"},          // Power page
    {22.0f, "next"},          // Trip page
    {30.0f, "next"},          // System page
    {36.0f, "next"},          // back to Main
    {43.0f, "fault"},         // OVER_TEMP_FET for a few seconds
    {46.5f, "fault-clear"},
    {48.0f, "vesc-offline"},  // link lost at the end
};

}  // namespace

int main(int argc, char **argv) {
    std::string outDir = "sim_out";
    float seconds = 52.0f;
    int fps = 8;
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = static_cast<float>(atof(argv[++i]));
        else if (!strcmp(argv[i], "--fps") && i + 1 < argc) fps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) quiet = true;
        else {
            fprintf(stderr, "usage: %s [--out DIR] [--seconds N] [--fps N] [--quiet]\n", argv[0]);
            return 2;
        }
    }
    sim::setQuiet(quiet);  // main.cpp logs once a second on Serial

    sim::setMotorPoles(MOTOR_POLES);
    setup();

    const uint32_t stepMs = 5;
    const uint32_t frameEveryMs = 1000 / fps;
    const uint32_t totalMs = static_cast<uint32_t>(seconds * 1000);
    uint32_t nextFrameMs = 0;
    size_t nextEvent = 0;
    int frames = 0;
    uint32_t lastPushed = 0;

    for (uint32_t t = 0; t <= totalMs; t += stepMs) {
        while (nextEvent < sizeof(kScript) / sizeof(kScript[0]) && kScript[nextEvent].at * 1000 <= t) {
            const char *what = kScript[nextEvent++].what;
            if (!strcmp(what, "vesc-online")) sim::setVescOnline(true);
            else if (!strcmp(what, "vesc-offline")) sim::setVescOnline(false);
            else if (!strcmp(what, "next")) sim::pressButton(BUTTON_NEXT_PIN, 80);
            else if (!strcmp(what, "prev")) sim::pressButton(BUTTON_PREV_PIN, 80);
            else if (!strcmp(what, "fault")) sim::setVescFault(vesc::FAULT_OVER_TEMP_FET);
            else if (!strcmp(what, "fault-clear")) sim::setVescFault(vesc::FAULT_NONE);
        }

        loop();

        if (t >= nextFrameMs && sim::frameCounter() != lastPushed) {
            lastPushed = sim::frameCounter();
            nextFrameMs += frameEveryMs;
            char name[512];
            snprintf(name, sizeof(name), "%s/frame_%05d.ppm", outDir.c_str(), frames);
            if (!writePpm(name)) {
                fprintf(stderr, "cannot write %s (does the directory exist?)\n", name);
                return 1;
            }
            frames++;
        }
        sim::advanceMillis(stepMs);
    }
    fprintf(stderr, "wrote %d frames to %s\n", frames, outDir.c_str());
    return 0;
}
