// ---------------------------------------------------------------------------
//  trigger.h - software trigger search over a completed capture
// ---------------------------------------------------------------------------
#pragma once

#include "capture/capture_buffer.h"
#include "logic_types.h"

// Compiled form of a TriggerConfig, so the inner loop is pure bit twiddling.
struct TriggerMasks {
    uint8_t rise      = 0;   // channels that must go 0 -> 1
    uint8_t fall      = 0;   // channels that must go 1 -> 0
    uint8_t levelHigh = 0;   // channels that must read 1
    uint8_t levelLow  = 0;   // channels that must read 0

    bool hasEdge()  const { return (rise | fall) != 0; }
    bool hasLevel() const { return (levelHigh | levelLow) != 0; }
    bool any()      const { return hasEdge() || hasLevel(); }
};

TriggerMasks compileTrigger(const TriggerConfig& cfg);

// Scans [from, buf.count()) and returns the index of the first sample that
// satisfies the condition, or -1 when nothing matched.
//
// Edge conditions are OR'ed together (any one of them fires) and then AND'ed
// with every level condition, which matches how bench analysers behave: "rising
// edge on CH0 while CH1 is low".
int64_t findTrigger(const CaptureBuffer& buf, const TriggerMasks& m, uint32_t from);
