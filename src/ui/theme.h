// ---------------------------------------------------------------------------
//  theme.h - colours and layout metrics
// ---------------------------------------------------------------------------
#pragma once

#include "gfx.h"
#include "logic_types.h"

namespace theme {

// Dark instrument palette: the traces have to stay readable next to a bench
// under harsh light, so the background is near-black and every trace colour is
// picked for luminance separation rather than hue variety alone.
constexpr uint16_t kBg        = 0x0861;   // #0c0c0c
constexpr uint16_t kPanel     = 0x18E3;   // #1c1c1c
constexpr uint16_t kPanelEdge = 0x39E7;   // #3c3c3c
constexpr uint16_t kGrid      = 0x2124;   // #202020
constexpr uint16_t kGridMajor = 0x4208;   // #404040
constexpr uint16_t kText      = 0xE73C;   // #e0e0e0
constexpr uint16_t kTextDim   = 0x8410;   // #808080
constexpr uint16_t kAccent    = 0x05FF;   // cyan
constexpr uint16_t kWarn      = 0xFD20;   // orange
constexpr uint16_t kBad       = 0xF9A6;   // red
constexpr uint16_t kGood      = 0x2FE8;   // green
constexpr uint16_t kCursorA   = 0xFFE0;   // yellow
constexpr uint16_t kCursorB   = 0xFC9F;   // pink
constexpr uint16_t kTrigLine  = 0xFB00;   // amber

constexpr uint16_t kChannel[LA_MAX_CHANNELS] = {
    0x07FF,  // cyan
    0x07E0,  // green
    0xFFE0,  // yellow
    0xFD20,  // orange
    0xF81F,  // magenta
    0x9CFF,  // periwinkle
    0xFFFF,  // white
    0xFBEC,  // salmon
};

inline uint16_t annColor(AnnKind k) {
    switch (k) {
        case AnnKind::Data:    return 0x1C9F;   // blue
        case AnnKind::Address: return 0x9819;   // purple
        case AnnKind::Ack:     return 0x1D66;   // dark green
        case AnnKind::Nak:     return 0xBB00;   // dark orange
        case AnnKind::Error:   return 0xA000;   // dark red
        case AnnKind::Info:
        default:               return 0x39E7;   // grey
    }
}

// -------- layout ----------------------------------------------------------
constexpr int kTopBarH     = 52;
constexpr int kBottomBarH  = 64;
constexpr int kLabelW      = 96;
constexpr int kRulerH      = 28;
constexpr int kPanelH      = 150;   // measurement / decode list panel
constexpr int kDecodeRows  = 2;
constexpr int kDecodeRowH  = 22;

}  // namespace theme
