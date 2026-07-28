#include "waveform_view.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

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
                        const AnnotationList* anns) {
    const Rect& a = _area;
    d.startWrite();
    d.fillRect(a.x, a.y, a.w, a.h, theme::kBg);

    Rect ruler{a.x, a.y, a.w, theme::kRulerH};
    drawRuler(d, info, ruler);

    const int nEnabled = countEnabled(channels);
    const int lane = laneHeight(nEnabled);
    const int lod = buf.pickLod(_spp);

    int laneY = a.y + theme::kRulerH;
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!channels[ch].enabled) continue;
        Rect r{a.x, laneY, a.w, lane};
        // Lane separator
        d.drawFastHLine(a.x, laneY + lane - 1, a.w, theme::kGrid);
        drawTrace(d, buf, ch, channels[ch].invert, theme::kChannel[ch], r, lod);
        laneY += lane;
    }

    if (anns && anns->size() > 0) {
        Rect r{a.x, a.y + a.h - theme::kDecodeRows * theme::kDecodeRowH,
               a.w, theme::kDecodeRows * theme::kDecodeRowH};
        drawAnnotations(d, *anns, r);
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
}

void WaveformView::drawRuler(LaGfx& d, const CaptureInfo& info, const Rect& r) {
    d.fillRect(r.x, r.y, r.w, r.h, theme::kPanel);
    d.drawFastHLine(r.x, r.y + r.h - 1, r.w, theme::kPanelEdge);

    const double sps = info.secondsPerSample();
    if (sps <= 0) return;

    // Aim for a label roughly every 130 px.
    const double secPerPixel = _spp * sps;
    const double step = niceStep(secPerPixel * 130.0);
    const int64_t origin = info.triggerIndex >= 0 ? info.triggerIndex : 0;

    const double tLeft = (_start - origin) * sps;
    const double tRight = tLeft + r.w * secPerPixel;

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::top_center);
    d.setTextColor(theme::kTextDim, theme::kPanel);

    char buf[24];
    const double firstTick = ceil(tLeft / step) * step;
    for (double t = firstTick; t <= tRight; t += step) {
        const double sample = origin + t / sps;
        const int x = static_cast<int>(xForSample(sample) + 0.5);
        if (x < r.x || x >= r.x + r.w) continue;
        d.drawFastVLine(x, r.y + r.h - 8, 7, theme::kGridMajor);
        // Suppress -0 for the tick sitting exactly on the trigger.
        formatSeconds(fabs(t) < step * 1e-6 ? 0.0 : t, buf, sizeof(buf));
        d.drawString(buf, x, r.y + 3);
        // Faint gridline down the plot.
        d.drawFastVLine(x, r.y + r.h, _area.h - r.h, theme::kGrid);
    }
}

void WaveformView::drawTrace(LaGfx& d, const CaptureBuffer& buf, int ch, bool invert,
                             uint16_t color, const Rect& lane, int lod) {
    const uint32_t n = buf.count();
    if (n == 0 || lane.h < 6) return;

    const int pad = lane.h / 5;
    const int yHigh = lane.y + pad;
    const int yLow = lane.y + lane.h - pad - 2;
    const uint8_t mask = static_cast<uint8_t>(1u << ch);

    const int x0 = lane.x;
    const int x1 = lane.x + lane.w;

    if (_spp >= 1.0) {
        // One or more samples per column: use the (OR, AND) summary so a single
        // sample glitch inside the column still shows up as a transition.
        int prevY = -1;
        for (int x = x0; x < x1; ++x) {
            const double s0d = _start + (x - x0) * _spp;
            const double s1d = s0d + _spp;
            if (s1d < 0 || s0d >= n) { prevY = -1; continue; }
            uint32_t s0 = s0d < 0 ? 0 : static_cast<uint32_t>(s0d);
            uint32_t s1 = s1d > n ? n : static_cast<uint32_t>(s1d);
            if (s1 <= s0) s1 = s0 + 1;
            if (s1 > n) s1 = n;

            LodEntry e = buf.summarize(lod, s0, s1 - s0);
            bool anyHigh = (e.orv & mask) != 0;
            bool allHigh = (e.andv & mask) != 0;
            if (invert) {
                const bool anyLow = !allHigh;
                allHigh = !anyHigh;
                anyHigh = anyLow;
            }

            if (anyHigh && !allHigh) {
                d.drawFastVLine(x, yHigh, yLow - yHigh + 1, color);
                prevY = -1;    // both levels present, nothing to connect
            } else {
                const int y = allHigh ? yHigh : yLow;
                if (prevY >= 0 && prevY != y) {
                    d.drawFastVLine(x, y < prevY ? y : prevY,
                                    abs(y - prevY) + 1, color);
                }
                d.drawPixel(x, y, color);
                prevY = y;
            }
        }
    } else {
        // Fewer than one sample per column: draw real steps.
        int32_t s = static_cast<int32_t>(floor(_start));
        if (s < 0) s = 0;
        const double pxPerSample = 1.0 / _spp;
        int prevY = -1;
        int prevX = -1;
        for (; s < static_cast<int32_t>(n); ++s) {
            const double xf = xForSample(s);
            if (xf >= x1) break;
            bool high = (buf.at(s) & mask) != 0;
            if (invert) high = !high;
            const int y = high ? yHigh : yLow;
            int xa = static_cast<int>(xf + 0.5);
            int xb = static_cast<int>(xf + pxPerSample + 0.5);
            if (xb <= xa) xb = xa + 1;
            if (xb <= x0) { prevY = y; prevX = xb; continue; }
            if (xa < x0) xa = x0;
            if (xb > x1) xb = x1;
            if (prevY >= 0 && prevY != y && prevX >= x0 && prevX < x1) {
                d.drawFastVLine(xa, y < prevY ? y : prevY, abs(y - prevY) + 1, color);
            }
            d.drawFastHLine(xa, y, xb - xa, color);
            prevY = y;
            prevX = xb;
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
