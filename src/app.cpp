#include "app.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ui/theme.h"

using namespace theme;

// ---------------------------------------------------------------------------
//  Button identifiers
// ---------------------------------------------------------------------------
enum : int {
    ID_NONE = 0,
    ID_RUN, ID_SINGLE, ID_RATE_DN, ID_RATE_UP, ID_DEPTH_DN, ID_DEPTH_UP,
    ID_ENGINE, ID_TRIGMODE,
    ID_ZOOM_OUT, ID_ZOOM_IN, ID_FIT, ID_AUTO, ID_HOME, ID_PAGE_L, ID_PAGE_R, ID_END,
    ID_GOTRIG, ID_CURA, ID_CURB, ID_CURCLR,
    ID_OV_TRIG, ID_OV_CHAN, ID_OV_DEC, ID_OV_SAVE, ID_OV_INFO,
    ID_CLOSE,
    ID_TM_AUTO, ID_TM_NORMAL, ID_TM_SINGLE,
    ID_TRIGPOS_DN, ID_TRIGPOS_UP, ID_TRIGCLR,
    ID_DEC_RUN, ID_DEC_CLEAR,

    ID_TRIGCH0 = 100,          // .. +7
    ID_CHEN0   = 120,          // .. +7
    ID_CHINV0  = 140,          // .. +7
    ID_DEC_KIND0 = 160,        // .. +3
    ID_DEC_P0  = 170,          // .. +15
    ID_SD_MOUNT = 200, ID_SD_CSV, ID_SD_VCD, ID_SD_DEC, ID_SD_SHOT,
};

