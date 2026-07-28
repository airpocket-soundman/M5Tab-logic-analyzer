#include "sim_gfx.h"

#include <string.h>

#include "sim_font_data.h"

namespace fonts {
const SimFont Font2 = {kSimFontSmall, 8, 16, 1};
const SimFont Font4 = {kSimFontLarge, 16, 26, 2};
}  // namespace fonts

// ---------------------------------------------------------------------------
//  Shapes
// ---------------------------------------------------------------------------
namespace {

// Quarter-circle offsets, used by both the filled and outlined rounded rect so
// the two agree pixel for pixel.
inline int arcDx(int r, int dy) {
    // Largest dx with dx^2 + dy^2 <= r^2.
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
    return dx;
}

}  // namespace

void SimGfx::fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                           uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { fillRect(x, y, w, h, color); return; }

    fillRect(x, y + r, w, h - 2 * r, color);
    for (int32_t i = 0; i < r; ++i) {
        const int dy = r - i;                       // distance from the centre
        const int dx = r - arcDx(r, dy);
        fillRect(x + dx, y + i, w - 2 * dx, 1, color);
        fillRect(x + dx, y + h - 1 - i, w - 2 * dx, 1, color);
    }
}

void SimGfx::drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                           uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0) { drawRect(x, y, w, h, color); return; }

    drawFastHLine(x + r, y, w - 2 * r, color);
    drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
    drawFastVLine(x, y + r, h - 2 * r, color);
    drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
    for (int32_t i = 0; i < r; ++i) {
        const int dy = r - i;
        const int dx = r - arcDx(r, dy);
        drawPixel(x + dx, y + i, color);
        drawPixel(x + w - 1 - dx, y + i, color);
        drawPixel(x + dx, y + h - 1 - i, color);
        drawPixel(x + w - 1 - dx, y + h - 1 - i, color);
    }
}

void SimGfx::fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                          int32_t x2, int32_t y2, uint32_t color) {
    // Sort by y so the sweep is monotonic.
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y1 > y2) { int32_t t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
    if (y0 > y1) { int32_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
    if (y2 == y0) {
        int32_t a = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int32_t b = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        drawFastHLine(a, y0, b - a + 1, color);
        return;
    }
    for (int32_t y = y0; y <= y2; ++y) {
        // Long edge x0->x2, short edge x0->x1 then x1->x2.
        const int32_t xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
        int32_t xb;
        if (y < y1) {
            xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        } else {
            xb = (y2 == y1) ? x2 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        }
        if (xa <= xb) drawFastHLine(xa, y, xb - xa + 1, color);
        else drawFastHLine(xb, y, xa - xb + 1, color);
    }
}

// ---------------------------------------------------------------------------
//  Text
// ---------------------------------------------------------------------------
int32_t SimGfx::textWidth(const char* text) const {
    if (!text) return 0;
    return static_cast<int32_t>(strlen(text)) * _font->width;
}

void SimGfx::drawString(const char* text, int32_t x, int32_t y) {
    if (!text || !*text) return;
    const SimFont* f = _font;
    const int32_t w = textWidth(text);
    const int32_t h = f->height;

    // The datum names the anchor point, so shift the top-left origin to suit.
    switch (_datum) {
        case textdatum_t::top_center:
        case textdatum_t::middle_center:
        case textdatum_t::bottom_center: x -= w / 2; break;
        case textdatum_t::top_right:
        case textdatum_t::middle_right:
        case textdatum_t::bottom_right:  x -= w; break;
        default: break;
    }
    switch (_datum) {
        case textdatum_t::middle_left:
        case textdatum_t::middle_center:
        case textdatum_t::middle_right:  y -= h / 2; break;
        case textdatum_t::bottom_left:
        case textdatum_t::bottom_center:
        case textdatum_t::bottom_right:  y -= h; break;
        default: break;
    }

    if (_opaque) fillRect(x, y, w, h, _bg);

    int32_t cx = x;
    for (const char* p = text; *p; ++p, cx += f->width) {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c < SIM_FONT_FIRST || c > SIM_FONT_LAST) c = '?';
        const int gi = c - SIM_FONT_FIRST;
        for (int row = 0; row < f->height; ++row) {
            uint32_t bits;
            if (f->bytesPerRow == 1) {
                bits = static_cast<const uint8_t(*)[16]>(f->glyphs)[gi][row];
            } else {
                bits = static_cast<const uint16_t(*)[26]>(f->glyphs)[gi][row];
            }
            if (!bits) continue;
            for (int col = 0; col < f->width; ++col) {
                if (bits & (1u << (f->width - 1 - col))) {
                    drawPixel(cx + col, y + row, _fg);
                }
            }
        }
    }
}

void SimGfx::readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* out) {
    if (!out) return;
    for (int32_t j = 0; j < h; ++j) {
        for (int32_t i = 0; i < w; ++i) {
            out[j * w + i] = readPixel(x + i, y + j);
        }
    }
}
