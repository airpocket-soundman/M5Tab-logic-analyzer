// ---------------------------------------------------------------------------
//  app.h - application state machine and touch UI
// ---------------------------------------------------------------------------
#pragma once

#include "gfx.h"

#include "analysis/measure.h"
#include "analysis/trigger.h"
#include "capture/capture_buffer.h"
#include "capture/sampler.h"
#include "decode/decoder.h"
#include "export/exporter.h"
#include "logic_types.h"
#include "ui/waveform_view.h"

enum class Overlay : uint8_t {
    None = 0,
    Trigger,
    Channels,
    Decode,
    Storage,
    Info,
};

struct Button {
    Rect     r;
    int      id      = 0;
    bool     active  = false;   // drawn highlighted
    bool     enabled = true;
    uint16_t tint    = 0;       // 0 = default chrome colour
    char     text[24] = {0};
};

class App {
public:
    void begin();
    void loop();

#ifdef LA_SIMULATOR
    // Hooks used only by the browser preview (sim/), so it can start from a
    // state that matches its synthetic capture.  Not compiled into firmware.
    DecoderConfig& decoderConfig() { return _dec; }
    TriggerConfig& triggerConfig() { return _trig; }
    void simSingleShot() { startRun(TrigMode::Single); }
#endif

private:
    // ---- capture ----------------------------------------------------------
    void selectEngine();
    void startRun(TrigMode mode);
    void stopRun();
    void serviceCapture();
    void finishCapture(uint32_t got, double measuredRate);
    void reanalyze();
    void runDecoderNow();
    void autoScale(uint32_t sampleCount);

    // ---- ui ---------------------------------------------------------------
    void buildLayout();
    void addButton(int id, const Rect& r, const char* text,
                   bool active = false, bool enabled = true, uint16_t tint = 0);
    void buildTopBar();
    void buildBottomBar();
    void buildOverlay();
    void handleTouch();
    void onButton(int id);
    void onWaveTouch(int x, int y);

    void drawAll();
    void paintChromeBackground();
    void drawChrome();
    void drawTopBar();
    void drawBottomBar();
    void drawPanel();
    void drawOverlay();
    void drawButton(const Button& b);
    void drawField(const char* text, int x, int y, int w, uint16_t fg);
    void toast(const char* fmt, ...);

    void adjustRate(int delta);
    void adjustDepth(int delta);
    void applyConfig();

    // ---- serial control API (src/api/serial_api.cpp) ----------------------
    void pollSerialApi();
    void apiHandleLine(char* line);
    void apiPing();
    void apiStatus();
    void apiConfig(const char* line);
    void apiTrigger(const char* line);
    void apiChannel(const char* line);
    void apiDecode(const char* line);
    void apiStats();
    void apiAnnotations(const char* line);
    void apiEdges(const char* line);
    void apiRead(const char* line);
    void apiView(const char* line);
    void apiCursor(const char* line);
    void apiSave(const char* line);
    void apiGen(const char* line);
    void apiFail(const char* why);

    uint8_t _genMask = 0;   // probe pins currently driven by the test generator

    // ---- state ------------------------------------------------------------
    CaptureConfig  _cfg;
    TriggerConfig  _trig;
    ChannelConfig  _chan[LA_MAX_CHANNELS];
    DecoderConfig  _dec;
    CaptureBuffer  _buf;
    CaptureInfo    _info;
    AnnotationList _anns;
    ChannelStats   _stats[LA_MAX_CHANNELS];
    Exporter       _sd;
    WaveformView   _wave;

    ISampler*    _sampler       = nullptr;
    ISampler*    _parlio        = nullptr;
    ISampler*    _cpu           = nullptr;
    Engine       _activeEngine  = Engine::Cpu;
    const char*  _engineNote    = "";

    CaptureState _state         = CaptureState::Idle;
    bool         _continuous    = false;
    bool         _needFit       = true;   // next capture re-fits the viewport
    uint32_t     _runStartMs    = 0;
    uint32_t     _captureCount  = 0;

    Overlay      _overlay       = Overlay::None;
    int          _rateIndex     = 3;      // index into kRateMenu
    int          _depthShift    = 21;     // 1 << 21 = 2 MSa
    int          _activeCursor  = 0;      // 0 = A, 1 = B

    // Touch tracking
    bool     _dragging      = false;
    bool     _pinching      = false;
    bool     _touchConsumed = false;   // press already handled by a button
    int      _lastX      = 0;
    int      _lastY      = 0;
    int      _pressX     = 0;
    int      _pressY     = 0;
    uint32_t _pressMs    = 0;
    float    _pinchStart = 0;
    double   _pinchSpp   = 1.0;
    int      _pinchAnchor = 0;

    static constexpr int kMaxButtons = 96;
    Button _btn[kMaxButtons];
    int    _btnCount = 0;
    // Snapshot of what is currently on screen, so only buttons that actually
    // changed get repainted.  Clearing and redrawing a whole bar every capture
    // is what made the chrome flicker.
    Button _prevBtn[kMaxButtons];
    int    _prevBtnCount = 0;

    // Off-screen buffer for the plot; see gfx.h for why it exists.
    LaCanvas _canvas;
    bool     _canvasOk = false;

    Rect _rTop, _rWave, _rPanel, _rBottom, _rOverlay;

    char     _statusPrev[96] = {0};
    char     _toast[80] = {0};
    uint32_t _toastMs   = 0;

    // Serial API line assembly
    char _apiBuf[192] = {0};
    int  _apiLen      = 0;
    bool _apiOverflow = false;

    bool _panelBgDirty = true;
    bool _dirtyChrome = true;
    bool _dirtyWave   = true;
    bool _dirtyPanel  = true;
};