namespace {

constexpr uint32_t kDepthShiftMin = 16;   //  64 kSa
constexpr uint32_t kDepthShiftMax = 23;   //   8 MSa

const uint32_t kBaudMenu[] = {
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200,
    230400, 460800, 921600, 1000000, 2000000,
};
constexpr int kBaudMenuCount = sizeof(kBaudMenu) / sizeof(kBaudMenu[0]);

TrigCond nextCond(TrigCond c) {
    switch (c) {
        case TrigCond::Ignore:  return TrigCond::Rising;
        case TrigCond::Rising:  return TrigCond::Falling;
        case TrigCond::Falling: return TrigCond::Either;
        case TrigCond::Either:  return TrigCond::High;
        case TrigCond::High:    return TrigCond::Low;
        case TrigCond::Low:
        default:                return TrigCond::Ignore;
    }
}

int8_t nextChannel(int8_t c, bool allowNone) {
    int8_t n = static_cast<int8_t>(c + 1);
    if (n >= LA_MAX_CHANNELS) n = allowNone ? -1 : 0;
    return n;
}

const char* chanText(int8_t c, char* buf, size_t len) {
    if (c < 0) snprintf(buf, len, "--");
    else snprintf(buf, len, "CH%d", c);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Lifecycle
// ---------------------------------------------------------------------------
void App::begin() {
    auto& d = M5.Display;
    d.setRotation(LA_ROTATION);
    d.fillScreen(kBg);
    d.setTextColor(kText, kBg);
    d.setFont(&fonts::Font4);
    d.setTextDatum(textdatum_t::middle_center);
    d.drawString("M5Tab5 Logic Analyzer", d.width() / 2, d.height() / 2 - 20);
    d.setFont(&fonts::Font2);
    d.drawString("initialising...", d.width() / 2, d.height() / 2 + 20);

    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        _chan[ch].enabled = true;
        _chan[ch].invert = false;
        snprintf(_chan[ch].name, sizeof(_chan[ch].name), "CH%d", ch);
    }
    _trig.cond[0] = TrigCond::Rising;

    _cpu = createCpuSampler();
    _parlio = createParlioSampler();

    const char* reason = nullptr;
    if (_cpu) _cpu->begin(&reason);
    if (_parlio && !_parlio->begin(&reason)) {
        _parlio = nullptr;
        _engineNote = reason ? reason : "PARLIO unavailable";
    }

    _anns.reserve(6000);

    // Layout has to exist before applyConfig() so the view can be fitted.
    buildLayout();
    applyConfig();

    _wave.fit(0);
    _dirtyChrome = _dirtyWave = _dirtyPanel = true;
}

void App::buildLayout() {
    auto& d = M5.Display;
    const int W = d.width();
    const int H = d.height();

    _rTop    = {0, 0, W, kTopBarH};
    _rBottom = {0, H - kBottomBarH, W, kBottomBarH};
    _rPanel  = {0, H - kBottomBarH - kPanelH, W, kPanelH};
    _rWave   = {0, kTopBarH, W, _rPanel.y - kTopBarH};

    _wave.setArea({kLabelW, _rWave.y, W - kLabelW, _rWave.h});

    const int ow = 940, oh = 560;
    _rOverlay = {(W - ow) / 2, (H - oh) / 2, ow, oh};
}

void App::applyConfig() {
    if (_rateIndex < 0) _rateIndex = 0;
    if (_rateIndex >= kRateMenuCount) _rateIndex = kRateMenuCount - 1;
    if (_depthShift < (int)kDepthShiftMin) _depthShift = kDepthShiftMin;
    if (_depthShift > (int)kDepthShiftMax) _depthShift = kDepthShiftMax;

    _cfg.rateHz = kRateMenu[_rateIndex];
    _cfg.depth = 1u << _depthShift;

    // Only touch the buffer when the depth really changed - reallocating would
    // throw away the capture the user is currently looking at, and a rate
    // change alone does not need it.
    if (_buf.capacity() != _cfg.depth) {
        if (!_buf.allocate(_cfg.depth, false)) {
            // Back off until something fits; a Tab5 has 32 MB but the display
            // framebuffer and the LOD pyramid want their share too.
            while (_depthShift > (int)kDepthShiftMin) {
                _depthShift--;
                _cfg.depth = 1u << _depthShift;
                if (_buf.allocate(_cfg.depth, false)) break;
            }
            toast("depth reduced to fit PSRAM");
        }
        _info.samples = 0;
        _info.triggerIndex = -1;
        _anns.clear();
        _needFit = true;
        _dirtyWave = _dirtyPanel = true;
    }
    selectEngine();
    _dirtyChrome = true;
}

void App::selectEngine() {
    const char* reason = nullptr;
    double achieved = 0;
    _sampler = nullptr;

    const bool wantParlio =
        (_cfg.engine == Engine::Auto || _cfg.engine == Engine::Parlio) && _parlio;

    if (wantParlio && _parlio->configure(_cfg, _buf, &achieved, &reason)) {
        _sampler = _parlio;
        _activeEngine = Engine::Parlio;
        _engineNote = "";
    } else {
        if (wantParlio) _engineNote = reason ? reason : "PARLIO configure failed";
        if (_cpu && _cpu->configure(_cfg, _buf, &achieved, &reason)) {
            _sampler = _cpu;
            _activeEngine = Engine::Cpu;
        }
    }
    _info.actualRateHz = achieved;
    _info.engineUsed = _activeEngine;
}

// ---------------------------------------------------------------------------
//  Capture
// ---------------------------------------------------------------------------
void App::startRun(TrigMode mode) {
    if (!_sampler) { toast("no sampling engine available"); return; }
    _trig.mode = mode;
    _continuous = (mode != TrigMode::Single);
    _runStartMs = millis();
    _state = CaptureState::Sampling;
    _dirtyChrome = true;
}

void App::stopRun() {
    _continuous = false;
    if (_state == CaptureState::Sampling) _state = CaptureState::Idle;
    _dirtyChrome = true;
}

void App::serviceCapture() {
    if (_state != CaptureState::Sampling || !_sampler) return;

    // capture() blocks for the whole sweep - a deep, slow capture can take
    // seconds - so push the "SAMPLING" state to the screen before we go in,
    // otherwise the UI looks frozen with a stale RUN button.
    if (_dirtyChrome) {
        _btnCount = 0;
        buildTopBar();
        buildBottomBar();
        drawTopBar();
        drawBottomBar();
        _dirtyChrome = false;
    }

    double measured = 0;
    const uint32_t got = _sampler->capture(_buf, _cfg.depth, &measured);
    _captureCount++;
    if (got == 0) {
        _state = CaptureState::Failed;
        _continuous = false;
        toast("capture failed (engine returned no samples)");
        _dirtyChrome = _dirtyPanel = true;
        return;
    }
    finishCapture(got, measured);
}

void App::finishCapture(uint32_t got, double measuredRate) {
    _buf.setCount(got);
    _info.samples = got;
    if (measuredRate > 0) _info.actualRateHz = measuredRate;
    _info.engineUsed = _activeEngine;

    const TriggerMasks masks = compileTrigger(_trig);
    int64_t idx = -1;
    if (masks.any()) {
        const uint32_t pre =
            static_cast<uint32_t>(static_cast<uint64_t>(got) * _trig.posPercent / 100);
        idx = findTrigger(_buf, masks, pre ? pre : 1);
        if (idx < 0 && _trig.mode == TrigMode::Normal &&
            (millis() - _runStartMs) < _trig.normalTimeoutMs) {
            // Keep hunting: leave the previous picture on screen so the user can
            // still see what the last matching capture looked like.
            _state = CaptureState::Sampling;
            _dirtyChrome = true;
            return;
        }
    }
    _info.triggerIndex = idx;

    _buf.buildLod();
    measureChannels(_buf, _info.secondsPerSample(), 0, got, _stats);
    runDecoderNow();

    if (_needFit) {
        autoScale(got);
        _needFit = false;
    }
    if (idx >= 0) {
        // Put the trigger at posPercent of the plot width, the way a bench
        // scope parks its trigger marker.
        const double screenSamples = _wave.plotW() * _wave.samplesPerPixel();
        _wave.centerOn(idx + screenSamples * (0.5 - _trig.posPercent / 100.0), got);
    } else {
        _wave.panPixels(0, got);
    }

    _state = _continuous ? CaptureState::Sampling : CaptureState::Done;
    _dirtyChrome = _dirtyWave = _dirtyPanel = true;
}

// Fitting a whole capture into the plot is useless as an initial view: a 2 MSa
// sweep across ~1180 px is 1700 samples per column, so every channel collapses
// into a solid bar and no structure is visible at all.  Instead, pick the zoom
// from the data - scale so the narrowest pulse anywhere in the capture is a few
// pixels wide, which is the coarsest view where every channel still shows its
// shape.  `Fit` remains one tap away for the overview.
void App::autoScale(uint32_t sampleCount) {
    _wave.fit(sampleCount);
    const double sps = _info.secondsPerSample();
    if (sps <= 0 || sampleCount == 0) return;

    double narrowest = 0.0;   // in samples
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!_chan[ch].enabled || _stats[ch].edges == 0) continue;
        const double widths[2] = {_stats[ch].minHighSec, _stats[ch].minLowSec};
        for (double w : widths) {
            if (w <= 0) continue;
            const double s = w / sps;
            if (narrowest <= 0 || s < narrowest) narrowest = s;
        }
    }
    if (narrowest <= 0) return;          // nothing toggled: the fit view is all there is

    constexpr double kPixelsPerPulse = 3.0;
    double target = narrowest / kPixelsPerPulse;
    if (target < 1.0 / 64.0) target = 1.0 / 64.0;

    const double fitSpp = _wave.samplesPerPixel();
    if (target >= fitSpp) return;        // the whole capture already resolves
    _wave.zoom(fitSpp / target, _wave.plotX() + _wave.plotW() / 2, sampleCount);
}

void App::reanalyze() {
    if (_buf.count() == 0) return;
    measureChannels(_buf, _info.secondsPerSample(), 0, _buf.count(), _stats);
    runDecoderNow();
    _dirtyWave = _dirtyPanel = true;
}

void App::runDecoderNow() {
    if (_dec.kind == DecoderKind::None || _buf.count() == 0) {
        _anns.clear();
        return;
    }
    const char* err = nullptr;
    if (!runDecoder(_buf, _info, _dec, 0, _buf.count(), _anns, &err)) {
        toast("%s", err ? err : "decoder error");
    } else if (_anns.truncated()) {
        toast("decode truncated at %u annotations", (unsigned)_anns.size());
    }
}

// ---------------------------------------------------------------------------
//  Main loop
// ---------------------------------------------------------------------------
void App::loop() {
    M5.update();
    pollSerialApi();
    handleTouch();
    serviceCapture();

    if (_toast[0] && millis() - _toastMs > 3500) {
        _toast[0] = 0;
        _dirtyPanel = true;
    }
    drawAll();
    delay(2);
}

