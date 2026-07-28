// ---------------------------------------------------------------------------
//  capture_buffer.h - PSRAM sample store plus a level-of-detail pyramid
// ---------------------------------------------------------------------------
//
//  Samples are stored one byte each, bit N holding channel N.  Rendering a
//  multi-megasample capture into ~1150 pixel columns would otherwise mean
//  walking the whole buffer on every frame, so the buffer also keeps a small
//  pyramid of (OR, AND) summaries.  Because the summary is exact - OR tells us
//  "was ever high", AND tells us "was always high" - zoomed out views still
//  show single sample glitches instead of silently dropping them.
//
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "config.h"

struct LodEntry {
    uint8_t orv;    // bitwise OR  of every sample covered by the entry
    uint8_t andv;   // bitwise AND of every sample covered by the entry
};

class CaptureBuffer {
public:
    CaptureBuffer() = default;
    ~CaptureBuffer();

    // Allocates (or grows) the sample store.  dmaCapable asks the allocator for
    // memory the GDMA engine can write into; it silently falls back to plain
    // PSRAM, in which case dmaReady() reports false and the PARLIO engine is
    // not usable.
    bool allocate(uint32_t depth, bool dmaCapable);
    void release();

    uint8_t*       data()       { return _data; }
    const uint8_t* data() const { return _data; }
    uint32_t capacity() const   { return _capacity; }
    uint32_t count() const      { return _count; }
    bool     dmaReady() const   { return _dmaReady; }

    void setCount(uint32_t n)   { _count = n < _capacity ? n : _capacity; }

    uint8_t at(uint32_t i) const { return _data[i]; }

    // Rebuild the pyramid.  Call once after a capture completes.
    void buildLod();
    bool lodReady() const { return _lodLevels > 0; }

    // Number of raw samples summarised by one entry of `level`.
    uint32_t lodStride(int level) const {
        return 1u << (LA_LOD_BASE_SHIFT + level);
    }
    int lodLevels() const { return _lodLevels; }

    // Deepest level whose stride still fits inside `samplesPerColumn`.
    // Returns -1 when the raw samples should be scanned directly.
    int pickLod(double samplesPerColumn) const;

    // Exact (OR, AND) over [first, first+len).  Uses the pyramid when it can
    // and falls back to a raw scan for the ragged edges.
    LodEntry summarize(int level, uint32_t first, uint32_t len) const;

private:
    LodEntry summarizeRaw(uint32_t first, uint32_t len) const;

    uint8_t*  _data      = nullptr;
    uint32_t  _capacity  = 0;
    uint32_t  _count     = 0;
    bool      _dmaReady  = false;

    LodEntry* _lod[LA_LOD_MAX_LEVELS] = {nullptr};
    uint32_t  _lodCount[LA_LOD_MAX_LEVELS] = {0};
    int       _lodLevels = 0;
};
