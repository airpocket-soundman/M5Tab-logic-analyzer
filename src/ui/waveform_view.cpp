#include "waveform_view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"

namespace {

// Nice 1 / 2 / 5 x 10^n step just above `raw`.
double niceStep(double raw) {
    if (raw <= 0) return 1.0;
    const double mag = pow(10.0, floor(log10(raw)));
    const double norm = raw / mag;
    if (norm <= 1.0) return mag;
    if (norm <= 2.0) return 2.0 * mag;
    if (norm <= 5.0) return 5.0 * mag;
    return 10.0 * mag;
}

int countEnabled(const ChannelConfig* ch) {
    int n = 0;
    for (int i = 0; i < LA_MAX_CHANNELS; ++i) {
        if (ch[i].enabled) n++;
    }
    return n ? n : 1;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Viewport maths
// ---------------------------------------------------------------------------
void WaveformView::fit(uint32_t sampleCount) {
    if (_area.w <= 0) return;
    if (sampleCount == 0) {
        _start = 0;
        _spp = 1.0;
        return;
    }
    _spp = static_cast<double>(sampleCount) / _area.w;
    if (_spp <= 0) _spp = 1.0;
    _start = 0;
}

void WaveformView::zoom(double factor, int anchorX, uint32_t sampleCount) {
    if (_area.w <= 0 || factor <= 0) return;
    const double anchorSample = sampleAtX(anchorX);
    double spp = _spp / factor;

    // Zooming in past ~1/64 sample per pixel just stretches steps; zooming out
    // past "whole capture in one screen" leaves dead space.
    const double minSpp = 1.0 / 64.0;
    const double maxSpp = sampleCount > 0
                              ? static_cast<double>(sampleCount) / _area.w * 4.0
                              : 1.0;
    if (spp < minSpp) spp = minSpp;
    if (spp > maxSpp) spp = maxSpp;
    _spp = spp;

    _start = anchorSample - (anchorX - _area.x) * _spp;
    panPixels(0, sampleCount);
}

void WaveformView::panPixels(double dx, uint32_t sampleCount) {
    _start -= dx * _spp;
    const double span = _area.w * _spp;
    const double minStart = -span * 0.25;
    const double maxStart = static_cast<double>(sampleCount) - span * 0.75;
    if (_start < minStart) _start = minStart;
    if (_start > maxStart) _start = maxStart > minStart ? maxStart : minStart;
}

void WaveformView::centerOn(double sample, uint32_t sampleCount) {
    _start = sample - _area.w * _spp * 0.5;
    panPixels(0, sampleCount);
}

double WaveformView::sampleAtX(int x) const {
    return _start + (x - _area.x) * _spp;
}

double WaveformView::xForSample(double sample) const {
    return _area.x + (sample - _start) / _spp;
}

int WaveformView::laneHeight(int enabledCount) const {
    const int traceH = _area.h - theme::kRulerH -
                       theme::kDecodeRows * theme::kDecodeRowH;
    return traceH / (enabledCount > 0 ? enabledCount : 1);
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------
void WaveformView::draw(LaGfx& d, const CaptureBuffer& buf, const CaptureInfo& info,
                        const ChannelConfig channels[LA_MAX_CHANNELS],
                        const AnnotationList* anns, bool canvasLocal) {
    // Rendering is written against _area, so pointing it at the origin is all
    // it takes to compose into an off-screen canvas instead of the display.
    const Rect saved = _area;
    if (canvasLocal) _area = Rect{0, 0, saved.w, saved.h};
    const Rect& a = _area;

    d.startWrite();

    const Ticks ticks = computeTicks(info, Rect{a.x, a.y, a.w, theme::kRulerH});
    drawRuler(d, info, Rect{a.x, a.y, a.w, theme::kRulerH}, ticks);

    const int nEnabled = countEnabled(channels);
    const int lane = laneHeight(nEnabled);
    const int lod = buf.pickLod(_spp);

    const Rect band{a.x, a.y + theme::kRulerH, a.w,
                    a.h - theme::kRulerH - theme::kDecodeRows * theme::kDecodeRowH};
    renderTraces(d, buf, channels, band, lane, lod, ticks);

    Rect annBand{a.x, a.y + a.h - theme::kDecodeRows * theme::kDecodeRowH,
                 a.w, theme::kDecodeRows * theme::kDecodeRowH};
    if (anns && anns->size() > 0) {
        drawAnnotations(d, *anns, annBand);
    } else {
        d.fillRect(annBand.x, annBand.y, annBand.w, annBand.h, theme::kBg);
    }

    // Trigger marker
    if (info.triggerIndex >= 0) {
        const double tx = xForSample(static_cast<double>(info.triggerIndex));
        if (tx >= a.x && tx < a.x + a.w) {
            d.drawFastVLine(static_cast<int>(tx), a.y, a.h, theme::kTrigLine);
            d.fillTriangle(static_cast<int>(tx) - 6, a.y,
                           static_cast<int>(tx) + 6, a.y,
                           static_cast<int>(tx), a.y + 9, theme::kTrigLine);
        }
    }

    drawCursors(d, a, info);
    d.endWrite();

    _area = saved;
}

WaveformView::Ticks WaveformView::computeTicks(const CaptureInfo& info,
                                               const Rect& r) const {
    Ticks t;
    const double sps = info.secondsPerSample();
    if (sps <= 0) return t;

    // Aim for a label roughly every 130 px.
    const double secPerPixel = _spp * sps;
    const double step = niceStep(secPerPixel * 130.0);
    const int64_t origin = info.triggerIndex >= 0 ? info.triggerIndex : 0;

    const double tLeft = (_start - origin) * sps;
    const double tRight = tLeft + r.w * secPerPixel;

    const double firstTick = ceil(tLeft / step) * step;
    for (double s = firstTick; s <= tRight && t.n < (int)(sizeof(t.x) / sizeof(t.x[0]));
         s += step) {
        const double sample = origin + s / sps;
        const int x = static_cast<int>(xForSample(sample) + 0.5);
        if (x < r.x || x >= r.x + r.w) continue;
        t.x[t.n++] = x;
    }
    return t;
}

void WaveformView::drawRuler(LaGfx& d, const CaptureInfo& info, const Rect& r,
                             const Ticks& ticks) {
    d.fillRect(r.x, r.y, r.w, r.h, theme::kPanel);
    d.drawFastHLine(r.x, r.y + r.h - 1, r.w, theme::kPanelEdge);

    const double sps = info.secondsPerSample();
    if (sps <= 0) return;

    const double secPerPixel = _spp * sps;
    const double step = niceStep(secPerPixel * 130.0);
    const int64_t origin = info.triggerIndex >= 0 ? info.triggerIndex : 0;

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::top_center);
    d.setTextColor(theme::kTextDim, theme::kPanel);

    char buf[24];
    for (int i = 0; i < ticks.n; ++i) {
        const int x = ticks.x[i];
        d.drawFastVLine(x, r.y + r.h - 8, 7, theme::kGridMajor);
        const double t = (sampleAtX(x) - origin) * sps;
        // Suppress -0 for the tick sitting exactly on the trigger.
        formatSeconds(fabs(t) < step * 1e-6 ? 0.0 : t, buf, sizeof(buf));
        d.drawString(buf, x, r.y + 3);
    }
}

// ---------------------------------------------------------------------------
//  Traces
// ---------------------------------------------------------------------------
//  Rendered a column at a time, painting that column's background, gridline and
//  every channel's segment together.  Two reasons:
//
//  * Nothing is ever left blank.  The panel has one framebuffer that is scanned
//    out continuously, so clearing the whole plot and then repainting it shows
//    the cleared state on screen - that is the flicker.  A column is narrow
//    enough that the intermediate state is never visible.
//  * One summary lookup serves all eight channels.  Drawing channel by channel
//    asked the LOD pyramid for the same column range eight times over; doing it
//    once cuts the work per redraw by the same factor, which keeps the UI out
//    of the way of the next capture.
// ---------------------------------------------------------------------------
void WaveformView::renderTraces(LaGfx& d, const CaptureBuffer& buf,
                                const ChannelConfig channels[LA_MAX_CHANNELS],
                                const Rect& band, int lane, int lod,
                                const Ticks& ticks) {
    if (band.h <= 0 || band.w <= 0) return;

    // Lane geometry, resolved once instead of per column.
    struct Lane {
        int ch;
        int yHigh;
        int yLow;
        int sep;
        uint16_t color;
        bool invert;
    };
    Lane lanes[LA_MAX_CHANNELS];
    int nLanes = 0;
    int laneY = band.y;
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!channels[ch].enabled) continue;
        const int pad = lane / 5;
        lanes[nLanes++] = Lane{ch, laneY + pad, laneY + lane - pad - 2,
                               laneY + lane - 1, theme::kChannel[ch],
                               channels[ch].invert};
        laneY += lane;
    }