// ---------------------------------------------------------------------------
//  Buttons
// ---------------------------------------------------------------------------
void App::addButton(int id, const Rect& r, const char* text, bool active,
                    bool enabled, uint16_t tint) {
    if (_btnCount >= kMaxButtons) return;
    Button& b = _btn[_btnCount++];
    b.r = r;
    b.id = id;
    b.active = active;
    b.enabled = enabled;
    b.tint = tint;
    snprintf(b.text, sizeof(b.text), "%s", text ? text : "");
}

void App::buildTopBar() {
    char buf[24], txt[48];
    const int y = 6;
    const int h = kTopBarH - 12;

    const bool running = (_state == CaptureState::Sampling);
    addButton(ID_RUN, {6, y, 88, h}, running ? "STOP" : "RUN", running,
              true, running ? kBad : kGood);
    addButton(ID_SINGLE, {100, y, 88, h}, "SINGLE", false, !running);

    formatHz(static_cast<double>(_cfg.rateHz), buf, sizeof(buf));
    addButton(ID_RATE_DN, {200, y, 40, h}, "-");
    addButton(ID_NONE, {242, y, 128, h}, buf, false, false);
    addButton(ID_RATE_UP, {372, y, 40, h}, "+");

    formatCount(_cfg.depth, buf, sizeof(buf));
    snprintf(txt, sizeof(txt), "%sSa", buf);
    addButton(ID_DEPTH_DN, {424, y, 40, h}, "-");
    addButton(ID_NONE, {466, y, 112, h}, txt, false, false);
    addButton(ID_DEPTH_UP, {580, y, 40, h}, "+");

    snprintf(txt, sizeof(txt), "ENG:%s", engineName(_cfg.engine));
    addButton(ID_ENGINE, {632, y, 132, h}, txt);

    addButton(ID_TRIGMODE, {776, y, 124, h}, trigModeName(_trig.mode));
}

void App::buildBottomBar() {
    const int y = _rBottom.y + 6;
    const int h = kBottomBarH - 12;
    int x = 6;
    auto next = [&](int w) { Rect r{x, y, w, h}; x += w + 4; return r; };

    addButton(ID_ZOOM_OUT, next(64), "Zoom-");
    addButton(ID_ZOOM_IN,  next(64), "Zoom+");
    addButton(ID_FIT,      next(56), "Fit");
    addButton(ID_AUTO,     next(56), "Auto");
    addButton(ID_HOME,     next(48), "|<");
    addButton(ID_PAGE_L,   next(48), "<<");
    addButton(ID_PAGE_R,   next(48), ">>");
    addButton(ID_END,      next(48), ">|");
    addButton(ID_GOTRIG,   next(64), "Trig", false, _info.triggerIndex >= 0);
    addButton(ID_CURA,     next(56), "Cur A", _activeCursor == 0, true, kCursorA);
    addButton(ID_CURB,     next(56), "Cur B", _activeCursor == 1, true, kCursorB);
    addButton(ID_CURCLR,   next(56), "Clr");

    x += 12;
    addButton(ID_OV_TRIG, next(96), "Trigger",  _overlay == Overlay::Trigger);
    addButton(ID_OV_CHAN, next(96), "Channels", _overlay == Overlay::Channels);
    addButton(ID_OV_DEC,  next(96), "Decode",   _overlay == Overlay::Decode);
    addButton(ID_OV_SAVE, next(80), "Save",     _overlay == Overlay::Storage);
    addButton(ID_OV_INFO, next(72), "Info",     _overlay == Overlay::Info);
}

