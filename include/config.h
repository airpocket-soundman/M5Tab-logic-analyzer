// ---------------------------------------------------------------------------
//  config.h - Board / build time configuration for the M5Tab5 Logic Analyzer
// ---------------------------------------------------------------------------
//
//  M5Stack Tab5 M5-Bus (2x15, 30 pins) reference
//  ---------------------------------------------
//    pin  1  GND             pin  2  G16  GPIO        <- CH4
//    pin  3  GND             pin  4  G17  PB_IN       <- CH5
//    pin  5  GND             pin  6  RST  EN
//    pin  7  G18  MOSI       pin  8  G45  GPIO        <- CH6 (pin 7)
//    pin  9  G19  MISO       pin 10  G52  PB_OUT      <- CH7 (pin 9)
//    pin 11  G5   SCK        pin 12  3V3              <- CH3 (pin 11)
//    pin 13  G38  RXD0       pin 14  G37  TXD0
//    pin 15  G7   PC_RX      pin 16  G6   PC_TX
//    pin 17  G31  Int SDA    pin 18  G32  Int SCL
//    pin 19  G3   GPIO       pin 20  G4   GPIO        <- CH1 / CH2
//    pin 21  G2   GPIO       pin 22  G48  GPIO        <- CH0
//    pin 23  G47  GPIO       pin 24  G35  GPIO
//    pin 25  HVIN            pin 26  G51  GPIO
//    pin 27  HVIN            pin 28  5V
//    pin 29  HVIN            pin 30  BAT
//
//  Pins 25/27/29 carry HVIN and 28/30 carry 5V and the battery: keep probe
//  leads away from them, and never feed a channel more than 3.3V.
//
//  The eight probe channels below were picked so that every pin lives in GPIO
//  bank 0 (GPIO0..31).  That lets the CPU sampling backend grab all channels
//  with a single 32 bit register read, which roughly doubles its throughput.
//
//  Caveats for the default map (change it here if it clashes with your setup):
//    * CH3 / CH6 / CH7 are the M5-Bus SPI lines (SCK / MOSI / MISO).
//    * CH5 (G17) is the M5-Bus PB_IN line.
//  Do not stack a module that drives those lines while probing.
//
#pragma once

#include <stdint.h>

// Normally injected by the build; the browser simulator does not set it.
#ifndef LA_BUILD_VERSION
#define LA_BUILD_VERSION "0.1.0"
#endif

// ---------------------------------------------------------------------------
//  Probe channels
// ---------------------------------------------------------------------------
#define LA_MAX_CHANNELS 8

// CH0 .. CH7  ->  physical GPIO number
#define LA_PIN_CH0 2
#define LA_PIN_CH1 3
#define LA_PIN_CH2 4
#define LA_PIN_CH3 5
#define LA_PIN_CH4 16
#define LA_PIN_CH5 17
#define LA_PIN_CH6 18
#define LA_PIN_CH7 19

static constexpr int8_t kChannelPin[LA_MAX_CHANNELS] = {
    LA_PIN_CH0, LA_PIN_CH1, LA_PIN_CH2, LA_PIN_CH3,
    LA_PIN_CH4, LA_PIN_CH5, LA_PIN_CH6, LA_PIN_CH7,
};

// Enable the internal pull-ups on idle probe inputs so unconnected channels
// read as a steady high instead of picking up noise.
#define LA_PROBE_PULLUP 1

// PARLIO's soft delimiter cannot express "never end a frame": its EOF length
// register is 16 bits and zero is rejected, so a frame boundary lands at least
// every 65535 samples and the DMA disturbs a sample or two each time.  A level
// delimiter does accept eof_data_len = 0, meaning "run until the enable signal
// goes inactive" - which never happens if the enable is tied active.  This pin
// supplies that permanently asserted enable.  The chip drives it itself, so
// nothing has to be wired to it; it simply must not be used for anything else.
//
// G51 is the pick because the schematic gives it a dedicated TVS
// (PESDNC2FD3V3B) rather than one of the shared ESD0524P arrays every other
// M5-Bus pin sits on.  Parking a permanently driven signal next to two probe
// channels - which G45 did, sharing array D24 with CH4 and CH5 - is the one
// thing worth avoiding here, and it costs nothing.  M5-Bus pin 26.
#define LA_PIN_VALID 51

// ---------------------------------------------------------------------------
//  microSD (Tab5 uses SDMMC 4bit).  Overridden at runtime by M5.getPin() when
//  M5Unified knows the board, these are only the fallback values.
// ---------------------------------------------------------------------------
#define LA_SD_CLK 43
#define LA_SD_CMD 44
#define LA_SD_D0  39
#define LA_SD_D1  40
#define LA_SD_D2  41
#define LA_SD_D3  42

// ---------------------------------------------------------------------------
//  Capture buffer
// ---------------------------------------------------------------------------
// One byte per sample, bit N == channel N.
#define LA_DEPTH_MIN      (64u * 1024u)
#define LA_DEPTH_DEFAULT  (2u * 1024u * 1024u)
#define LA_DEPTH_MAX      (8u * 1024u * 1024u)

// Level-of-detail pyramid: the base level summarises this many raw samples.
#define LA_LOD_BASE_SHIFT 6            // 64 samples per base entry
#define LA_LOD_MAX_LEVELS 20

// ---------------------------------------------------------------------------
//  Sampling engines
// ---------------------------------------------------------------------------
// PARLIO RX (DMA) is the fast path.  It is compiled in only when the target
// actually has the peripheral and the Arduino build exposes the driver.
#if __has_include(<driver/parlio_rx.h>)
#include "soc/soc_caps.h"
#if defined(SOC_PARLIO_SUPPORTED) && SOC_PARLIO_SUPPORTED
#define LA_HAVE_PARLIO 1
#endif
#endif
#ifndef LA_HAVE_PARLIO
#define LA_HAVE_PARLIO 0
#endif

// Rate menu offered in the UI (Hz).  The engine reports what it really achieved.
//
// The high end is deliberately aligned to what PARLIO can actually produce:
// its clock is PLL_F160M divided by an integer, so 160/8, 160/7, 160/6 ... are
// the only reachable points up there.  Asking for a round 24 MHz would silently
// land on 22.86 MHz; offering the real values instead keeps the menu honest.
static constexpr uint32_t kRateMenu[] = {
    100000u, 200000u, 500000u,
    1000000u, 2000000u, 5000000u,
    10000000u, 16000000u,      // 160/10
    20000000u,                 // 160/8
    22857142u,                 // 160/7
    26666666u,                 // 160/6  - beats a 24 MSa/s USB analyser
    32000000u,                 // 160/5
    40000000u,                 // 160/4
    53333333u,                 // 160/3
    80000000u,                 // 160/2
};
static constexpr int kRateMenuCount = sizeof(kRateMenu) / sizeof(kRateMenu[0]);

// ---------------------------------------------------------------------------
//  Display
// ---------------------------------------------------------------------------
#define LA_SCREEN_W 1280
#define LA_SCREEN_H 720
// The Tab5 panel is natively 720x1280 portrait.  Rotation 3 is the landscape
// orientation with the USB-C port and the buttons on the side you expect when
// the tablet sits on a bench; rotation 1 is the same view upside down.
#define LA_ROTATION 3

// ---------------------------------------------------------------------------
//  Export
// ---------------------------------------------------------------------------
#define LA_EXPORT_DIR "/la"
