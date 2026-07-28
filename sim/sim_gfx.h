// ---------------------------------------------------------------------------
//  sim_gfx.h - the extra M5GFX surface the firmware UI needs
// ---------------------------------------------------------------------------
//
//  The web simulator's M5GFX compatibility class covers pixels, rectangles and
//  lines but explicitly does not emulate text, fonts or rotation.  The firmware
//  UI uses all of those, so this subclass fills the gap on top of the
//  primitives the simulator does provide.  Everything here is presentation
//  only - no firmware logic lives in the simulator.
//
#pragma once

#include <M5GFX.h>
#include <stdint.h>

struct SimFont {
    const void* glyphs;
    uint8_t     width;
    uint8_t     height;
    uint8_t     bytesPerRow;
};

namespace fonts {
extern const SimFont Font2;   //  8 x 16, matches the metric the UI assumes
extern const SimFont Font4;   // 16 x 26
}  // namespace fonts

enum class textdatum_t : uint8_t {
    top_left = 0,
    top_center,
    top_right,
    middle_left,
    middle_center,
    middle_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

class SimGfx : public M5GFX {
public:
    // The simulator canvas is already 1280x720, so rotation is a no-op that
    // exists purely so the firmware's begin() compiles unchanged.
    void setRotation(uint8_t) {}

    void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                       uint32_t color);
    void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
                       uint32_t color);
    void fillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                      int32_t x2, int32_t y2, uint32_t color);

    void setFont(const SimFont* f) { _font = f ? f : &fonts::Font2; }
    void setTextDatum(textdatum_t d) { _datum = d; }
    void setTextColor(uint32_t fg, uint32_t bg) { _fg = fg; _bg = bg; _opaque = true; }
    void setTextColor(uint32_t fg) { _fg = fg; _opaque = false; }
    void drawString(const char* text, int32_t x, int32_t y);
    int32_t textWidth(const char* text) const;

    void readRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* out);

private:
    const SimFont* _font = &fonts::Font2;
    textdatum_t    _datum = textdatum_t::top_left;
    uint32_t       _fg = 0xFFFF;
    uint32_t       _bg = 0x0000;
    bool           _opaque = true;
};
