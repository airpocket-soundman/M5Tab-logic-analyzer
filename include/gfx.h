// ---------------------------------------------------------------------------
//  gfx.h - the graphics types the UI draws on
// ---------------------------------------------------------------------------
//
//  LaGfx is whatever surface the UI can render to.  On hardware that is the
//  LovyanGFX base class, so the same drawing code binds to both the live
//  display and to an off-screen M5Canvas; in the browser simulator (sim/) the
//  shimmed M5Unified.h supplies a compatibility class instead.  Either way the
//  UI never names a concrete driver, which is what lets waveform_view.cpp and
//  the App drawing code stay byte-for-byte identical between the firmware and
//  the simulator.
//
//  LaCanvas is the off-screen buffer used to keep the plot from flickering.
//  The Tab5's MIPI-DSI panel has a single framebuffer that is scanned out
//  continuously (M5GFX configures it with num_fbs = 1), so anything drawn
//  straight to the display is visible the instant it lands - including the
//  moment between clearing a region and repainting it.  Composing off-screen
//  and pushing once removes that window entirely.
//
#pragma once

#include <type_traits>

#include <M5Unified.h>

#ifdef LA_SIMULATOR

using LaGfx = std::remove_reference<decltype(M5.Display)>::type;
using LaCanvas = LaGfx;
// The browser runtime composites its own canvas, so there is nothing to
// double buffer and the preview draws straight to the display.
#define LA_HAVE_CANVAS 0

#else

using LaGfx = lgfx::LovyanGFX;   // common base of M5GFX and M5Canvas
using LaCanvas = M5Canvas;
#define LA_HAVE_CANVAS 1

#endif
