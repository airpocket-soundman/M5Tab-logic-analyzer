// ---------------------------------------------------------------------------
//  decoder.h - protocol decoders (UART / I2C / SPI)
// ---------------------------------------------------------------------------
//
//  Every decoder turns a captured range into a flat, sample-ordered list of
//  Annotations.  Keeping one shared list type means the waveform view and the
//  text panel do not need to know which protocol produced what, and the list can
//  live in PSRAM where a few thousand annotations cost nothing.
//
#pragma once

#include "capture/capture_buffer.h"
#include "logic_types.h"

class AnnotationList {
public:
    ~AnnotationList();

    bool reserve(uint32_t maxItems);
    void clear() { _count = 0; _truncated = false; }
    void release();

    bool add(uint32_t startSample, uint32_t endSample, AnnKind kind,
             uint8_t row, const char* text);

    uint32_t size() const { return _count; }
    bool truncated() const { return _truncated; }
    const Annotation& operator[](uint32_t i) const { return _items[i]; }

    // Index of the first annotation whose endSample is >= `sample`.
    uint32_t lowerBound(uint32_t sample) const;

private:
    Annotation* _items     = nullptr;
    uint32_t    _capacity  = 0;
    uint32_t    _count     = 0;
    bool        _truncated = false;
};

// ---------------------------------------------------------------------------
//  Decoder selection and options
// ---------------------------------------------------------------------------
enum class DecoderKind : uint8_t { None = 0, Uart, I2c, Spi };

struct UartOptions {
    int8_t   channel  = 0;
    uint32_t baud     = 115200;
    bool     autoBaud = true;    // derive the baud from the narrowest pulse
    uint8_t  dataBits = 8;       // 5..9
    char     parity   = 'N';     // 'N', 'E', 'O'
    uint8_t  stopBits = 1;       // 1 or 2
    bool     invert   = false;   // idle-low (e.g. after an inverting probe)
    bool     lsbFirst = true;
};

struct I2cOptions {
    int8_t sclChannel = 0;
    int8_t sdaChannel = 1;
    bool   showAcks   = true;
};

struct SpiOptions {
    int8_t clkChannel  = 0;
    int8_t mosiChannel = 1;
    int8_t misoChannel = -1;   // -1 disables the MISO lane
    int8_t csChannel   = -1;   // -1 means "always selected"
    bool   cpol        = false;
    bool   cpha        = false;
    bool   msbFirst    = true;
    bool   csActiveLow = true;
    uint8_t wordBits   = 8;    // 4..16
};

struct DecoderConfig {
    DecoderKind kind = DecoderKind::None;
    UartOptions uart;
    I2cOptions  i2c;
    SpiOptions  spi;
};

// Run `cfg` over [first, first+len) and append to `out` (which is cleared).
// Returns false when the options are inconsistent; `err` then describes why.
bool runDecoder(const CaptureBuffer& buf, const CaptureInfo& info,
                const DecoderConfig& cfg, uint32_t first, uint32_t len,
                AnnotationList& out, const char** err);

// Individual entry points, useful for tests and for the auto-baud helper.
void decodeUart(const CaptureBuffer& buf, const CaptureInfo& info,
                const UartOptions& opt, uint32_t first, uint32_t len,
                AnnotationList& out);
void decodeI2c(const CaptureBuffer& buf, const CaptureInfo& info,
               const I2cOptions& opt, uint32_t first, uint32_t len,
               AnnotationList& out);
void decodeSpi(const CaptureBuffer& buf, const CaptureInfo& info,
               const SpiOptions& opt, uint32_t first, uint32_t len,
               AnnotationList& out);

// Narrowest pulse on `channel` in samples, or 0 when the line never toggles.
uint32_t narrowestPulse(const CaptureBuffer& buf, int channel,
                        uint32_t first, uint32_t len);