void App::buildOverlay() {
    if (_overlay == Overlay::None) return;
    const Rect& o = _rOverlay;
    char buf[24], txt[24];

    addButton(ID_CLOSE, {o.x + o.w - 100, o.y + 10, 90, 40}, "Close", false, true, kBad);

    switch (_overlay) {
        case Overlay::Trigger: {
            int y = o.y + 70;
            addButton(ID_TM_AUTO,   {o.x + 24, y, 130, 42}, "AUTO",
                      _trig.mode == TrigMode::Auto);
            addButton(ID_TM_NORMAL, {o.x + 160, y, 130, 42}, "NORMAL",
                      _trig.mode == TrigMode::Normal);
            addButton(ID_TM_SINGLE, {o.x + 296, y, 130, 42}, "SINGLE",
                      _trig.mode == TrigMode::Single);

            snprintf(txt, sizeof(txt), "pre %u%%", (unsigned)_trig.posPercent);
            addButton(ID_TRIGPOS_DN, {o.x + 500, y, 46, 42}, "-");
            addButton(ID_NONE,       {o.x + 550, y, 130, 42}, txt, false, false);
            addButton(ID_TRIGPOS_UP, {o.x + 684, y, 46, 42}, "+");
            addButton(ID_TRIGCLR,    {o.x + 760, y, 140, 42}, "Clear all");

            y += 70;
            for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
                const int col = ch / 4;
                const int row = ch % 4;
                const int bx = o.x + 24 + col * 460;
                const int by = y + row * 60;
                snprintf(txt, sizeof(txt), "%s : %s", _chan[ch].name,
                         trigCondName(_trig.cond[ch]));
                addButton(ID_TRIGCH0 + ch, {bx, by, 400, 48}, txt,
                          _trig.cond[ch] != TrigCond::Ignore, true,
                          kChannel[ch]);
            }
            break;
        }

        case Overlay::Channels: {
            int y = o.y + 80;
            for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
                const int by = y + ch * 56;
                snprintf(txt, sizeof(txt), "%s  (GPIO%d)", _chan[ch].name,
                         (int)kChannelPin[ch]);
                addButton(ID_NONE, {o.x + 24, by, 300, 46}, txt, false, false,
                          kChannel[ch]);
                addButton(ID_CHEN0 + ch, {o.x + 336, by, 140, 46},
                          _chan[ch].enabled ? "SHOWN" : "hidden",
                          _chan[ch].enabled);
                addButton(ID_CHINV0 + ch, {o.x + 488, by, 140, 46},
                          _chan[ch].invert ? "INVERTED" : "normal",
                          _chan[ch].invert);
            }
            break;
        }

        case Overlay::Decode: {
            int y = o.y + 70;
            const char* kinds[] = {"Off", "UART", "I2C", "SPI"};
            for (int k = 0; k < 4; ++k) {
                addButton(ID_DEC_KIND0 + k, {o.x + 24 + k * 150, y, 140, 44},
                          kinds[k], static_cast<int>(_dec.kind) == k);
            }
            addButton(ID_DEC_RUN, {o.x + 660, y, 150, 44}, "Decode now", false,
                      _dec.kind != DecoderKind::None && _buf.count() > 0,
                      kAccent);

            y += 70;
            int p = 0;
            auto row = [&](const char* label, const char* value, int id) {
                const int rowY = y + (p / 2) * 58;
                const int rowX = o.x + 24 + (p % 2) * 450;
                snprintf(txt, sizeof(txt), "%s: %s", label, value);
                addButton(id, {rowX, rowY, 420, 48}, txt);
                p++;
            };

            if (_dec.kind == DecoderKind::Uart) {
                row("Line", chanText(_dec.uart.channel, buf, sizeof(buf)), ID_DEC_P0 + 0);
                if (_dec.uart.autoBaud) {
                    row("Baud", "auto", ID_DEC_P0 + 1);
                } else {
                    snprintf(buf, sizeof(buf), "%u", (unsigned)_dec.uart.baud);
                    row("Baud", buf, ID_DEC_P0 + 1);
                }
                snprintf(buf, sizeof(buf), "%u", (unsigned)_dec.uart.dataBits);
                row("Data bits", buf, ID_DEC_P0 + 2);
                buf[0] = _dec.uart.parity; buf[1] = 0;
                row("Parity", buf, ID_DEC_P0 + 3);
                snprintf(buf, sizeof(buf), "%u", (unsigned)_dec.uart.stopBits);
                row("Stop bits", buf, ID_DEC_P0 + 4);
                row("Polarity", _dec.uart.invert ? "inverted" : "idle high", ID_DEC_P0 + 5);
                row("Bit order", _dec.uart.lsbFirst ? "LSB first" : "MSB first", ID_DEC_P0 + 6);
            } else if (_dec.kind == DecoderKind::I2c) {
                row("SCL", chanText(_dec.i2c.sclChannel, buf, sizeof(buf)), ID_DEC_P0 + 0);
                row("SDA", chanText(_dec.i2c.sdaChannel, buf, sizeof(buf)), ID_DEC_P0 + 1);
                row("Show ACK/NAK", _dec.i2c.showAcks ? "yes" : "no", ID_DEC_P0 + 2);
            } else if (_dec.kind == DecoderKind::Spi) {
                row("CLK",  chanText(_dec.spi.clkChannel,  buf, sizeof(buf)), ID_DEC_P0 + 0);
                row("MOSI", chanText(_dec.spi.mosiChannel, buf, sizeof(buf)), ID_DEC_P0 + 1);
                row("MISO", chanText(_dec.spi.misoChannel, buf, sizeof(buf)), ID_DEC_P0 + 2);
                row("CS",   chanText(_dec.spi.csChannel,   buf, sizeof(buf)), ID_DEC_P0 + 3);
                snprintf(buf, sizeof(buf), "%d", _dec.spi.cpol ? 1 : 0);
                row("CPOL", buf, ID_DEC_P0 + 4);
                snprintf(buf, sizeof(buf), "%d", _dec.spi.cpha ? 1 : 0);
                row("CPHA", buf, ID_DEC_P0 + 5);
                row("Bit order", _dec.spi.msbFirst ? "MSB first" : "LSB first", ID_DEC_P0 + 6);
                snprintf(buf, sizeof(buf), "%u", (unsigned)_dec.spi.wordBits);
                row("Word bits", buf, ID_DEC_P0 + 7);
            }
            break;
        }

        case Overlay::Storage: {
            int y = o.y + 80;
            addButton(ID_SD_MOUNT, {o.x + 24, y, 200, 52},
                      _sd.mounted() ? "SD mounted" : "Mount SD", _sd.mounted());
            y += 70;
            addButton(ID_SD_CSV,  {o.x + 24, y, 260, 52}, "Save CSV (view)",
                      false, _buf.count() > 0);
            addButton(ID_SD_VCD,  {o.x + 300, y, 260, 52}, "Save VCD (all)",
                      false, _buf.count() > 0);
            y += 70;
            addButton(ID_SD_DEC,  {o.x + 24, y, 260, 52}, "Save decode",
                      false, _anns.size() > 0);
            addButton(ID_SD_SHOT, {o.x + 300, y, 260, 52}, "Screenshot BMP");
            break;
        }

        case Overlay::Info:
        case Overlay::None:
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
//  Input
// ---------------------------------------------------------------------------
void App::handleTouch() {
    _btnCount = 0;
    buildTopBar();
    buildBottomBar();
    buildOverlay();

    const int count = M5.Touch.getCount();

    // Two fingers on the plot: pinch zoom.
    if (count >= 2 && _overlay == Overlay::None) {
        auto a = M5.Touch.getDetail(0);
        auto b = M5.Touch.getDetail(1);
        const float dist = fabsf(static_cast<float>(a.x - b.x));
        if (!_pinching) {
            _pinching = true;
            _dragging = false;
            _pinchStart = dist > 8 ? dist : 8;
            _pinchSpp = _wave.samplesPerPixel();
            _pinchAnchor = (a.x + b.x) / 2;
        } else if (dist > 8) {
            // Spreading the fingers by N times shows N times fewer samples.
            const double target = _pinchSpp / (dist / _pinchStart);
            // zoom() takes a relative factor, so convert the absolute target.
            _wave.zoom(_wave.samplesPerPixel() / target, _pinchAnchor, _buf.count());
            _dirtyWave = _dirtyPanel = true;
        }
        return;
    }
    _pinching = false;

    if (count == 0) {
        if (_dragging) _dragging = false;
        return;
    }

    auto t = M5.Touch.getDetail(0);
    const int x = t.x;
    const int y = t.y;

    if (t.wasPressed()) {
        _pressX = _lastX = x;
        _pressY = _lastY = y;
        _pressMs = millis();
        _dragging = false;

        _touchConsumed = false;

        // Tapping outside an open overlay dismisses it.
        if (_overlay != Overlay::None && !_rOverlay.contains(x, y)) {
            _overlay = Overlay::None;
            _touchConsumed = true;
            _dirtyChrome = _dirtyWave = _dirtyPanel = true;
            return;
        }
        for (int i = 0; i < _btnCount; ++i) {
            const Button& b = _btn[i];
            if (b.id != ID_NONE && b.enabled && b.r.contains(x, y)) {
                _touchConsumed = true;
                onButton(b.id);
                return;
            }
        }
        return;
    }

    if (t.isPressed()) {
        if (_overlay != Overlay::None) return;
        const int dx = x - _lastX;
        if (_wave.area().contains(_pressX, _pressY)) {
            if (!_dragging && abs(x - _pressX) > 6) _dragging = true;
            if (_dragging && dx != 0) {
                _wave.panPixels(dx, _buf.count());
                _lastX = x;
                _lastY = y;
                _dirtyWave = _dirtyPanel = true;
            }
        }
        return;
    }

    if (t.wasReleased()) {
        // A press that a button already handled must not fall through to the
        // plot.  Without this, closing an overlay with its own Close button
        // drops a cursor wherever that button happened to sit.
        if (!_touchConsumed && !_dragging && _overlay == Overlay::None &&
            _wave.area().contains(_pressX, _pressY)) {
            onWaveTouch(_pressX, _pressY);
        }
        _dragging = false;
    }
}

