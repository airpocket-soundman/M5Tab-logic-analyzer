// ---------------------------------------------------------------------------
//  gfx.h - the graphics type the UI draws on
// ---------------------------------------------------------------------------
//
//  On hardware this is M5GFX.  In the browser simulator (sim/) the shimmed
//  M5Unified.h hands back a compatibility class instead, so the UI code is
//  written against whatever M5.Display happens to be rather than naming a
//  concrete type.  That keeps waveform_view.cpp and the App drawing code
//  byte-for-byte identical between the firmware and the simulator - the
//  screenshots in the documentation are produced by the shipping code, not by
//  a mock-up of it.
//
#pragma once

#include <type_traits>

#include <M5Unified.h>

using LaGfx = std::remove_reference<decltype(M5.Display)>::type;
