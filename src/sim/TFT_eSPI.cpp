#include "TFT_eSPI.h"

#include <string.h>

// Font tables from TFT_eSPI (see fonts/LICENSE.txt).
extern const unsigned char widtbl_f16[96];
extern const unsigned char *const chrtbl_f16[96];
extern const unsigned char widtbl_f32[96];
extern const unsigned char *const chrtbl_f32[96];
extern const unsigned char widtbl_f64[96];
extern const unsigned char *const chrtbl_f64[96];
extern const unsigned char widtbl_f7s[96];
extern const unsigned char *const chrtbl_f7s[96];
const unsigned char *glcdFontData();

namespace {

struct FontInfo {
    const unsigned char *const *chartbl;
    const unsigned char *widthtbl;
    uint8_t height;
    uint8_t baseline;
    bool rle;
};

// Mirrors TFT_eSPI's fontdata[] for the fonts this project loads.
bool fontInfo(uint8_t font, FontInfo &out) {
    switch (font) {
        case 2: out = {chrtbl_f16, widtbl_f16, 16, 13, false}; return true;
        case 4: out = {chrtbl_f32, widtbl_f32, 26, 19, true}; return true;
        case 6: out = {chrtbl_f64, widtbl_f64, 48, 36, true}; return true;
        case 7: out = {chrtbl_f7s, widtbl_f7s, 48, 47, true}; return true;
        default: return false;
    }
}

std::vector<uint16_t> g_lastFrame;
int g_frameW = 0;
int g_frameH = 0;
uint32_t g_frameCounter = 0;

}  // namespace

namespace sim {
const std::vector<uint16_t> &lastFrame() { return g_lastFrame; }
int frameWidth() { return g_frameW; }
int frameHeight() { return g_frameH; }
uint32_t frameCounter() { return g_frameCounter; }
}  // namespace sim

void *TFT_eSprite::createSprite(int w, int h) {
    w_ = w;
    h_ = h;
    buf_.assign(static_cast<size_t>(w) * h, 0);
    resetViewport();
    return buf_.data();
}

void TFT_eSprite::setViewport(int x, int y, int w, int h, bool vpDatum) {
    // Same rules as TFT_eSPI: clip the viewport to the sprite, drawing is
    // offset by the (unclipped) datum when vpDatum is true.
    xDatum_ = vpDatum ? x : 0;
    yDatum_ = vpDatum ? y : 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > w_) w = w_ - x;
    if (y + h > h_) h = h_ - y;
    if (w < 1 || h < 1) {
        vpW_ = vpH_ = 0;  // entirely off screen: draw nothing
        return;
    }
    vpX_ = x;
    vpY_ = y;
    vpW_ = w;
    vpH_ = h;
}

void TFT_eSprite::resetViewport() {
    xDatum_ = yDatum_ = 0;
    vpX_ = vpY_ = 0;
    vpW_ = w_;
    vpH_ = h_;
}

void TFT_eSprite::fillSprite(uint16_t color) { std::fill(buf_.begin(), buf_.end(), color); }

void TFT_eSprite::pushSprite(int, int) {
    g_lastFrame = buf_;
    g_frameW = w_;
    g_frameH = h_;
    g_frameCounter++;
}

void TFT_eSprite::drawPixel(int x, int y, uint16_t color) {
    x += xDatum_;
    y += yDatum_;
    if (x < vpX_ || y < vpY_ || x >= vpX_ + vpW_ || y >= vpY_ + vpH_) return;
    buf_[static_cast<size_t>(y) * w_ + x] = color;
}

void TFT_eSprite::drawFastHLine(int x, int y, int w, uint16_t color) {
    for (int i = 0; i < w; i++) drawPixel(x + i, y, color);
}

void TFT_eSprite::drawFastVLine(int x, int y, int h, uint16_t color) {
    for (int i = 0; i < h; i++) drawPixel(x, y + i, color);
}

void TFT_eSprite::drawRect(int x, int y, int w, int h, uint16_t color) {
    drawFastHLine(x, y, w, color);
    drawFastHLine(x, y + h - 1, w, color);
    drawFastVLine(x, y, h, color);
    drawFastVLine(x + w - 1, y, h, color);
}

void TFT_eSprite::fillRect(int x, int y, int w, int h, uint16_t color) {
    for (int j = 0; j < h; j++) drawFastHLine(x, y + j, w, color);
}

// Midpoint circle, same shape as TFT_eSPI's implementation.
void TFT_eSprite::drawCircle(int x0, int y0, int r, uint16_t color) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;
    drawPixel(x0, y0 + r, color);
    drawPixel(x0, y0 - r, color);
    drawPixel(x0 + r, y0, color);
    drawPixel(x0 - r, y0, color);
    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        drawPixel(x0 + x, y0 + y, color);
        drawPixel(x0 - x, y0 + y, color);
        drawPixel(x0 + x, y0 - y, color);
        drawPixel(x0 - x, y0 - y, color);
        drawPixel(x0 + y, y0 + x, color);
        drawPixel(x0 - y, y0 + x, color);
        drawPixel(x0 + y, y0 - x, color);
        drawPixel(x0 - y, y0 - x, color);
    }
}

