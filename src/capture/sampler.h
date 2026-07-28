// ---------------------------------------------------------------------------
//  sampler.h - sampling backend interface
// ---------------------------------------------------------------------------
//
//  Two backends implement this:
//
//    SamplerParlio  ESP32-P4 PARLIO RX peripheral streaming straight into the
//                   PSRAM capture buffer over GDMA.  Gapless, no CPU involved,
//                   good for the tens-of-MSa/s range.
//
//    SamplerCpu     A pacing loop that reads the GPIO input registers.  Always
//                   available, works everywhere, but the rate ceiling is set by
//                   how fast the loop runs and it cannot be preempted-free
//                   forever, so the achieved rate is measured and reported.
//
//  Both are one-shot: fill the buffer front to back, then hand it over.  The
//  trigger is resolved afterwards by scanning the captured samples, which keeps
//  the fast path free of per-sample comparisons.  See docs/ARCHITECTURE.md for
//  why that trade-off was made.
//
#pragma once

#include <stdint.h>

#include "capture_buffer.h"
#include "logic_types.h"

class ISampler {
public:
    virtual ~ISampler() = default;

    virtual const char* name() const = 0;

    // One-off hardware setup.  Returns false when the backend is unusable on
    // this build/board, in which case `reason` explains why.
    virtual bool begin(const char** reason) = 0;

    // Prepare the peripheral for `cfg`.  Must be called whenever the rate or
    // depth changes.  Returns the rate the hardware will actually run at.
    virtual bool configure(const CaptureConfig& cfg, CaptureBuffer& buf,
                           double* achievedRateHz, const char** reason) = 0;

    // Blocking single-shot capture of `samples` samples into buf.
    // Returns the number of samples actually written.
    virtual uint32_t capture(CaptureBuffer& buf, uint32_t samples,
                             double* measuredRateHz) = 0;

    // One line about how the last capture went (buffer mode, DMA headroom,
    // ...).  Surfaced in the Info panel and the `status` API response.
    virtual void describeLast(char* buf, size_t len) const {
        if (len) buf[0] = 0;
    }

    // Largest capture this backend can take without a real-time constraint.
    // 0 means "no such limit".
    virtual uint32_t losslessDepth() const { return 0; }

    virtual void end() {}
};

ISampler* createParlioSampler();   // nullptr when not compiled in
ISampler* createCpuSampler();
