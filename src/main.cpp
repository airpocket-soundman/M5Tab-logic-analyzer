// ---------------------------------------------------------------------------
//  M5Tab5 Logic Analyzer
//
//  8 channel digital capture, trigger, protocol decoding and export for the
//  M5Stack Tab5 (ESP32-P4).  See README.md for wiring and docs/ARCHITECTURE.md
//  for how the sampling engines work.
// ---------------------------------------------------------------------------
#include <M5Unified.h>

#include "app.h"

static App g_app;

void setup() {
    auto cfg = M5.config();
    cfg.internal_imu = false;
    cfg.internal_rtc = false;
    cfg.external_speaker = {};
    M5.begin(cfg);

    Serial.begin(115200);
    // The control API writes ~1 kB responses; give the CDC time to hand them to
    // the host instead of dropping the tail when the buffer fills.
    Serial.setTxTimeoutMs(500);

    g_app.begin();
}

void loop() {
    g_app.loop();
}
