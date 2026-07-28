#include "decoder.h"

#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  AnnotationList
// ---------------------------------------------------------------------------
AnnotationList::~AnnotationList() { release(); }

void AnnotationList::release() {
    if (_items) heap_caps_free(_items);
    _items = nullptr;
    _capacity = 0;
    _count = 0;
    _truncated = false;
}

bool AnnotationList::reserve(uint32_t maxItems) {
    if (_capacity >= maxItems) { clear(); return true; }
    release();
    _items = static_cast<Annotation*>(
        heap_caps_malloc(sizeof(Annotation) * maxItems, MALLOC_CAP_SPIRAM));
    if (!_items) return false;
    _capacity = maxItems;
    return true;
}

bool AnnotationList::add(uint32_t startSample, uint32_t endSample, AnnKind kind,
                         uint8_t row, const char* text) {
    if (_count >= _capacity) { _truncated = true; return false; }
    Annotation& a = _items[_count++];
    a.startSample = startSample;
    a.endSample = endSample;
    a.kind = kind;
    a.row = row;
    snprintf(a.text, sizeof(a.text), "%s", text ? text : "");
    return true;
}

uint32_t AnnotationList::lowerBound(uint32_t sample) const {
    uint32_t lo = 0, hi = _count;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (_items[mid].endSample < sample) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ---------------------------------------------------------------------------
//  Shared helpers
// ---------------------------------------------------------------------------
namespace {

inline bool lvl(const uint8_t* p, uint32_t i, int ch) {
    return (p[i] >> ch) & 1u;
}

// Clamp a range to what was actually captured.
bool clampRange(const CaptureBuffer& buf, uint32_t& first, uint32_t& len) {
    const uint32_t n = buf.count();
    if (n == 0 || first >= n) return false;
    if (first + len > n) len = n - first;
    return len >= 2;
}

}  // namespace

uint32_t narrowestPulse(const CaptureBuffer& buf, int channel,
                        uint32_t first, uint32_t len) {
    if (channel < 0 || channel >= LA_MAX_CHANNELS) return 0;
    if (!clampRange(buf, first, len)) return 0;
    const uint8_t* p = buf.data();
    const uint32_t end = first + len;

    uint32_t best = 0xFFFFFFFFu;
    int64_t lastEdge = -1;
    bool prev = lvl(p, first, channel);
    for (uint32_t i = first + 1; i < end; ++i) {
        const bool cur = lvl(p, i, channel);
        if (cur != prev) {
            if (lastEdge >= 0) {
                const uint32_t w = static_cast<uint32_t>(i - lastEdge);
                if (w < best) best = w;
            }
            lastEdge = i;
            prev = cur;
        }
    }
    return best == 0xFFFFFFFFu ? 0 : best;
}

// ---------------------------------------------------------------------------
//  UART
// ---------------------------------------------------------------------------
//  Standard asynchronous framing: idle high, a start bit pulls the line low,
//  then data bits sampled at the middle of each bit cell, optional parity, then
//  the stop bit(s).  A stop bit that is not high is reported as a framing error
//  and the decoder resynchronises on the next falling edge instead of blindly
//  continuing, which keeps one bad byte from corrupting the whole stream.
// ---------------------------------------------------------------------------
void decodeUart(const CaptureBuffer& buf, const CaptureInfo& info,
                const UartOptions& opt, uint32_t first, uint32_t len,
                AnnotationList& out) {
    if (opt.channel < 0 || opt.channel >= LA_MAX_CHANNELS) return;
    if (!clampRange(buf, first, len)) return;

    const uint8_t* p = buf.data();
    const int ch = opt.channel;
    const bool inv = opt.invert;
    const uint32_t end = first + len;

    double samplesPerBit = 0.0;
    if (opt.autoBaud) {
        const uint32_t np = narrowestPulse(buf, ch, first, len);
        samplesPerBit = np > 0 ? static_cast<double>(np) : 0.0;
    }
    if (samplesPerBit < 1.5) {
        if (info.actualRateHz <= 0 || opt.baud == 0) return;
        samplesPerBit = info.actualRateHz / opt.baud;
    }
    if (samplesPerBit < 1.5) return;   // signal is faster than the sample rate

    const uint32_t nData = opt.dataBits >= 5 && opt.dataBits <= 9 ? opt.dataBits : 8;
    const uint32_t nStop = opt.stopBits == 2 ? 2 : 1;
    const bool hasParity = (opt.parity == 'E' || opt.parity == 'O');

    char txt[24];
    if (opt.autoBaud) {
        const double baud = info.actualRateHz > 0 ? info.actualRateHz / samplesPerBit : 0;
        snprintf(txt, sizeof(txt), "auto %.0f bd", baud);
        out.add(first, first, AnnKind::Info, 0, txt);
    }

    auto bitAt = [&](double pos) -> bool {
        const uint32_t i = static_cast<uint32_t>(pos + 0.5);
        if (i >= end) return true;
        const bool v = lvl(p, i, ch);
        return inv ? !v : v;
    };

    uint32_t i = first + 1;
    bool prev = inv ? !lvl(p, first, ch) : lvl(p, first, ch);
    while (i < end) {
        const bool cur = inv ? !lvl(p, i, ch) : lvl(p, i, ch);
        if (!(prev && !cur)) { prev = cur; ++i; continue; }   // want a falling edge

        const double startEdge = static_cast<double>(i);
        // Confirm the start bit really is low at its centre.
        if (bitAt(startEdge + samplesPerBit * 0.5)) { prev = cur; ++i; continue; }

        const double frameBits = 1.0 + nData + (hasParity ? 1 : 0) + nStop;
        const uint32_t frameEnd =
            static_cast<uint32_t>(startEdge + samplesPerBit * frameBits + 0.5);
        if (frameEnd >= end) break;

        out.add(static_cast<uint32_t>(startEdge),
                static_cast<uint32_t>(startEdge + samplesPerBit), AnnKind::Info, 1, "S");

        uint32_t value = 0;
        uint32_t ones = 0;
        for (uint32_t b = 0; b < nData; ++b) {
            const double c = startEdge + samplesPerBit * (1.5 + b);
            const bool v = bitAt(c);
            if (v) ones++;
            if (opt.lsbFirst) value |= (v ? 1u : 0u) << b;
            else value = (value << 1) | (v ? 1u : 0u);
        }

        bool parityBad = false;
        if (hasParity) {
            const double c = startEdge + samplesPerBit * (1.5 + nData);
            const bool pbit = bitAt(c);
            const bool wantEven = (opt.parity == 'E');
            const bool total = ((ones + (pbit ? 1u : 0u)) & 1u) != 0;
            parityBad = wantEven ? total : !total;
            const double ps = startEdge + samplesPerBit * (1.0 + nData);
            out.add(static_cast<uint32_t>(ps),
                    static_cast<uint32_t>(ps + samplesPerBit),
                    parityBad ? AnnKind::Error : AnnKind::Ack, 1,
                    parityBad ? "P!" : "P");
        }

        const double stopStart =
            startEdge + samplesPerBit * (1.0 + nData + (hasParity ? 1 : 0));
        const bool stopOk = bitAt(stopStart + samplesPerBit * 0.5);
        out.add(static_cast<uint32_t>(stopStart),
                static_cast<uint32_t>(stopStart + samplesPerBit * nStop),
                stopOk ? AnnKind::Info : AnnKind::Error, 1, stopOk ? "T" : "T!");

        const uint32_t dataStart = static_cast<uint32_t>(startEdge + samplesPerBit);
        const uint32_t dataEnd = static_cast<uint32_t>(stopStart);
        if (value >= 0x20 && value < 0x7F) {
            snprintf(txt, sizeof(txt), "%02X '%c'", (unsigned)value, (char)value);
        } else {
            snprintf(txt, sizeof(txt), "%02X", (unsigned)value);
        }
        out.add(dataStart, dataEnd,
                (parityBad || !stopOk) ? AnnKind::Error : AnnKind::Data, 0, txt);

        if (stopOk) {
            i = frameEnd;
            prev = true;                       // the line is idle again
        } else {
            // Framing error: resynchronise on the next edge rather than trust
            // the bit clock we just used.
            i = static_cast<uint32_t>(startEdge + samplesPerBit) + 1;
            prev = inv ? !lvl(p, i - 1, ch) : lvl(p, i - 1, ch);
        }
    }
}

// ---------------------------------------------------------------------------
//  I2C
// ---------------------------------------------------------------------------
void decodeI2c(const CaptureBuffer& buf, const CaptureInfo& info,
               const I2cOptions& opt, uint32_t first, uint32_t len,
               AnnotationList& out) {
    (void)info;
    if (opt.sclChannel < 0 || opt.sclChannel >= LA_MAX_CHANNELS) return;
    if (opt.sdaChannel < 0 || opt.sdaChannel >= LA_MAX_CHANNELS) return;
    if (opt.sclChannel == opt.sdaChannel) return;
    if (!clampRange(buf, first, len)) return;

    const uint8_t* p = buf.data();
    const int scl = opt.sclChannel;
    const int sda = opt.sdaChannel;
    const uint32_t end = first + len;

    enum class St { Idle, Address, Data };
    St st = St::Idle;

    uint32_t shift = 0;
    int bitCount = 0;
    uint32_t byteStart = 0;
    bool readMode = false;
    char txt[24];

    bool prevScl = lvl(p, first, scl);
    bool prevSda = lvl(p, first, sda);

    for (uint32_t i = first + 1; i < end; ++i) {
        const bool curScl = lvl(p, i, scl);
        const bool curSda = lvl(p, i, sda);

        // START / STOP are SDA transitions while SCL is high.
        if (curScl && prevScl && curSda != prevSda) {
            if (!curSda) {                       // falling SDA -> START
                out.add(i, i + 1, AnnKind::Info, 0,
                        st == St::Idle ? "START" : "Sr");
                st = St::Address;
                shift = 0;
                bitCount = 0;
                byteStart = i;
            } else {                             // rising SDA -> STOP
                out.add(i, i + 1, AnnKind::Info, 0, "STOP");
                st = St::Idle;
                bitCount = 0;
            }
            prevScl = curScl;
            prevSda = curSda;
            continue;
        }

        // Data is valid on the rising edge of SCL.
        if (curScl && !prevScl && st != St::Idle) {
            if (bitCount == 0) byteStart = i;
            if (bitCount < 8) {
                shift = (shift << 1) | (curSda ? 1u : 0u);
                bitCount++;
            } else {
                // The ninth clock carries the acknowledge from the receiver.
                const bool nak = curSda;
                if (st == St::Address) {
                    readMode = (shift & 1u) != 0;
                    snprintf(txt, sizeof(txt), "%02X %s",
                             (unsigned)(shift >> 1), readMode ? "R" : "W");
                    out.add(byteStart, i, AnnKind::Address, 0, txt);
                    st = St::Data;
                } else {
                    snprintf(txt, sizeof(txt), "%02X", (unsigned)(shift & 0xFF));
                    out.add(byteStart, i, AnnKind::Data, 0, txt);
                }
                if (opt.showAcks) {
                    out.add(i, i + 1, nak ? AnnKind::Nak : AnnKind::Ack, 1,
                            nak ? "N" : "A");
                }
                shift = 0;
                bitCount = 0;
            }
        }

        prevScl = curScl;
        prevSda = curSda;
    }
}

// ---------------------------------------------------------------------------
//  SPI
// ---------------------------------------------------------------------------
void decodeSpi(const CaptureBuffer& buf, const CaptureInfo& info,
               const SpiOptions& opt, uint32_t first, uint32_t len,
               AnnotationList& out) {
    (void)info;
    if (opt.clkChannel < 0 || opt.clkChannel >= LA_MAX_CHANNELS) return;
    if (opt.mosiChannel < 0 && opt.misoChannel < 0) return;
    if (!clampRange(buf, first, len)) return;

    const uint8_t* p = buf.data();
    const int clk = opt.clkChannel;
    const uint32_t end = first + len;
    const uint32_t bits = (opt.wordBits >= 4 && opt.wordBits <= 16) ? opt.wordBits : 8;

    // CPOL/CPHA determine which clock edge latches the data.  With CPHA=0 the
    // sample edge is the leading edge, with CPHA=1 it is the trailing one.
    const bool sampleOnRising = (opt.cpol == opt.cpha);

    uint32_t mosi = 0, miso = 0;
    int bitCount = 0;
    uint32_t wordStart = 0;
    bool selected = (opt.csChannel < 0);
    char txt[24];

    bool prevClk = lvl(p, first, clk);
    bool prevCs = opt.csChannel >= 0 ? lvl(p, first, opt.csChannel) : false;

    for (uint32_t i = first + 1; i < end; ++i) {
        if (opt.csChannel >= 0) {
            const bool curCs = lvl(p, i, opt.csChannel);
            if (curCs != prevCs) {
                const bool nowSelected = opt.csActiveLow ? !curCs : curCs;
                if (nowSelected) {
                    out.add(i, i + 1, AnnKind::Info, 0, "CS\\");
                    bitCount = 0;
                    mosi = miso = 0;
                } else {
                    out.add(i, i + 1, AnnKind::Info, 0, "CS/");
                    bitCount = 0;
                }
                selected = nowSelected;
                prevCs = curCs;
            }
        }

        const bool curClk = lvl(p, i, clk);
        if (selected && curClk != prevClk) {
            const bool isSampleEdge = sampleOnRising ? (curClk && !prevClk)
                                                     : (!curClk && prevClk);
            if (isSampleEdge) {
                if (bitCount == 0) wordStart = i;
                const uint32_t mo = (opt.mosiChannel >= 0 && lvl(p, i, opt.mosiChannel)) ? 1u : 0u;
                const uint32_t mi = (opt.misoChannel >= 0 && lvl(p, i, opt.misoChannel)) ? 1u : 0u;
                if (opt.msbFirst) {
                    mosi = (mosi << 1) | mo;
                    miso = (miso << 1) | mi;
                } else {
                    mosi |= mo << bitCount;
                    miso |= mi << bitCount;
                }
                if (++bitCount == static_cast<int>(bits)) {
                    if (opt.mosiChannel >= 0) {
                        snprintf(txt, sizeof(txt), "%0*X",
                                 (int)((bits + 3) / 4), (unsigned)mosi);
                        out.add(wordStart, i, AnnKind::Data, 0, txt);
                    }
                    if (opt.misoChannel >= 0) {
                        snprintf(txt, sizeof(txt), "%0*X",
                                 (int)((bits + 3) / 4), (unsigned)miso);
                        out.add(wordStart, i, AnnKind::Data, 1, txt);
                    }
                    bitCount = 0;
                    mosi = miso = 0;
                }
            }
        }
        prevClk = curClk;
    }
}

// ---------------------------------------------------------------------------
//  Dispatch
// ---------------------------------------------------------------------------
bool runDecoder(const CaptureBuffer& buf, const CaptureInfo& info,
                const DecoderConfig& cfg, uint32_t first, uint32_t len,
                AnnotationList& out, const char** err) {
    if (err) *err = nullptr;
    out.clear();
    switch (cfg.kind) {
        case DecoderKind::None:
            return true;
        case DecoderKind::Uart:
            if (cfg.uart.channel < 0) { if (err) *err = "UART: pick a channel"; return false; }
            decodeUart(buf, info, cfg.uart, first, len, out);
            return true;
        case DecoderKind::I2c:
            if (cfg.i2c.sclChannel == cfg.i2c.sdaChannel) {
                if (err) *err = "I2C: SCL and SDA must differ";
                return false;
            }
            decodeI2c(buf, info, cfg.i2c, first, len, out);
            return true;
        case DecoderKind::Spi:
            if (cfg.spi.mosiChannel < 0 && cfg.spi.misoChannel < 0) {
                if (err) *err = "SPI: enable MOSI or MISO";
                return false;
            }
            decodeSpi(buf, info, cfg.spi, first, len, out);
            return true;
    }
    return true;
}
