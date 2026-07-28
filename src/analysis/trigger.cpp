#include "trigger.h"

TriggerMasks compileTrigger(const TriggerConfig& cfg) {
    TriggerMasks m;
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        switch (cfg.cond[ch]) {
            case TrigCond::Rising:  m.rise |= bit; break;
            case TrigCond::Falling: m.fall |= bit; break;
            case TrigCond::Either:  m.rise |= bit; m.fall |= bit; break;
            case TrigCond::High:    m.levelHigh |= bit; break;
            case TrigCond::Low:     m.levelLow  |= bit; break;
            case TrigCond::Ignore:  break;
        }
    }
    return m;
}

int64_t findTrigger(const CaptureBuffer& buf, const TriggerMasks& m, uint32_t from) {
    const uint32_t n = buf.count();
    if (!m.any() || n < 2) return -1;
    if (from < 1) from = 1;
    if (from >= n) return -1;

    const uint8_t* p = buf.data();
    const uint8_t rise = m.rise;
    const uint8_t fall = m.fall;
    const uint8_t lvlH = m.levelHigh;
    const uint8_t lvlL = m.levelLow;
    const bool edgeMode = m.hasEdge();

    uint8_t prev = p[from - 1];
    for (uint32_t i = from; i < n; ++i) {
        const uint8_t cur = p[i];
        const bool levelOk = ((cur & lvlH) == lvlH) && ((cur & lvlL) == 0);
        if (edgeMode) {
            const uint8_t edges = static_cast<uint8_t>(
                ((cur & static_cast<uint8_t>(~prev)) & rise) |
                ((static_cast<uint8_t>(~cur) & prev) & fall));
            if (edges && levelOk) return static_cast<int64_t>(i);
        } else {
            // Level-only trigger fires on the transition *into* the match so it
            // does not latch on a window that was already satisfied.
            const bool prevOk = ((prev & lvlH) == lvlH) && ((prev & lvlL) == 0);
            if (levelOk && !prevOk) return static_cast<int64_t>(i);
        }
        prev = cur;
    }
    return -1;
}
