// ---------------------------------------------------------------------------
//  sim_sampler.cpp - synthetic capture source for the browser preview
// ---------------------------------------------------------------------------
//
//  Stands in for the PARLIO and CPU backends.  It fills the same CaptureBuffer
//  the firmware uses, so everything downstream - the LOD pyramid, the trigger
//  search, the decoders, the measurements and the renderer - is the real
//  firmware code operating on real-shaped data.
//
//  The eight channels carry a mixed-signal scene chosen so each decoder has
//  something to chew on with its default channel assignment where possible:
//
//      CH0  I2C SCL          CH4  SPI SCK
//      CH1  I2C SDA          CH5  SPI MOSI
//      CH2  UART TX          CH6  SPI MISO
//      CH3  PWM              CH7  SPI CS (active low)
//
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "capture/sampler.h"

namespace {

// Chosen so that at a few MSa/s one screen width shows a complete transaction
// on every bus at once, which is what makes the documentation screenshots
// useful rather than three separate zoom levels.
constexpr uint32_t kUartBaud = 115200;
constexpr uint32_t kI2cFreq  = 400000;
constexpr uint32_t kSpiFreq  = 1000000;
constexpr uint32_t kPwmFreq  = 50000;

struct Writer {
    uint8_t* p;
    uint32_t n;
    double   rate;

    void level(int ch, double tStart, double tEnd, bool high) {
        int64_t a = static_cast<int64_t>(tStart * rate + 0.5);
        int64_t b = static_cast<int64_t>(tEnd * rate + 0.5);
        if (b <= a) b = a + 1;
        if (a < 0) a = 0;
        if (b > static_cast<int64_t>(n)) b = n;
        const uint8_t bit = static_cast<uint8_t>(1u << ch);
        for (int64_t i = a; i < b; ++i) {
            if (high) p[i] |= bit;
            else p[i] = static_cast<uint8_t>(p[i] & ~bit);
        }
    }
    double span() const { return n / rate; }
};

// 8N1, idle high, LSB first.
void genUart(Writer& w, int ch, const char* msg, double gapSec) {
    const double bit = 1.0 / kUartBaud;
    double t = gapSec;
    const double end = w.span();
    size_t i = 0;
    while (t + 10 * bit < end) {
        const uint8_t byte = static_cast<uint8_t>(msg[i]);
        if (msg[++i] == 0) i = 0;
        w.level(ch, t, t + bit, false);                 // start
        for (int b = 0; b < 8; ++b) {
            w.level(ch, t + bit * (1 + b), t + bit * (2 + b), (byte >> b) & 1);
        }
        w.level(ch, t + bit * 9, t + bit * 10, true);   // stop
        t += bit * 10 + gapSec;
    }
}

// A repeating "write 3 bytes to 0x50" transaction.
void genI2c(Writer& w, int sclCh, int sdaCh, double gapSec) {
    const double bit = 1.0 / kI2cFreq;
    const double half = bit / 2;
    const uint8_t payload[] = {0xA0, 0x50, 0x5A, 0x3C};   // addr+W, then 3 data
    double t = gapSec;

    while (t + bit * 40 < w.span()) {
        // START: SDA falls while SCL is high.
        w.level(sclCh, t, t + bit, true);
        w.level(sdaCh, t, t + half, true);
        w.level(sdaCh, t + half, t + bit, false);
        t += bit;

        for (size_t byteIdx = 0; byteIdx < sizeof(payload); ++byteIdx) {
            for (int b = 7; b >= 0; --b) {
                const bool v = (payload[byteIdx] >> b) & 1;
                w.level(sdaCh, t, t + bit, v);
                w.level(sclCh, t, t + half, false);
                w.level(sclCh, t + half, t + bit, true);
                t += bit;
            }
            // ACK from the slave: SDA pulled low on the ninth clock.
            w.level(sdaCh, t, t + bit, false);
            w.level(sclCh, t, t + half, false);
            w.level(sclCh, t + half, t + bit, true);
            t += bit;
        }

        // STOP: SDA rises while SCL is high.
        w.level(sdaCh, t, t + half, false);
        w.level(sclCh, t, t + bit, true);
        w.level(sdaCh, t + half, t + bit, true);
        t += bit + gapSec;
    }
}

// Mode 0 (CPOL=0, CPHA=0), MSB first, CS active low.
void genSpi(Writer& w, int clk, int mosi, int miso, int cs, double gapSec) {
    const double bit = 1.0 / kSpiFreq;
    const uint8_t tx[] = {0x9F, 0x00, 0x00};
    const uint8_t rx[] = {0xFF, 0xEF, 0x40};
    double t = gapSec;

    while (t + bit * 30 < w.span()) {
        w.level(cs, t, t + bit, false);                  // select
        t += bit;
        for (size_t byteIdx = 0; byteIdx < sizeof(tx); ++byteIdx) {
            for (int b = 7; b >= 0; --b) {
                w.level(mosi, t, t + bit, (tx[byteIdx] >> b) & 1);
                w.level(miso, t, t + bit, (rx[byteIdx] >> b) & 1);
                w.level(clk, t, t + bit / 2, false);
                w.level(clk, t + bit / 2, t + bit, true);
                w.level(cs, t, t + bit, false);
                t += bit;
            }
        }
        w.level(cs, t, t + bit, false);
        w.level(clk, t, t + bit, false);
        t += bit;
        w.level(cs, t, t + gapSec, true);                // deselect
        t += gapSec;
    }
}

void genPwm(Writer& w, int ch, double dutyFraction) {
    const double period = 1.0 / kPwmFreq;
    for (double t = 0; t < w.span(); t += period) {
        w.level(ch, t, t + period * dutyFraction, true);
        w.level(ch, t + period * dutyFraction, t + period, false);
    }
}

class SamplerSim final : public ISampler {
public:
    const char* name() const override { return "SIM"; }

    bool begin(const char** reason) override {
        if (reason) *reason = nullptr;
        return true;
    }

    bool configure(const CaptureConfig& cfg, CaptureBuffer&, double* achieved,
                   const char** reason) override {
        _rate = cfg.rateHz ? cfg.rateHz : 1000000;
        if (achieved) *achieved = _rate;
        if (reason) *reason = nullptr;
        return true;
    }

    uint32_t capture(CaptureBuffer& buf, uint32_t samples,
                     double* measuredRateHz) override {
        if (!buf.data()) return 0;
        if (samples > buf.capacity()) samples = buf.capacity();

        memset(buf.data(), 0xFF, samples);   // every line idles high
        Writer w{buf.data(), samples, static_cast<double>(_rate)};

        // Nudge each run so a free-running preview looks alive rather than
        // frozen, without making the picture jump about.
        const double jitter = (_seq % 7) * (0.02 / kUartBaud);
        _seq++;

        genI2c(w, 0, 1, 0.00015 + jitter);
        genUart(w, 2, "M5Tab5 LA ", 0.00005 + jitter);
        genPwm(w, 3, 0.30);
        genSpi(w, 4, 5, 6, 7, 0.00015 + jitter);

        if (measuredRateHz) *measuredRateHz = _rate;
        return samples;
    }

    void describeLast(char* out, size_t len) const override {
        snprintf(out, len, "synthetic scene (browser preview)");
    }

private:
    uint32_t _rate = 1000000;
    uint32_t _seq  = 0;
};

SamplerSim g_sim;

}  // namespace

ISampler* createCpuSampler() { return &g_sim; }
ISampler* createParlioSampler() { return nullptr; }
