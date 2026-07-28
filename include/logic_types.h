// ---------------------------------------------------------------------------
//  logic_types.h - shared value types for capture, analysis and UI
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "config.h"

// ---------------------------------------------------------------------------
//  Trigger
// ---------------------------------------------------------------------------
enum class TrigCond : uint8_t {
    Ignore = 0,   // channel takes no part in the trigger
    Rising,
    Falling,
    Either,
    High,         // level match, evaluated together with the other Level conds
    Low,
};

enum class TrigMode : uint8_t {
    Auto = 0,     // show the capture even when the condition never matched
    Normal,       // keep re-capturing until the condition matches
    Single,       // one capture, then stop
};

struct TriggerConfig {
    TrigMode mode = TrigMode::Auto;
    TrigCond cond[LA_MAX_CHANNELS] = {};
    uint8_t  posPercent = 25;      // how much of the window is pre-trigger
    uint32_t normalTimeoutMs = 5000;

    bool isArmed() const {
        for (int i = 0; i < LA_MAX_CHANNELS; ++i) {
            if (cond[i] != TrigCond::Ignore) return true;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
//  Capture
// ---------------------------------------------------------------------------
enum class Engine : uint8_t {
    Auto = 0,
    Parlio,       // PARLIO RX + DMA (fast)
    Cpu,          // tight GPIO polling loop (portable fallback)
};

struct CaptureConfig {
    uint32_t rateHz = 1000000;
    uint32_t depth  = LA_DEPTH_DEFAULT;   // samples
    Engine   engine = Engine::Auto;
};

enum class CaptureState : uint8_t {
    Idle = 0,
    Armed,
    Sampling,
    Searching,
    Done,
    Failed,
};

struct CaptureInfo {
    uint32_t samples      = 0;
    double   actualRateHz = 0.0;   // measured / achievable rate
    int64_t  triggerIndex = -1;    // -1 when the condition never matched
    Engine   engineUsed   = Engine::Cpu;
    uint32_t elapsedUs    = 0;
    char     note[64]     = {0};

    double secondsPerSample() const {
        return actualRateHz > 0.0 ? 1.0 / actualRateHz : 0.0;
    }
};

// ---------------------------------------------------------------------------
//  Channel presentation
// ---------------------------------------------------------------------------
struct ChannelConfig {
    bool     enabled = true;
    bool     invert  = false;
    char     name[12] = {0};
};

// ---------------------------------------------------------------------------
//  Decoder annotations
// ---------------------------------------------------------------------------
enum class AnnKind : uint8_t {
    Info = 0,     // grey   - framing / structural
    Data,         // blue   - payload byte
    Address,      // purple - bus address
    Ack,          // green  - positive acknowledge
    Nak,          // orange - negative acknowledge
    Error,        // red    - protocol violation
};

struct Annotation {
    uint32_t startSample = 0;
    uint32_t endSample   = 0;
    AnnKind  kind        = AnnKind::Info;
    uint8_t  row         = 0;      // decoder-local lane (0 = summary)
    char     text[24]    = {0};
};

// ---------------------------------------------------------------------------
//  Per channel measurements
// ---------------------------------------------------------------------------
struct ChannelStats {
    uint32_t edges       = 0;
    uint32_t risingEdges = 0;
    double   freqHz      = 0.0;
    double   dutyPercent = 0.0;
    double   minHighSec  = 0.0;
    double   minLowSec   = 0.0;
    double   highRatio   = 0.0;
    bool     valid       = false;
};

// ---------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------
const char* engineName(Engine e);
const char* trigCondName(TrigCond c);
const char* trigModeName(TrigMode m);

// "12.34 MHz", "1.500 kSa/s" ... buf must hold at least 24 bytes.
void formatHz(double hz, char* buf, size_t len);
void formatSeconds(double s, char* buf, size_t len);
void formatCount(uint64_t n, char* buf, size_t len);