void App::onWaveTouch(int x, int y) {
    (void)y;
    const double s = _wave.sampleAtX(x);
    if (s < 0 || s >= _buf.count()) return;
    if (_activeCursor == 0) _wave.cursorA = static_cast<int64_t>(s);
    else _wave.cursorB = static_cast<int64_t>(s);
    _dirtyWave = _dirtyPanel = true;
}

void App::onButton(int id) {
    _dirtyChrome = true;

    if (id >= ID_TRIGCH0 && id < ID_TRIGCH0 + LA_MAX_CHANNELS) {
        const int ch = id - ID_TRIGCH0;
        _trig.cond[ch] = nextCond(_trig.cond[ch]);
        return;
    }
    if (id >= ID_CHEN0 && id < ID_CHEN0 + LA_MAX_CHANNELS) {
        const int ch = id - ID_CHEN0;
        _chan[ch].enabled = !_chan[ch].enabled;
        _dirtyWave = _dirtyPanel = true;
        return;
    }
    if (id >= ID_CHINV0 && id < ID_CHINV0 + LA_MAX_CHANNELS) {
        const int ch = id - ID_CHINV0;
        _chan[ch].invert = !_chan[ch].invert;
        _dirtyWave = _dirtyPanel = true;
        return;
    }
    if (id >= ID_DEC_KIND0 && id < ID_DEC_KIND0 + 4) {
        _dec.kind = static_cast<DecoderKind>(id - ID_DEC_KIND0);
        runDecoderNow();
        _dirtyWave = _dirtyPanel = true;
        return;
    }
    if (id >= ID_DEC_P0 && id < ID_DEC_P0 + 16) {
        const int p = id - ID_DEC_P0;
        if (_dec.kind == DecoderKind::Uart) {
            switch (p) {
                case 0: _dec.uart.channel = nextChannel(_dec.uart.channel, false); break;
                case 1: {
                    if (_dec.uart.autoBaud) { _dec.uart.autoBaud = false; _dec.uart.baud = kBaudMenu[0]; }
                    else {
                        int i = 0;
                        while (i < kBaudMenuCount && kBaudMenu[i] != _dec.uart.baud) i++;
                        if (i + 1 >= kBaudMenuCount) _dec.uart.autoBaud = true;
                        else _dec.uart.baud = kBaudMenu[i + 1];
                    }
                    break;
                }
                case 2: _dec.uart.dataBits = _dec.uart.dataBits >= 9 ? 5 : _dec.uart.dataBits + 1; break;
                case 3: _dec.uart.parity = _dec.uart.parity == 'N' ? 'E'
                                        : (_dec.uart.parity == 'E' ? 'O' : 'N'); break;
                case 4: _dec.uart.stopBits = _dec.uart.stopBits == 1 ? 2 : 1; break;
                case 5: _dec.uart.invert = !_dec.uart.invert; break;
                case 6: _dec.uart.lsbFirst = !_dec.uart.lsbFirst; break;
                default: break;
            }
        } else if (_dec.kind == DecoderKind::I2c) {
            switch (p) {
                case 0: _dec.i2c.sclChannel = nextChannel(_dec.i2c.sclChannel, false); break;
                case 1: _dec.i2c.sdaChannel = nextChannel(_dec.i2c.sdaChannel, false); break;
                case 2: _dec.i2c.showAcks = !_dec.i2c.showAcks; break;
                default: break;
            }
        } else if (_dec.kind == DecoderKind::Spi) {
            switch (p) {
                case 0: _dec.spi.clkChannel  = nextChannel(_dec.spi.clkChannel, false); break;
                case 1: _dec.spi.mosiChannel = nextChannel(_dec.spi.mosiChannel, true); break;
                case 2: _dec.spi.misoChannel = nextChannel(_dec.spi.misoChannel, true); break;
                case 3: _dec.spi.csChannel   = nextChannel(_dec.spi.csChannel, true); break;
                case 4: _dec.spi.cpol = !_dec.spi.cpol; break;
                case 5: _dec.spi.cpha = !_dec.spi.cpha; break;
                case 6: _dec.spi.msbFirst = !_dec.spi.msbFirst; break;
                case 7: _dec.spi.wordBits = _dec.spi.wordBits >= 16 ? 4 : _dec.spi.wordBits + 1; break;
                default: break;
            }
        }
        runDecoderNow();
        _dirtyWave = _dirtyPanel = true;
        return;
    }

    switch (id) {
        case ID_RUN:
            if (_state == CaptureState::Sampling) stopRun();
            else startRun(_trig.mode == TrigMode::Single ? TrigMode::Auto : _trig.mode);
            break;
        case ID_SINGLE:
            startRun(TrigMode::Single);
            break;
        case ID_RATE_DN: adjustRate(-1); break;
        case ID_RATE_UP: adjustRate(+1); break;
        case ID_DEPTH_DN: adjustDepth(-1); break;
        case ID_DEPTH_UP: adjustDepth(+1); break;
        case ID_ENGINE: {
            int e = static_cast<int>(_cfg.engine) + 1;
            if (e > static_cast<int>(Engine::Cpu)) e = 0;
            _cfg.engine = static_cast<Engine>(e);
            selectEngine();
            if (_cfg.engine == Engine::Parlio && _activeEngine != Engine::Parlio) {
                toast("PARLIO unavailable: %s", _engineNote[0] ? _engineNote : "?");
            }
            break;
        }
        case ID_TRIGMODE: {
            int m = static_cast<int>(_trig.mode) + 1;
            if (m > static_cast<int>(TrigMode::Single)) m = 0;
            _trig.mode = static_cast<TrigMode>(m);
            break;
        }

        case ID_ZOOM_OUT: _wave.zoom(0.5, _wave.plotX() + _wave.plotW() / 2, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_ZOOM_IN:  _wave.zoom(2.0, _wave.plotX() + _wave.plotW() / 2, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_FIT:      _wave.fit(_buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_AUTO:
            autoScale(_buf.count());
            if (_info.triggerIndex >= 0) {
                const double screenSamples = _wave.plotW() * _wave.samplesPerPixel();
                _wave.centerOn(_info.triggerIndex +
                                   screenSamples * (0.5 - _trig.posPercent / 100.0),
                               _buf.count());
            }
            _dirtyWave = _dirtyPanel = true;
            break;
        case ID_HOME:     _wave.centerOn(_wave.plotW() * _wave.samplesPerPixel() * 0.5, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_PAGE_L:   _wave.panPixels(_wave.plotW() * 0.5, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_PAGE_R:   _wave.panPixels(-_wave.plotW() * 0.5, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_END:      _wave.centerOn(_buf.count() - _wave.plotW() * _wave.samplesPerPixel() * 0.5, _buf.count()); _dirtyWave = _dirtyPanel = true; break;
        case ID_GOTRIG:
            if (_info.triggerIndex >= 0) {
                _wave.centerOn(static_cast<double>(_info.triggerIndex), _buf.count());
                _dirtyWave = _dirtyPanel = true;
            }
            break;
        case ID_CURA: _activeCursor = 0; break;
        case ID_CURB: _activeCursor = 1; break;
        case ID_CURCLR:
            _wave.cursorA = _wave.cursorB = -1;
            _dirtyWave = _dirtyPanel = true;
            break;

        case ID_OV_TRIG: _overlay = _overlay == Overlay::Trigger ? Overlay::None : Overlay::Trigger; _dirtyWave = _dirtyPanel = true; break;
        case ID_OV_CHAN: _overlay = _overlay == Overlay::Channels ? Overlay::None : Overlay::Channels; _dirtyWave = _dirtyPanel = true; break;
        case ID_OV_DEC:  _overlay = _overlay == Overlay::Decode ? Overlay::None : Overlay::Decode; _dirtyWave = _dirtyPanel = true; break;
        case ID_OV_SAVE: _overlay = _overlay == Overlay::Storage ? Overlay::None : Overlay::Storage; _dirtyWave = _dirtyPanel = true; break;
        case ID_OV_INFO: _overlay = _overlay == Overlay::Info ? Overlay::None : Overlay::Info; _dirtyWave = _dirtyPanel = true; break;
        case ID_CLOSE:   _overlay = Overlay::None; _dirtyWave = _dirtyPanel = true; break;

        case ID_TM_AUTO:   _trig.mode = TrigMode::Auto; break;
        case ID_TM_NORMAL: _trig.mode = TrigMode::Normal; break;
        case ID_TM_SINGLE: _trig.mode = TrigMode::Single; break;
        case ID_TRIGPOS_DN:
            _trig.posPercent = _trig.posPercent >= 5 ? _trig.posPercent - 5 : 0;
            break;
        case ID_TRIGPOS_UP:
            _trig.posPercent = _trig.posPercent <= 90 ? _trig.posPercent + 5 : 95;
            break;
        case ID_TRIGCLR:
            for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) _trig.cond[ch] = TrigCond::Ignore;
            break;

        case ID_DEC_RUN:
            runDecoderNow();
            toast("decoded %u annotations", (unsigned)_anns.size());
            _dirtyWave = _dirtyPanel = true;
            break;

        case ID_SD_MOUNT:
            if (_sd.mounted()) { _sd.unmount(); toast("SD unmounted"); }
            else if (_sd.mount()) toast("SD mounted");
            else toast("SD: %s", _sd.lastError());
            break;
        case ID_SD_CSV: {
            const uint32_t first = _wave.start() < 0 ? 0 : (uint32_t)_wave.start();
            const uint32_t len = (uint32_t)(_wave.plotW() * _wave.samplesPerPixel());
            if (_sd.writeCsv(_buf, _info, _chan, first, len ? len : 1))
                toast("saved %s", _sd.lastPath());
            else toast("CSV: %s", _sd.lastError());
            break;
        }
        case ID_SD_VCD:
            if (_sd.writeVcd(_buf, _info, _chan)) toast("saved %s", _sd.lastPath());
            else toast("VCD: %s", _sd.lastError());
            break;
        case ID_SD_DEC: {
            const char* names[] = {"none", "uart", "i2c", "spi"};
            if (_sd.writeAnnotations(_anns, _info, names[(int)_dec.kind]))
                toast("saved %s", _sd.lastPath());
            else toast("decode: %s", _sd.lastError());
            break;
        }
        case ID_SD_SHOT:
            if (_sd.writeScreenshot()) toast("saved %s", _sd.lastPath());
            else toast("shot: %s", _sd.lastError());
            break;

        default:
            break;
    }
}

void App::adjustRate(int delta) {
    const int wasRunning = (_state == CaptureState::Sampling);
    _rateIndex += delta;
    applyConfig();
    if (!wasRunning) reanalyze();
}

void App::adjustDepth(int delta) {
    _depthShift += delta;
    applyConfig();
}

// ---------------------------------------------------------------------------
//  Drawing
// ---------------------------------------------------------------------------
void App::toast(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_toast, sizeof(_toast), fmt, ap);
    va_end(ap);
    _toastMs = millis();
    _dirtyPanel = true;
}

void App::drawButton(const Button& b) {
    auto& d = M5.Display;
    const uint16_t base = b.tint ? b.tint : kPanelEdge;
    const uint16_t fill = b.active ? base : kPanel;
    const uint16_t fg = b.active ? kBg : (b.enabled ? kText : kTextDim);

    d.fillRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 6, fill);
    d.drawRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 6,
                    b.enabled ? base : kGrid);
    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(fg, fill);
    d.drawString(b.text, b.r.x + b.r.w / 2, b.r.y + b.r.h / 2);
}

void App::drawTopBar() {
    auto& d = M5.Display;
    d.fillRect(_rTop.x, _rTop.y, _rTop.w, _rTop.h, kBg);
    d.drawFastHLine(0, _rTop.h - 1, _rTop.w, kPanelEdge);

    for (int i = 0; i < _btnCount; ++i) {
        if (_btn[i].r.y < _rTop.h) drawButton(_btn[i]);
    }

    // Status readout on the right.
    char line[96], hz[24];
    formatHz(_info.actualRateHz, hz, sizeof(hz));
    const char* st = "IDLE";
    uint16_t stc = kTextDim;
    switch (_state) {
        case CaptureState::Sampling: st = "SAMPLING"; stc = kGood; break;
        case CaptureState::Done:     st = _info.triggerIndex >= 0 ? "TRIG'D" : "UNTRIG"; stc = _info.triggerIndex >= 0 ? kAccent : kWarn; break;
        case CaptureState::Failed:   st = "FAILED"; stc = kBad; break;
        default: break;
    }
    snprintf(line, sizeof(line), "%s  %s @ %s  #%u",
             engineName(_activeEngine), st, hz, (unsigned)_captureCount);
    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::middle_right);
    d.setTextColor(stc, kBg);
    d.drawString(line, _rTop.w - 12, _rTop.h / 2);
}

void App::drawBottomBar() {
    auto& d = M5.Display;
    d.fillRect(_rBottom.x, _rBottom.y, _rBottom.w, _rBottom.h, kBg);
    d.drawFastHLine(_rBottom.x, _rBottom.y, _rBottom.w, kPanelEdge);
    for (int i = 0; i < _btnCount; ++i) {
        if (_btn[i].r.y >= _rBottom.y) drawButton(_btn[i]);
    }
}

void App::drawPanel() {
    auto& d = M5.Display;
    d.fillRect(_rPanel.x, _rPanel.y, _rPanel.w, _rPanel.h, kBg);
    d.drawFastHLine(_rPanel.x, _rPanel.y, _rPanel.w, kPanelEdge);

    const double sps = _info.secondsPerSample();
    char a[24], b[24], c[64];

    // --- cursor block -----------------------------------------------------
    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::top_left);
    d.setTextColor(kTextDim, kBg);
    d.drawString("CURSORS", 12, _rPanel.y + 8);

    const int64_t origin = _info.triggerIndex >= 0 ? _info.triggerIndex : 0;
    int y = _rPanel.y + 30;
    if (_wave.cursorA >= 0) {
        formatSeconds((_wave.cursorA - origin) * sps, a, sizeof(a));
        d.setTextColor(kCursorA, kBg);
        snprintf(c, sizeof(c), "A  %s", a);
        d.drawString(c, 12, y);
    }
    y += 22;
    if (_wave.cursorB >= 0) {
        formatSeconds((_wave.cursorB - origin) * sps, b, sizeof(b));
        d.setTextColor(kCursorB, kBg);
        snprintf(c, sizeof(c), "B  %s", b);
        d.drawString(c, 12, y);
    }
    y += 22;
    if (_wave.cursorA >= 0 && _wave.cursorB >= 0) {
        const double dt = (_wave.cursorB - _wave.cursorA) * sps;
        formatSeconds(dt, a, sizeof(a));
        formatHz(dt != 0 ? 1.0 / (dt < 0 ? -dt : dt) : 0, b, sizeof(b));
        d.setTextColor(kText, kBg);
        snprintf(c, sizeof(c), "dt %s", a);
        d.drawString(c, 12, y);
        d.drawString(b, 12, y + 22);
    }

    // --- measurement table ------------------------------------------------
    d.setTextColor(kTextDim, kBg);
    d.drawString("MEASUREMENTS", 300, _rPanel.y + 8);
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        const int col = ch / 4;
        const int row = ch % 4;
        const int tx = 300 + col * 330;
        const int ty = _rPanel.y + 30 + row * 26;
        const ChannelStats& s = _stats[ch];
        d.setTextColor(kChannel[ch], kBg);
        d.drawString(_chan[ch].name, tx, ty);
        d.setTextColor(_chan[ch].enabled ? kText : kTextDim, kBg);
        if (s.valid && s.freqHz > 0) {
            formatHz(s.freqHz, a, sizeof(a));
            snprintf(c, sizeof(c), "%s  %.1f%%  %u edges", a, s.dutyPercent,
                     (unsigned)s.edges);
        } else if (s.edges > 0) {
            formatSeconds(s.minHighSec > 0 ? s.minHighSec : s.minLowSec, a, sizeof(a));
            snprintf(c, sizeof(c), "%u edges  min %s", (unsigned)s.edges, a);
        } else {
            snprintf(c, sizeof(c), "static %s", s.highRatio > 0.5 ? "high" : "low");
        }
        d.drawString(c, tx + 46, ty);
    }

    // --- status / toast ---------------------------------------------------
    d.setTextColor(kTextDim, kBg);
    d.drawString("STATUS", 980, _rPanel.y + 8);
    d.setTextColor(kText, kBg);
    char depth[24];
    formatCount(_info.samples, depth, sizeof(depth));
    snprintf(c, sizeof(c), "%sSa captured", depth);
    d.drawString(c, 980, _rPanel.y + 30);
    formatSeconds(_info.samples * sps, a, sizeof(a));
    snprintf(c, sizeof(c), "window %s", a);
    d.drawString(c, 980, _rPanel.y + 52);
    if (_dec.kind != DecoderKind::None) {
        snprintf(c, sizeof(c), "decode: %u items", (unsigned)_anns.size());
        d.drawString(c, 980, _rPanel.y + 74);
    }
    if (_toast[0]) {
        d.setTextColor(kWarn, kBg);
        d.setTextDatum(textdatum_t::top_left);
        d.drawString(_toast, 980, _rPanel.y + 100);
    }
}

