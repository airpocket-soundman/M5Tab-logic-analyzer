// ---------------------------------------------------------------------------
//  sim/shim/esp_heap_caps.h - capability-aware allocation on a flat heap
// ---------------------------------------------------------------------------
//
//  The browser has one heap, so every capability maps onto malloc.  The free
//  size reported back is a plausible constant rather than a lie about the real
//  numbers being meaningful - the Info panel is a UI element here, not a
//  diagnostic.
//
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MALLOC_CAP_EXEC     (1 << 0)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT  (1 << 12)

inline void* heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
inline void* heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
inline void  heap_caps_free(void* p) { free(p); }

inline void* heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t) {
    void* p = nullptr;
    if (alignment < sizeof(void*)) alignment = sizeof(void*);
    // aligned_alloc requires a size that is a multiple of the alignment.
    const size_t rounded = ((size + alignment - 1) / alignment) * alignment;
    p = aligned_alloc(alignment, rounded);
    return p;
}

inline void* heap_caps_aligned_calloc(size_t alignment, size_t n, size_t size, uint32_t caps) {
    void* p = heap_caps_aligned_alloc(alignment, n * size, caps);
    if (p) memset(p, 0, n * size);
    return p;
}

inline size_t heap_caps_get_free_size(uint32_t caps) {
    return (caps & MALLOC_CAP_SPIRAM) ? 29u * 1024u * 1024u : 213u * 1024u;
}
