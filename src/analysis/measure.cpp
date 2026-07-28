#include "measure.h"

#include <string.h>

namespace {

struct Acc {
    uint32_t highSamples = 0;
    uint32_t edges       = 0;
    uint32_t risingEdges = 0;

    // First and last rising edge, used for the mean period.
    int64_t  firstRise = -1;
    int64_t  lastRise  = -1;

    // High time accumulated between complete rising->rising periods.
    uint64_t highInPeriods = 0;
    int64_t  pendingRise   = -1;   // rising edge of the period being measured
    int64_t  pendingFall   = -1;   // falling edge inside that period

    uint32_t minHigh = 0xFFFFFFFFu;
    uint32_t minLow  = 0xFFFFFFFFu;
    int64_t  lastEdge = -1;
};

}  // namespace

void measureChannels(const CaptureBuffer& buf, double secondsPerSample,
                     uint32_t first, uint32_t len,
                     ChannelStats stats[LA_MAX_CHANNELS]) {
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) stats[ch] = ChannelStats();

    const uint32_t n = buf.count();
    if (n == 0 || first >= n) return;
    if (first + len > n) len = n - first;
    if (len < 2) return;

    const uint8_t* p = buf.data();
    Acc acc[LA_MAX_CHANNELS];

    uint8_t prev = p[first];
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (prev & (1u << ch)) acc[ch].highSamples++;
    }

    const uint32_t end = first + len;
    for (uint32_t i = first + 1; i < end; ++i) {
        const uint8_t cur = p[i];
        const uint8_t changed = static_cast<uint8_t>(cur ^ prev);
        if (changed) {
            uint8_t bits = changed;
            while (bits) {
                const int ch = __builtin_ctz(bits);
                bits = static_cast<uint8_t>(bits & (bits - 1));
                Acc& a = acc[ch];
                a.edges++;
                const bool rising = (cur >> ch) & 1u;

                if (a.lastEdge >= 0) {
                    const uint32_t width = static_cast<uint32_t>(i - a.lastEdge);
                    // The level that just ended was the opposite of the new one.
                    if (rising) { if (width < a.minLow)  a.minLow  = width; }
                    else        { if (width < a.minHigh) a.minHigh = width; }
                }
                a.lastEdge = i;

                if (rising) {
                    a.risingEdges++;
                    if (a.firstRise < 0) a.firstRise = i;
                    if (a.pendingRise >= 0 && a.pendingFall > a.pendingRise) {
                        a.highInPeriods += static_cast<uint64_t>(a.pendingFall - a.pendingRise);
                    }
                    a.pendingRise = i;
                    a.pendingFall = -1;
                    a.lastRise = i;
                } else {
                    if (a.pendingRise >= 0 && a.pendingFall < 0) a.pendingFall = i;
                }
            }
        }
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            if (cur & (1u << ch)) acc[ch].highSamples++;
        }
        prev = cur;
    }

    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        const Acc& a = acc[ch];
        ChannelStats& s = stats[ch];
        s.edges       = a.edges;
        s.risingEdges = a.risingEdges;
        s.highRatio   = static_cast<double>(a.highSamples) / len;
        s.minHighSec  = (a.minHigh != 0xFFFFFFFFu) ? a.minHigh * secondsPerSample : 0.0;
        s.minLowSec   = (a.minLow  != 0xFFFFFFFFu) ? a.minLow  * secondsPerSample : 0.0;
        s.valid       = a.edges > 0;

        // Two or more rising edges means at least one complete period.
        if (a.firstRise >= 0 && a.lastRise > a.firstRise && a.risingEdges >= 2) {
            const uint32_t periods = a.risingEdges - 1;
            const double meanPeriodSamples =
                static_cast<double>(a.lastRise - a.firstRise) / periods;
            if (meanPeriodSamples > 0 && secondsPerSample > 0) {
                s.freqHz = 1.0 / (meanPeriodSamples * secondsPerSample);
                // highInPeriods only accumulated complete rise->fall->rise runs.
                const double meanHigh =
                    static_cast<double>(a.highInPeriods) / periods;
                s.dutyPercent = 100.0 * meanHigh / meanPeriodSamples;
            }
        }
    }
}
