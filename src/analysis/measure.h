// ---------------------------------------------------------------------------
//  measure.h - per channel timing statistics over a captured range
// ---------------------------------------------------------------------------
#pragma once

#include "capture/capture_buffer.h"
#include "logic_types.h"

// Walks [first, first+len) once and fills stats[0..LA_MAX_CHANNELS-1].
//
// freqHz comes from the mean period between rising edges rather than from the
// edge count, so a burst that starts or ends mid-window does not skew it.
// dutyPercent is the mean high time over those same whole periods; highRatio is
// the raw fraction of samples that were high, which stays meaningful for
// aperiodic lines where freq/duty do not.
void measureChannels(const CaptureBuffer& buf, double secondsPerSample,
                     uint32_t first, uint32_t len,
                     ChannelStats stats[LA_MAX_CHANNELS]);
