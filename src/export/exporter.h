// ---------------------------------------------------------------------------
//  exporter.h - microSD mount plus CSV / VCD / screenshot writers
// ---------------------------------------------------------------------------
#pragma once

#include "capture/capture_buffer.h"
#include "decode/decoder.h"
#include "logic_types.h"

class Exporter {
public:
    // Mounts the card (SDMMC 4 bit).  Safe to call repeatedly; returns the
    // current mount state.
    bool mount();
    void unmount();
    bool mounted() const { return _mounted; }

    const char* lastError() const { return _err; }
    const char* lastPath() const { return _lastPath; }

    // Per-sample CSV of [first, first+len).  Meant for a screen-sized window;
    // a whole multi-megasample buffer would take minutes and tens of MB, so the
    // caller is expected to pass the visible range.
    bool writeCsv(const CaptureBuffer& buf, const CaptureInfo& info,
                  const ChannelConfig channels[LA_MAX_CHANNELS],
                  uint32_t first, uint32_t len);

    // Value-change dump of the whole capture, readable by PulseView/GTKWave.
    // Only transitions are written, so even an 8 MSa capture stays small.
    bool writeVcd(const CaptureBuffer& buf, const CaptureInfo& info,
                  const ChannelConfig channels[LA_MAX_CHANNELS]);

    // Decoder output as tab separated text.
    bool writeAnnotations(const AnnotationList& anns, const CaptureInfo& info,
                          const char* decoderName);

    // 24 bit BMP of the current framebuffer contents.
    bool writeScreenshot();

private:
    bool ensureDir();
    bool nextPath(const char* prefix, const char* ext);

    bool  _mounted = false;
    char  _err[64] = {0};
    char  _lastPath[48] = {0};
    uint32_t _seq = 0;
};