    const uint32_t n = buf.count();
    const int x0 = band.x;
    const int x1 = band.x + band.w;
    int prevY[LA_MAX_CHANNELS];
    for (int i = 0; i < LA_MAX_CHANNELS; ++i) prevY[i] = -1;

    int tickIdx = 0;
    for (int x = x0; x < x1; ++x) {
        // Background first, in the gridline colour on tick columns.
        while (tickIdx < ticks.n && ticks.x[tickIdx] < x) ++tickIdx;
        const bool onTick = (tickIdx < ticks.n && ticks.x[tickIdx] == x);
        d.drawFastVLine(x, band.y, band.h, onTick ? theme::kGrid : theme::kBg);
        for (int i = 0; i < nLanes; ++i) {
            d.drawPixel(x, lanes[i].sep, theme::kGrid);
        }

        if (n == 0) continue;

        const double s0d = _start + (x - x0) * _spp;
        const double s1d = s0d + _spp;
        if (s1d < 0 || s0d >= n) {
            for (int i = 0; i < LA_MAX_CHANNELS; ++i) prevY[i] = -1;
            continue;
        }

        uint32_t s0 = s0d < 0 ? 0 : static_cast<uint32_t>(s0d);
        uint32_t s1 = s1d > n ? n : static_cast<uint32_t>(s1d);
        if (s1 <= s0) s1 = s0 + 1;      // zoomed in past one sample per column
        if (s1 > n) s1 = n;

        // One lookup covering every channel in this column.
        const LodEntry e = buf.summarize(lod, s0, s1 - s0);

        for (int i = 0; i < nLanes; ++i) {
            const Lane& L = lanes[i];
            const uint8_t mask = static_cast<uint8_t>(1u << L.ch);
            bool anyHigh = (e.orv & mask) != 0;
            bool allHigh = (e.andv & mask) != 0;
            if (L.invert) {
                const bool anyLow = !allHigh;
                allHigh = !anyHigh;
                anyHigh = anyLow;
            }

            if (anyHigh && !allHigh) {
                // Both levels inside this column: draw the transition span.
                d.drawFastVLine(x, L.yHigh, L.yLow - L.yHigh + 1, L.color);
                prevY[L.ch] = -1;
            } else {
                const int y = allHigh ? L.yHigh : L.yLow;
                if (prevY[L.ch] >= 0 && prevY[L.ch] != y) {
                    const int top = y < prevY[L.ch] ? y : prevY[L.ch];
                    d.drawFastVLine(x, top, abs(y - prevY[L.ch]) + 1, L.color);
                } else {
                    d.drawPixel(x, y, L.color);
                }
                prevY[L.ch] = y;
            }
        }
    }
}

