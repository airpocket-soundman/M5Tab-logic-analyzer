// ---------------------------------------------------------------------------
//  sim/shim/Arduino.h - the sliver of the Arduino core the UI code touches
// ---------------------------------------------------------------------------
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IRAM_ATTR
#define INPUT        0x01
#define INPUT_PULLUP 0x05
#define OUTPUT       0x03

// millis() is supplied by the simulator's own M5GFX.h.
inline void delay(uint32_t) {}          // the browser runtime owns the frame clock
inline void pinMode(int, int) {}
inline uint32_t getCpuFrequencyMhz(void) { return 360; }

// LEDC test generator: there is no signal to drive in the simulator, but the
// serial API code still references these.
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return false; }
inline bool ledcWrite(uint8_t, uint32_t) { return false; }
inline bool ledcDetach(uint8_t) { return false; }

// The control API is a hardware-only feature; in the browser the stream simply
// has nothing to read and everything written goes to the JS console.
class SimSerial {
public:
    void begin(unsigned long = 0) {}
    void setTxTimeoutMs(uint32_t) {}
    void flush() {}
    int available() { return 0; }
    int read() { return -1; }
    size_t print(const char* s) { return s ? fputs(s, stdout), strlen(s) : 0; }
    size_t println(const char* s) { print(s); return fputs("\n", stdout), 1; }
    size_t printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        const int n = vprintf(fmt, ap);
        va_end(ap);
        return n > 0 ? static_cast<size_t>(n) : 0;
    }
};

extern SimSerial Serial;