void App::drawOverlay() {
    if (_overlay == Overlay::None) return;
    auto& d = M5.Display;
    const Rect& o = _rOverlay;

    d.fillRoundRect(o.x, o.y, o.w, o.h, 12, kPanel);
    d.drawRoundRect(o.x, o.y, o.w, o.h, 12, kAccent);

    const char* title = "";
    switch (_overlay) {
        case Overlay::Trigger:  title = "TRIGGER"; break;
        case Overlay::Channels: title = "CHANNELS"; break;
        case Overlay::Decode:   title = "PROTOCOL DECODER"; break;
        case Overlay::Storage:  title = "SAVE TO microSD"; break;
        case Overlay::Info:     title = "SYSTEM INFO"; break;
        default: break;
    }
    d.setFont(&fonts::Font4);
    d.setTextDatum(textdatum_t::top_left);
    d.setTextColor(kAccent, kPanel);
    d.drawString(title, o.x + 24, o.y + 16);

    for (int i = 0; i < _btnCount; ++i) {
        if (_rOverlay.contains(_btn[i].r.x, _btn[i].r.y)) drawButton(_btn[i]);
    }

    d.setFont(&fonts::Font2);
    d.setTextDatum(textdatum_t::top_left);
    d.setTextColor(kTextDim, kPanel);

    char line[96];
    if (_overlay == Overlay::Storage) {
        d.drawString(_sd.mounted() ? "card ready" : "card not mounted",
                     o.x + 250, o.y + 96);
        if (_sd.lastPath()[0]) {
            snprintf(line, sizeof(line), "last file: %s", _sd.lastPath());
            d.drawString(line, o.x + 24, o.y + o.h - 100);
        }
        if (_sd.lastError()[0]) {
            d.setTextColor(kBad, kPanel);
            snprintf(line, sizeof(line), "error: %s", _sd.lastError());
            d.drawString(line, o.x + 24, o.y + o.h - 76);
        }
        d.setTextColor(kTextDim, kPanel);
        d.drawString("CSV covers the visible window; VCD covers the whole capture.",
                     o.x + 24, o.y + o.h - 44);
    } else if (_overlay == Overlay::Info) {
        int y = o.y + 76;
        auto put = [&](const char* fmt, ...) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(line, sizeof(line), fmt, ap);
            va_end(ap);
            d.drawString(line, o.x + 24, y);
            y += 26;
        };
        d.setTextColor(kText, kPanel);
        put("firmware       %s", LA_BUILD_VERSION);
        put("board          %d (M5Unified board id)", (int)M5.getBoard());
        put("CPU            %u MHz", (unsigned)getCpuFrequencyMhz());
        put("engine         %s%s%s", engineName(_activeEngine),
            _engineNote[0] ? "  -  " : "", _engineNote);
        put("PARLIO built   %s", LA_HAVE_PARLIO ? "yes" : "no");
        if (_sampler) {
            char how[80] = {0};
            _sampler->describeLast(how, sizeof(how));
            if (how[0]) put("last capture   %s", how);
            const uint32_t ll = _sampler->losslessDepth();
            if (ll) put("no-copy depth  %u kSa (faster above this is lossy)",
                        (unsigned)(ll / 1024));
        }
        put("free PSRAM     %u kB",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        put("free internal  %u kB",
            (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        put("capture buffer %u kB", (unsigned)(_buf.capacity() / 1024));
        y += 10;
        d.setTextColor(kTextDim, kPanel);
        put("probe pins");
        d.setTextColor(kText, kPanel);
        for (int ch = 0; ch < LA_MAX_CHANNELS; ch += 4) {
            put("  CH%d=G%d  CH%d=G%d  CH%d=G%d  CH%d=G%d",
                ch, (int)kChannelPin[ch], ch + 1, (int)kChannelPin[ch + 1],
                ch + 2, (int)kChannelPin[ch + 2], ch + 3, (int)kChannelPin[ch + 3]);
        }
    } else if (_overlay == Overlay::Decode) {
        snprintf(line, sizeof(line),
                 "%u annotations from %u samples%s",
                 (unsigned)_anns.size(), (unsigned)_buf.count(),
                 _anns.truncated() ? " (truncated)" : "");
        d.drawString(line, o.x + 24, o.y + o.h - 44);
    } else if (_overlay == Overlay::Trigger) {
        d.drawString("Tap a channel to cycle: --  Rise  Fall  Both  High  Low."
                     "  Edges are OR'ed, levels are AND'ed.",
                     o.x + 24, o.y + o.h - 44);
    }
}

void App::drawAll() {
    auto& d = M5.Display;

    // handleTouch() built the button array *before* dispatching this frame's
    // input, so it still describes the pre-click state.  Rebuild it here or the
    // frame right after every tap renders stale labels and highlights.
    _btnCount = 0;
    buildTopBar();
    buildBottomBar();
    buildOverlay();

    if (_dirtyChrome) {
        drawTopBar();
        drawBottomBar();
    }
    if (_dirtyWave) {
        // Channel name gutter
        d.fillRect(0, _rWave.y, kLabelW, _rWave.h, kPanel);
        d.drawFastVLine(kLabelW - 1, _rWave.y, _rWave.h, kPanelEdge);

        int enabled = 0;
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            if (_chan[ch].enabled) enabled++;
        }
        if (enabled == 0) enabled = 1;
        const int lane = _wave.laneHeight(enabled);
        int laneY = _rWave.y + kRulerH;
        d.setFont(&fonts::Font2);
        d.setTextDatum(textdatum_t::middle_left);
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            if (!_chan[ch].enabled) continue;
            d.setTextColor(kChannel[ch], kPanel);
            d.drawString(_chan[ch].name, 8, laneY + lane / 2 - 8);
            d.setTextColor(kTextDim, kPanel);
            char p[12];
            snprintf(p, sizeof(p), "G%d%s", (int)kChannelPin[ch],
                     _chan[ch].invert ? " inv" : "");
            d.drawString(p, 8, laneY + lane / 2 + 8);
            laneY += lane;
        }

        _wave.draw(d, _buf, _info, _chan,
                   _dec.kind != DecoderKind::None ? &_anns : nullptr);
    }
    if (_dirtyPanel) drawPanel();

    if ((_dirtyChrome || _dirtyWave || _dirtyPanel) && _overlay != Overlay::None) {
        drawOverlay();
    }
    _dirtyChrome = _dirtyWave = _dirtyPanel = false;
}
