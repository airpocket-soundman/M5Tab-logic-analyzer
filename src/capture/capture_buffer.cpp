#include "capture_buffer.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <string.h>

static const char* TAG = "buf";

CaptureBuffer::~CaptureBuffer() { release(); }

void CaptureBuffer::release() {
    for (int i = 0; i < LA_LOD_MAX_LEVELS; ++i) {
        if (_lod[i]) {
            heap_caps_free(_lod[i]);
            _lod[i] = nullptr;
        }
        _lodCount[i] = 0;
    }
    _lodLevels = 0;
    if (_data) {
        heap_caps_free(_data);
        _data = nullptr;
    }
    _capacity = 0;
    _count = 0;
    _dmaReady = false;
}

bool CaptureBuffer::allocate(uint32_t depth, bool dmaCapable) {
    if (depth < LA_DEPTH_MIN) depth = LA_DEPTH_MIN;
    if (depth > LA_DEPTH_MAX) depth = LA_DEPTH_MAX;
    if (_data && _capacity == depth && _dmaReady == dmaCapable) {
        _count = 0;
        return true;
    }
    release();

    // 64 byte alignment keeps the buffer cache-line friendly, which matters for
    // DMA into PSRAM on the P4.
    if (dmaCapable) {
        _data = static_cast<uint8_t*>(
            heap_caps_aligned_alloc(64, depth, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
        if (_data) _dmaReady = true;
    }
    if (!_data) {
        _data = static_cast<uint8_t*>(
            heap_caps_aligned_alloc(64, depth, MALLOC_CAP_SPIRAM));
        _dmaReady = false;
    }
    if (!_data) {
        ESP_LOGE(TAG, "failed to allocate %u byte capture buffer", (unsigned)depth);
        return false;
    }
    _capacity = depth;
    _count = 0;
    ESP_LOGI(TAG, "capture buffer %u bytes at %p (dma=%d)",
             (unsigned)depth, _data, (int)_dmaReady);
    return true;
}

void CaptureBuffer::buildLod() {
    for (int i = 0; i < LA_LOD_MAX_LEVELS; ++i) {
        if (_lod[i]) { heap_caps_free(_lod[i]); _lod[i] = nullptr; }
        _lodCount[i] = 0;
    }
    _lodLevels = 0;
    if (!_data || _count == 0) return;

    const uint32_t base = 1u << LA_LOD_BASE_SHIFT;
    uint32_t entries = (_count + base - 1) / base;
    if (entries < 2) return;

    // Level 0 is folded straight out of the raw samples.
    _lod[0] = static_cast<LodEntry*>(
        heap_caps_malloc(entries * sizeof(LodEntry), MALLOC_CAP_SPIRAM));
    if (!_lod[0]) {
        ESP_LOGW(TAG, "no room for LOD level 0");
        return;
    }
    _lodCount[0] = entries;
    for (uint32_t e = 0; e < entries; ++e) {
        uint32_t s0 = e * base;
        uint32_t s1 = s0 + base;
        if (s1 > _count) s1 = _count;
        uint8_t orv = 0x00, andv = 0xFF;
        for (uint32_t s = s0; s < s1; ++s) {
            uint8_t v = _data[s];
            orv |= v;
            andv &= v;
        }
        _lod[0][e].orv = orv;
        _lod[0][e].andv = andv;
    }
    _lodLevels = 1;

    // Higher levels each fold two entries of the level below.
    while (_lodLevels < LA_LOD_MAX_LEVELS) {
        uint32_t prevN = _lodCount[_lodLevels - 1];
        if (prevN < 4) break;
        uint32_t n = (prevN + 1) / 2;
        LodEntry* dst = static_cast<LodEntry*>(
            heap_caps_malloc(n * sizeof(LodEntry), MALLOC_CAP_SPIRAM));
        if (!dst) break;
        const LodEntry* src = _lod[_lodLevels - 1];
        for (uint32_t e = 0; e < n; ++e) {
            uint32_t a = e * 2;
            uint32_t b = a + 1;
            uint8_t orv = src[a].orv;
            uint8_t andv = src[a].andv;
            if (b < prevN) { orv |= src[b].orv; andv &= src[b].andv; }
            dst[e].orv = orv;
            dst[e].andv = andv;
        }
        _lod[_lodLevels] = dst;
        _lodCount[_lodLevels] = n;
        _lodLevels++;
    }
    ESP_LOGI(TAG, "LOD built: %d levels, base stride %u", _lodLevels, (unsigned)base);
}

int CaptureBuffer::pickLod(double samplesPerColumn) const {
    if (_lodLevels == 0) return -1;
    // Choose the deepest level that still lands at least one entry per column.
    int best = -1;
    for (int lv = 0; lv < _lodLevels; ++lv) {
        if (static_cast<double>(lodStride(lv)) <= samplesPerColumn) best = lv;
        else break;
    }
    return best;
}

LodEntry CaptureBuffer::summarizeRaw(uint32_t first, uint32_t len) const {
    LodEntry r{0x00, 0xFF};
    uint32_t end = first + len;
    if (end > _count) end = _count;
    for (uint32_t s = first; s < end; ++s) {
        uint8_t v = _data[s];
        r.orv |= v;
        r.andv &= v;
    }
    return r;
}

LodEntry CaptureBuffer::summarize(int level, uint32_t first, uint32_t len) const {
    if (len == 0 || first >= _count) return LodEntry{0x00, 0xFF};
    if (first + len > _count) len = _count - first;
    if (level < 0 || level >= _lodLevels) return summarizeRaw(first, len);

    const uint32_t stride = lodStride(level);
    const LodEntry* tbl = _lod[level];
    const uint32_t n = _lodCount[level];

    uint32_t end = first + len;
    uint32_t alignedFirst = (first + stride - 1) / stride * stride;
    uint32_t alignedEnd = end / stride * stride;

    LodEntry r{0x00, 0xFF};
    if (alignedFirst >= alignedEnd) {
        return summarizeRaw(first, len);   // window narrower than one entry
    }
    if (alignedFirst > first) {
        LodEntry h = summarizeRaw(first, alignedFirst - first);
        r.orv |= h.orv;
        r.andv &= h.andv;
    }
    for (uint32_t e = alignedFirst / stride; e < alignedEnd / stride && e < n; ++e) {
        r.orv |= tbl[e].orv;
        r.andv &= tbl[e].andv;
    }
    if (end > alignedEnd) {
        LodEntry t = summarizeRaw(alignedEnd, end - alignedEnd);
        r.orv |= t.orv;
        r.andv &= t.andv;
    }
    return r;
}
