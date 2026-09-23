// ---------------------------------------------------------------------------
// TFT_eSPI stand-in for the PC simulator.
//
// Implements the subset of TFT_eSPI / TFT_eSprite that the dashboard uses,
// drawing into an in-memory RGB565 framebuffer. Text uses the original
// TFT_eSPI font tables (fonts 2, 4 and 7) and the same decoding rules, so
// the output matches the real display pixel for pixel.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#include <vector>

#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF

#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

class TFT_eSPI {
public:
    void init() {}
    void setRotation(uint8_t r) { rotation_ = r; }
    void fillScreen(uint16_t) {}
    int width() const { return (rotation_ & 1) ? 240 : 135; }
    int height() const { return (rotation_ & 1) ? 135 : 240; }

private:
    uint8_t rotation_ = 0;
};

class TFT_eSprite {
public:
    explicit TFT_eSprite(TFT_eSPI *tft) : tft_(tft) {}

    void setColorDepth(int) {}
    void *createSprite(int w, int h);
    void setTextDatum(uint8_t datum) { datum_ = datum; }
    void setTextColor(uint16_t fg, uint16_t bg) {
        textColor_ = fg;
        textBg_ = bg;
    }

    void fillSprite(uint16_t color);
    void pushSprite(int x, int y);

    void drawPixel(int x, int y, uint16_t color);
    uint16_t readPixel(int x, int y) const;
    void drawLine(int x0, int y0, int x1, int y1, uint16_t color);
    void drawFastHLine(int x, int y, int w, uint16_t color);
    void drawFastVLine(int x, int y, int h, uint16_t color);
    void drawRect(int x, int y, int w, int h, uint16_t color);
    void fillRect(int x, int y, int w, int h, uint16_t color);
    void drawCircle(int x0, int y0, int r, uint16_t color);
    void fillCircle(int x0, int y0, int r, uint16_t color);
    void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color);
    void fillRoundRect(int x, int y, int w, int h, int r, uint16_t color);

    int16_t drawString(const char *s, int x, int y, uint8_t font);

    // Viewport: subsequent drawing is offset by (x, y) and clipped to w x h.
    void setViewport(int x, int y, int w, int h, bool vpDatum = true);
    void resetViewport();
    int16_t textWidth(const char *s, uint8_t font) const;
    int16_t fontHeight(uint8_t font) const;

    int width() const { return w_; }
    int height() const { return h_; }

private:
    int16_t drawChar(uint16_t c, int x, int y, uint8_t font);
    void fillCircleHelper(int x0, int y0, int r, uint8_t corners, int delta, uint16_t color);
    void drawCircleHelper(int x0, int y0, int r, uint8_t corners, uint16_t color);

    TFT_eSPI *tft_;
    int w_ = 0;
    int h_ = 0;
    std::vector<uint16_t> buf_;
    int vpX_ = 0, vpY_ = 0, vpW_ = 0, vpH_ = 0;  // clip rect in sprite coords
    int xDatum_ = 0, yDatum_ = 0;                // drawing offset
    uint8_t datum_ = TL_DATUM;
    uint16_t textColor_ = TFT_WHITE;
    uint16_t textBg_ = TFT_BLACK;
};

// ---- Simulator access -------------------------------------------------------
namespace sim {
// The framebuffer most recently pushed to the "display" (RGB565, row major).
const std::vector<uint16_t> &lastFrame();
int frameWidth();
int frameHeight();
uint32_t frameCounter();
}  // namespace sim