void TFT_eSprite::fillCircleHelper(int x0, int y0, int r, uint8_t corners, int delta, uint16_t color) {
    int f = 1 - r;
    int ddF_x = 1;
    int ddF_y = -2 * r;
    int x = 0;
    int y = r;
    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x;
        if (corners & 1) {
            drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, color);
            drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, color);
        }
        if (corners & 2) {
            drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, color);
            drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, color);
        }
    }
}

void TFT_eSprite::fillCircle(int x0, int y0, int r, uint16_t color) {
    drawFastVLine(x0, y0 - r, 2 * r + 1, color);
    fillCircleHelper(x0, y0, r, 3, 0, color);
}

int16_t TFT_eSprite::fontHeight(uint8_t font) const {
    if (font == 1) return 8;
    FontInfo fi;
    return fontInfo(font, fi) ? fi.height : 0;
}

int16_t TFT_eSprite::textWidth(const char *s, uint8_t font) const {
    if (font == 1) return static_cast<int16_t>(6 * strlen(s));
    FontInfo fi;
    if (!fontInfo(font, fi)) return 0;
    int w = 0;
    while (*s) {
        const unsigned char c = static_cast<unsigned char>(*s++);
        w += fi.widthtbl[(c > 31 && c < 128) ? c - 32 : 0];
    }
    return static_cast<int16_t>(w);
}

// Character rendering follows TFT_eSprite::drawChar for textsize 1.
int16_t TFT_eSprite::drawChar(uint16_t c, int x, int y, uint8_t font) {
    if (font == 1) {
        // Classic 5x7 GLCD font in a 6x8 cell, columns of 8 bits, LSB at top.
        if (c > 255) return 0;
        if (c > 175) c++;  // TFT_eSPI default (_cp437 == false) quirk
        const unsigned char *glyph = glcdFontData() + c * 5;
        const bool drawBg = textColor_ != textBg_;
        for (int i = 0; i < 6; i++) {
            unsigned char line = i == 5 ? 0 : glyph[i];
            for (int j = 0; j < 8; j++) {
                if (line & 1) drawPixel(x + i, y + j, textColor_);
                else if (drawBg) drawPixel(x + i, y + j, textBg_);
                line >>= 1;
            }
        }
        return 6;
    }
    FontInfo fi;
    if (!fontInfo(font, fi)) return 0;
    if (c < 32 || c > 127) return 0;
    const unsigned char *glyph = fi.chartbl[c - 32];
    const int width = fi.widthtbl[c - 32];
    const int height = fi.height;
    const bool drawBg = textColor_ != textBg_;

    if (!fi.rle) {
        // Font 2: plain bitmap rows, MSB left, (width + 6) / 8 bytes per row.
        const int bytesPerRow = (width + 6) / 8;
        for (int row = 0; row < height; row++) {
            if (drawBg) fillRect(x, y + row, width, 1, textBg_);
            for (int k = 0; k < bytesPerRow; k++) {
                const unsigned char line = glyph[bytesPerRow * row + k];
                if (!line) continue;
                for (int bit = 0; bit < 8; bit++) {
                    if (line & (0x80 >> bit)) drawPixel(x + k * 8 + bit, y + row, textColor_);
                }
            }
        }
    } else {
        // RLE: high bit set = run of foreground pixels, clear = background.
        int remaining = width * height;
        int pc = 0;
        const unsigned char *p = glyph;
        while (remaining > 0) {
            unsigned char line = *p++;
            const bool fg = line & 0x80;
            int run = (line & 0x7F) + 1;
            remaining -= run;
            while (run--) {
                const int px = x + pc % width;
                const int py = y + pc / width;
                if (fg) {
                    drawPixel(px, py, textColor_);
                } else if (drawBg) {
                    drawPixel(px, py, textBg_);
                }
                pc++;
            }
        }
    }
    return static_cast<int16_t>(width);
}

int16_t TFT_eSprite::drawString(const char *s, int x, int y, uint8_t font) {
    const int cwidth = textWidth(s, font);
    const int cheight = fontHeight(font);
    if (cheight == 0) return 0;

    switch (datum_) {
        case TC_DATUM: x -= cwidth / 2; break;
        case TR_DATUM: x -= cwidth; break;
        case ML_DATUM: y -= cheight / 2; break;
        case MC_DATUM: x -= cwidth / 2; y -= cheight / 2; break;
        case MR_DATUM: x -= cwidth; y -= cheight / 2; break;
        case BL_DATUM: y -= cheight; break;
        case BC_DATUM: x -= cwidth / 2; y -= cheight; break;
        case BR_DATUM: x -= cwidth; y -= cheight; break;
        default: break;
    }

    int sumX = 0;
    while (*s) {
        sumX += drawChar(static_cast<unsigned char>(*s++), x + sumX, y, font);
    }
    return static_cast<int16_t>(sumX);
}
