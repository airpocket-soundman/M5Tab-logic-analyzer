// ---------------------------------------------------------------------------
//  sampler_cpu.cpp - portable sampling backend built on a paced polling loop
// ---------------------------------------------------------------------------
//
//  Reads the raw GPIO input registers and packs the eight probe bits into one
//  byte per sample.  The channel-to-bit shuffle is expanded at compile time
//  from the pin map in config.h, so with the default (all bank 0) map it costs
//  a single register read plus a handful of shifts.
//
//  Pacing uses the RISC-V cycle counter rather than a delay loop: we compute a
//  deadline per sample and spin until it passes.  When the requested rate is
//  faster than the loop can go, the deadline is always in the past and the loop
//  free-runs; either way the *measured* rate is what gets reported, so the time
//  axis stays honest.
//
//  Interrupts are masked in short chunks (a few ms).  Masking for the whole
//  capture would starve the tick and the watchdogs on a multi-second run, so we
//  trade a small, reported gap at each chunk boundary for a system that stays
//  alive.
//
#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_task_wdt.h>
#include <soc/gpio_reg.h>

#include "sampler.h"

namespace {

constexpr bool pinInBank1(int p) { return p >= 32; }

constexpr bool kUsesBank0 =
    !pinInBank1(LA_PIN_CH0) || !pinInBank1(LA_PIN_CH1) ||
    !pinInBank1(LA_PIN_CH2) || !pinInBank1(LA_PIN_CH3) ||
    !pinInBank1(LA_PIN_CH4) || !pinInBank1(LA_PIN_CH5) ||
    !pinInBank1(LA_PIN_CH6) || !pinInBank1(LA_PIN_CH7);

constexpr bool kUsesBank1 =
    pinInBank1(LA_PIN_CH0) || pinInBank1(LA_PIN_CH1) ||
    pinInBank1(LA_PIN_CH2) || pinInBank1(LA_PIN_CH3) ||
    pinInBank1(LA_PIN_CH4) || pinInBank1(LA_PIN_CH5) ||
    pinInBank1(LA_PIN_CH6) || pinInBank1(LA_PIN_CH7);

#define LA_BIT(ch, pin) \
    ((((pinInBank1(pin) ? r1 : r0) >> ((pin) & 31)) & 1u) << (ch))

static inline uint8_t packSample(uint32_t r0, uint32_t r1) {
    return static_cast<uint8_t>(
        LA_BIT(0, LA_PIN_CH0) | LA_BIT(1, LA_PIN_CH1) |
        LA_BIT(2, LA_PIN_CH2) | LA_BIT(3, LA_PIN_CH3) |
        LA_BIT(4, LA_PIN_CH4) | LA_BIT(5, LA_PIN_CH5) |
        LA_BIT(6, LA_PIN_CH6) | LA_BIT(7, LA_PIN_CH7));
}

portMUX_TYPE g_cpuSamplerMux = portMUX_INITIALIZER_UNLOCKED;

// Samples one chunk with interrupts masked.  Returns the cycle count consumed.
IRAM_ATTR uint32_t sampleChunk(uint8_t* dst, uint32_t n, uint32_t periodCycles) {
    uint32_t r0 = 0, r1 = 0;
    portENTER_CRITICAL(&g_cpuSamplerMux);
    const uint32_t t0 = esp_cpu_get_cycle_count();
    uint32_t deadline = t0;
    for (uint32_t i = 0; i < n; ++i) {
        if (kUsesBank0) r0 = REG_READ(GPIO_IN_REG);
        if (kUsesBank1) r1 = REG_READ(GPIO_IN1_REG);
        dst[i] = packSample(r0, r1);
        deadline += periodCycles;
        while (static_cast<int32_t>(esp_cpu_get_cycle_count() - deadline) < 0) {
            // spin until the next sample instant
        }
    }
    const uint32_t t1 = esp_cpu_get_cycle_count();
    portEXIT_CRITICAL(&g_cpuSamplerMux);
    return t1 - t0;
}

class SamplerCpu final : public ISampler {
public:
    const char* name() const override { return "CPU"; }

    bool begin(const char** reason) override {
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            const int pin = kChannelPin[ch];
#if LA_PROBE_PULLUP
            pinMode(pin, INPUT_PULLUP);
#else
            pinMode(pin, INPUT);
#endif
        }
        _cpuHz = static_cast<uint32_t>(getCpuFrequencyMhz()) * 1000000u;
        if (_cpuHz == 0) _cpuHz = 360000000u;
        if (reason) *reason = nullptr;
        return true;
    }

    bool configure(const CaptureConfig& cfg, CaptureBuffer& buf,
                   double* achievedRateHz, const char** reason) override {
        (void)buf;
        if (cfg.rateHz == 0) {
            if (reason) *reason = "rate must be > 0";
            return false;
        }
        _period = _cpuHz / cfg.rateHz;
        if (_period < 1) _period = 1;
        // Roughly what the loop costs when it never waits.  Used only to give
        // the UI a sane pre-capture estimate; the real number is measured.
        const uint32_t floorPeriod = kUsesBank1 ? 26u : 18u;
        uint32_t effective = _period < floorPeriod ? floorPeriod : _period;
        if (achievedRateHz) *achievedRateHz = static_cast<double>(_cpuHz) / effective;
        if (reason) *reason = nullptr;
        return true;
    }

    uint32_t capture(CaptureBuffer& buf, uint32_t samples,
                     double* measuredRateHz) override {
        uint8_t* dst = buf.data();
        if (!dst) return 0;
        if (samples > buf.capacity()) samples = buf.capacity();

        // Keep each interrupts-off window near 2 ms.
        uint32_t chunk = static_cast<uint32_t>(
            (static_cast<uint64_t>(_cpuHz) / 500u) / (_period ? _period : 1));
        if (chunk < 1024) chunk = 1024;
        if (chunk > samples) chunk = samples;

        uint64_t totalCycles = 0;
        uint32_t done = 0;
        while (done < samples) {
            uint32_t n = samples - done;
            if (n > chunk) n = chunk;
            totalCycles += sampleChunk(dst + done, n, _period);
            done += n;
            if (done < samples) {
                // Let the tick, the watchdogs and Wi-Fi breathe between chunks.
                esp_task_wdt_reset();
                taskYIELD();
            }
        }
        if (measuredRateHz) {
            *measuredRateHz = totalCycles > 0
                ? static_cast<double>(done) * _cpuHz / static_cast<double>(totalCycles)
                : 0.0;
        }
        return done;
    }

private:
    uint32_t _cpuHz  = 360000000u;
    uint32_t _period = 360;
};

}  // namespace

ISampler* createCpuSampler() {
    static SamplerCpu s;
    return &s;
}