void WaveformView::drawAnnotations(LaGfx& d, const AnnotationList& anns, const Rect& r) {
    d.fillRect(r.x, r.y, r.w, r.h, theme::kPanel);
    d.drawFastHLine(r.x, r.y, r.w, theme::kPanelEdge);

    const double sLeft = _start;
    const double sRight = _start + r.w * _spp;
    if (sRight < 0) return;

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::middle_center);

    uint32_t i = anns.lowerBound(sLeft < 0 ? 0 : static_cast<uint32_t>(sLeft));
    for (; i < anns.size(); ++i) {
        const Annotation& a = anns[i];
        if (a.startSample > sRight) break;
        if (a.row >= theme::kDecodeRows) continue;

        int xa = static_cast<int>(xForSample(a.startSample) + 0.5);
        int xb = static_cast<int>(xForSample(a.endSample) + 0.5);
        if (xb <= xa) xb = xa + 1;
        if (xb < r.x || xa > r.x + r.w) continue;
        if (xa < r.x) xa = r.x;
        if (xb > r.x + r.w) xb = r.x + r.w;

        const int y = r.y + a.row * theme::kDecodeRowH + 1;
        const int h = theme::kDecodeRowH - 3;
        const uint16_t c = theme::annColor(a.kind);
        d.fillRect(xa, y, xb - xa, h, c);
        d.drawRect(xa, y, xb - xa, h, theme::kPanelEdge);

        // Only draw the text when the box is wide enough for it to be readable.
        const int need = static_cast<int>(strlen(a.text)) * 8 + 6;
        if (xb - xa >= need) {
            d.setTextColor(theme::kText, c);
            d.drawString(a.text, (xa + xb) / 2, y + h / 2);
        }
    }
}

void WaveformView::drawCursors(LaGfx& d, const Rect& r, const CaptureInfo& info) {
    (void)info;
    struct Cur { int64_t s; uint16_t c; const char* tag; };
    const Cur cur[2] = {{cursorA, theme::kCursorA, "A"}, {cursorB, theme::kCursorB, "B"}};

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::top_center);
    for (const auto& k : cur) {
        if (k.s < 0) continue;
        const int x = static_cast<int>(xForSample(static_cast<double>(k.s)) + 0.5);
        if (x < r.x || x >= r.x + r.w) continue;
        d.drawFastVLine(x, r.y, r.h, k.c);
        d.fillRect(x - 8, r.y + theme::kRulerH - 16, 16, 15, k.c);
        d.setTextColor(theme::kBg, k.c);
        d.drawString(k.tag, x, r.y + theme::kRulerH - 15);
    }
}
