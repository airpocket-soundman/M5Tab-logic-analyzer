// ---------------------------------------------------------------------------
//  waveform_view.h - the scrolling / zooming trace area
// ---------------------------------------------------------------------------
#pragma once

#include "capture/capture_buffer.h"
#include "decode/decoder.h"
#include "gfx.h"
#include "logic_types.h"

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

class WaveformView {
public:
    void setArea(const Rect& r) { _area = r; }
    const Rect& area() const { return _area; }

    // Fit the whole capture into the plot width.
    void fit(uint32_t sampleCount);

    // Multiply the zoom by `factor` (>1 zooms in) keeping the sample under
    // `anchorX` pinned to that pixel.
    void zoom(double factor, int anchorX, uint32_t sampleCount);

    // Scroll by whole pixels.
    void panPixels(double dx, uint32_t sampleCount);

    // Centre the view on a sample without changing the zoom.
    void centerOn(double sample, uint32_t sampleCount);

    double sampleAtX(int x) const;
    double xForSample(double sample) const;

    double start() const { return _start; }
    double samplesPerPixel() const { return _spp; }
    int plotX() const { return _area.x; }
    int plotW() const { return _area.w; }

    int64_t cursorA = -1;
    int64_t cursorB = -1;

    // Render the plot.  With `canvasLocal` the output is placed at (0,0) so it
    // can go into an off-screen canvas the caller then pushes to area().
    void draw(LaGfx& d, const CaptureBuffer& buf, const CaptureInfo& info,
              const ChannelConfig channels[LA_MAX_CHANNELS],
              const AnnotationList* anns, bool canvasLocal = false);

    // Vertical span of one channel lane, for hit testing.
    int laneHeight(int enabledCount) const;

private:
    // Time ruler ticks, shared between the ruler labels and the gridline
    // columns so the two cannot disagree.
    struct Ticks {
        int x[32];
        int n = 0;
        bool has(int px) const {
            for (int i = 0; i < n; ++i) {
                if (x[i] == px) return true;
            }
            return false;
        }
    };

    Ticks computeTicks(const CaptureInfo& info, const Rect& r) const;
    void drawRuler(LaGfx& d, const CaptureInfo& info, const Rect& r, const Ticks& t);
    void renderTraces(LaGfx& d, const CaptureBuffer& buf,
                      const ChannelConfig channels[LA_MAX_CHANNELS],
                      const Rect& band, int lane, int lod, const Ticks& t);
    void drawAnnotations(LaGfx& d, const AnnotationList& anns, const Rect& r);
    void drawCursors(LaGfx& d, const Rect& r, const CaptureInfo& info);

    Rect   _area;
    double _start = 0.0;    // leftmost visible sample (may be fractional)
    double _spp   = 1.0;    // samples per pixel
};
