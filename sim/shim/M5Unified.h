// ---------------------------------------------------------------------------
//  sim/shim/M5Unified.h - browser stand-in for the M5Unified board object
// ---------------------------------------------------------------------------
//
//  Only what the firmware UI actually touches: a display, touch state and a
//  couple of identification calls.  Everything hardware-flavoured is either a
//  no-op or reports "not present", which is honest - the simulator is a UI
//  preview, not a board emulator.
//
#pragma once

#include <stdint.h>

#include "../sim_gfx.h"
#include "Arduino.h"

namespace m5 {

enum class board_t : uint8_t {
    board_unknown = 0,
    board_M5Tab5 = 22,
};

enum pin_name_t : uint8_t {
    sd_mmc_clk = 0, sd_mmc_cmd, sd_mmc_d0, sd_mmc_d1, sd_mmc_d2, sd_mmc_d3,
    pin_name_max,
};

struct touch_detail_t {
    int16_t x = 0, y = 0;
    bool _was_pressed = false;
    bool _is_pressed = false;
    bool _was_released = false;

    bool wasPressed() const { return _was_pressed; }
    bool isPressed() const { return _is_pressed; }
    bool wasReleased() const { return _was_released; }
};

class Touch_Class {
public:
    void update();
    int getCount() const { return _count; }
    touch_detail_t getDetail(int i = 0) const { return i == 0 ? _d : touch_detail_t{}; }

private:
    touch_detail_t _d;
    bool _prev = false;
    int  _count = 0;
};

struct config_t {
    bool internal_imu = true;
    bool internal_rtc = true;
    struct { int dummy = 0; } external_speaker;
};

class M5Unified {
public:
    SimGfx      Display;
    Touch_Class Touch;

    config_t config() { return config_t(); }
    void begin(const config_t& = config_t()) {}
    void update() { Touch.update(); }

    board_t getBoard() const { return board_t::board_M5Tab5; }
    static int8_t getPin(pin_name_t) { return -1; }
    static bool hasSD() { return false; }
};

}  // namespace m5

using m5::board_t;

extern m5::M5Unified M5;
