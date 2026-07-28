// ---------------------------------------------------------------------------
//  sim_main.cpp - browser entry point
// ---------------------------------------------------------------------------
//
//  Runs the real App against a synthetic capture source.  The runtime calls
//  setup() once and loop() at 60 Hz, which maps straight onto the firmware's
//  own structure, so what the browser shows is the shipping UI code.
//
#include "app.h"

#include "shim/M5Unified.h"

m5::M5Unified M5;
SimSerial Serial;

// ---------------------------------------------------------------------------
//  Touch: the runtime exposes a single point; derive press/release edges here
//  so the firmware's gesture handling sees what it expects.
// ---------------------------------------------------------------------------
void m5::Touch_Class::update() {
    int32_t x = 0, y = 0;
    const bool now = m5gfx_simulator_get_touch(&x, &y) != 0;
    _d._was_pressed = now && !_prev;
    _d._was_released = !now && _prev;
    _d._is_pressed = now;
    if (now) {
        _d.x = static_cast<int16_t>(x);
        _d.y = static_cast<int16_t>(y);
    }
    // Report one contact while held, and one more frame on release so
    // wasReleased() is observable.
    _count = (now || _d._was_released) ? 1 : 0;
    _prev = now;
}

static App g_app;

extern "C" void setup(void) {
    M5.begin();
    g_app.begin();

    // Point each decoder at the channels the synthetic scene actually uses, so
    // picking a protocol in the Decode panel shows something immediately.
    DecoderConfig& dec = g_app.decoderConfig();
    dec.i2c.sclChannel = 0;
    dec.i2c.sdaChannel = 1;
    dec.uart.channel = 2;
    dec.uart.autoBaud = true;
    dec.spi.clkChannel = 4;
    dec.spi.mosiChannel = 5;
    dec.spi.misoChannel = 6;
    dec.spi.csChannel = 7;

    // Trigger on the I2C start condition so the first sweep lands somewhere
    // meaningful instead of at an arbitrary sample.
    TriggerConfig& trig = g_app.triggerConfig();
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) trig.cond[ch] = TrigCond::Ignore;
    trig.cond[1] = TrigCond::Falling;
    trig.posPercent = 10;

    g_app.simSingleShot();
}

extern "C" void loop(void) {
    g_app.loop();
}
