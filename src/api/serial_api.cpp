// ---------------------------------------------------------------------------
//  serial_api.cpp - line based control API over the USB CDC port
// ---------------------------------------------------------------------------
//
//  Request : one line, "verb key=value key=value\n"
//  Response: exactly one line of JSON, always starting with {"ok":true|false
//
//  One line in, one line out, with no unsolicited output in between, so a host
//  script can treat the port as a synchronous request/response channel without
//  needing to frame or resynchronise.  The waveform buffer is never dumped
//  wholesale - `edges` returns a transition list, which is what an automated
//  caller almost always wants and is orders of magnitude smaller than the raw
//  samples.
//
//  Commands are only serviced between captures: capture() blocks for the whole
//  sweep by design, so a `stop` sent mid-sweep takes effect at the next one.
//
#include <Arduino.h>
#include <ctype.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"

#define LA_API_VERSION 1

namespace {

// Value of `key=` in `line`, or nullptr.  The key must sit at the start of the
// line or right after whitespace so that "baud" does not match "autobaud".
const char* findArg(const char* line, const char* key) {
    const size_t klen = strlen(key);
    for (const char* p = line; *p; ++p) {
        if (p != line && !isspace((unsigned char)p[-1])) continue;
        if (strncmp(p, key, klen) != 0) continue;
        if (p[klen] != '=') continue;
        return p + klen + 1;
    }
    return nullptr;
}

bool argU32(const char* line, const char* key, uint32_t* out) {
    const char* v = findArg(line, key);
    if (!v) return false;
    *out = static_cast<uint32_t>(strtoul(v, nullptr, 0));
    return true;
}

bool argI32(const char* line, const char* key, int32_t* out) {
    const char* v = findArg(line, key);
    if (!v) return false;
    *out = static_cast<int32_t>(strtol(v, nullptr, 0));
    return true;
}

bool argDouble(const char* line, const char* key, double* out) {
    const char* v = findArg(line, key);
    if (!v) return false;
    *out = strtod(v, nullptr);
    return true;
}

// Copies the token after `key=` (up to whitespace) into out, lowercased.
bool argWord(const char* line, const char* key, char* out, size_t n) {
    const char* v = findArg(line, key);
    if (!v) return false;
    size_t i = 0;
    while (v[i] && !isspace((unsigned char)v[i]) && i + 1 < n) {
        out[i] = static_cast<char>(tolower((unsigned char)v[i]));
        ++i;
    }
    out[i] = 0;
    return true;
}

bool wordIs(const char* w, const char* v) { return strcmp(w, v) == 0; }

void jsonEscape(const char* in, char* out, size_t n) {
    size_t o = 0;
    for (const char* p = in; *p && o + 2 < n; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = static_cast<char>(c);
        } else if (c < 0x20 || c > 0x7E) {
            if (o + 6 >= n) break;
            o += snprintf(out + o, n - o, "\\u%04x", c);
        } else {
            out[o++] = static_cast<char>(c);
        }
    }
    out[o] = 0;
}

const char* stateName(CaptureState s) {
    switch (s) {
        case CaptureState::Armed:     return "armed";
        case CaptureState::Sampling:  return "sampling";
        case CaptureState::Searching: return "searching";
        case CaptureState::Done:      return "done";
        case CaptureState::Failed:    return "failed";
        case CaptureState::Idle:
        default:                      return "idle";
    }
}

const char* condName(TrigCond c) {
    switch (c) {
        case TrigCond::Rising:  return "rise";
        case TrigCond::Falling: return "fall";
        case TrigCond::Either:  return "both";
        case TrigCond::High:    return "high";
        case TrigCond::Low:     return "low";
        case TrigCond::Ignore:
        default:                return "off";
    }
}

bool parseCond(const char* w, TrigCond* out) {
    if (wordIs(w, "off") || wordIs(w, "-")) { *out = TrigCond::Ignore;  return true; }
    if (wordIs(w, "rise"))                  { *out = TrigCond::Rising;  return true; }
    if (wordIs(w, "fall"))                  { *out = TrigCond::Falling; return true; }
    if (wordIs(w, "both"))                  { *out = TrigCond::Either;  return true; }
    if (wordIs(w, "high"))                  { *out = TrigCond::High;    return true; }
    if (wordIs(w, "low"))                   { *out = TrigCond::Low;     return true; }
    return false;
}

const char* kindName(DecoderKind k) {
    switch (k) {
        case DecoderKind::Uart: return "uart";
        case DecoderKind::I2c:  return "i2c";
        case DecoderKind::Spi:  return "spi";
        case DecoderKind::None:
        default:                return "off";
    }
}

const char* annKindName(AnnKind k) {
    switch (k) {
        case AnnKind::Data:    return "data";
        case AnnKind::Address: return "addr";
        case AnnKind::Ack:     return "ack";
        case AnnKind::Nak:     return "nak";
        case AnnKind::Error:   return "err";
        case AnnKind::Info:
        default:               return "info";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Line assembly
// ---------------------------------------------------------------------------
void App::pollSerialApi() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            _apiBuf[_apiLen] = 0;
            if (_apiOverflow) {
                apiFail("line too long");
            } else if (_apiLen > 0) {
                apiHandleLine(_apiBuf);
                // `status` runs to ~900 bytes across several printf calls.  The
                // CDC TX buffer is smaller than that, so without draining it
                // here a response can be truncated mid-object and the host sees
                // malformed JSON.
                Serial.flush();
            }
            _apiLen = 0;
            _apiOverflow = false;
            continue;
        }
        if (_apiLen < static_cast<int>(sizeof(_apiBuf)) - 1) {
            _apiBuf[_apiLen++] = static_cast<char>(c);
        } else {
            _apiOverflow = true;
        }
    }
}

void App::apiFail(const char* why) {
    char esc[96];
    jsonEscape(why ? why : "error", esc, sizeof(esc));
    Serial.printf("{\"ok\":false,\"err\":\"%s\"}\n", esc);
}

void App::apiHandleLine(char* line) {
    // Split the verb off the rest of the line.
    char verb[16] = {0};
    size_t i = 0;
    while (line[i] && !isspace((unsigned char)line[i]) && i + 1 < sizeof(verb)) {
        verb[i] = static_cast<char>(tolower((unsigned char)line[i]));
        ++i;
    }
    const char* args = line + i;

    if (wordIs(verb, "ping"))      { apiPing(); return; }
    if (wordIs(verb, "status"))    { apiStatus(); return; }
    if (wordIs(verb, "config"))    { apiConfig(args); return; }
    if (wordIs(verb, "trigger"))   { apiTrigger(args); return; }
    if (wordIs(verb, "channel"))   { apiChannel(args); return; }
    if (wordIs(verb, "decode"))    { apiDecode(args); return; }
    if (wordIs(verb, "stats"))     { apiStats(); return; }
    if (wordIs(verb, "ann"))       { apiAnnotations(args); return; }
    if (wordIs(verb, "edges"))     { apiEdges(args); return; }
    if (wordIs(verb, "read"))      { apiRead(args); return; }
    if (wordIs(verb, "view"))      { apiView(args); return; }
    if (wordIs(verb, "cursor"))    { apiCursor(args); return; }
    if (wordIs(verb, "save"))      { apiSave(args); return; }

    if (wordIs(verb, "gen"))       { apiGen(args); return; }

    if (wordIs(verb, "run"))    { startRun(_trig.mode == TrigMode::Single ? TrigMode::Auto : _trig.mode); apiStatus(); return; }
    if (wordIs(verb, "single")) { startRun(TrigMode::Single); apiStatus(); return; }
    if (wordIs(verb, "stop"))   { stopRun(); apiStatus(); return; }

    apiFail("unknown command");
}

// ---------------------------------------------------------------------------
//  Commands
// ---------------------------------------------------------------------------
void App::apiPing() {
    Serial.printf(
        "{\"ok\":true,\"fw\":\"%s\",\"api\":%d,\"board\":%d,\"channels\":%d,"
        "\"parlio_built\":%s,\"cpu_mhz\":%u}\n",
        LA_BUILD_VERSION, LA_API_VERSION, (int)M5.getBoard(), LA_MAX_CHANNELS,
        LA_HAVE_PARLIO ? "true" : "false", (unsigned)getCpuFrequencyMhz());
}

void App::apiStatus() {
    char note[80];
    jsonEscape(_engineNote ? _engineNote : "", note, sizeof(note));

    Serial.printf("{\"ok\":true,\"state\":\"%s\",\"engine\":\"%s\",\"engine_sel\":\"%s\","
                  "\"engine_note\":\"%s\",\"rate_req\":%u,\"rate_actual\":%.3f,"
                  "\"depth\":%u,\"samples\":%u,\"trigger\":%lld,\"captures\":%u,"
                  "\"trig_mode\":\"%s\",\"trig_pos\":%u,",
                  stateName(_state), engineName(_activeEngine),
                  engineName(_cfg.engine), note,
                  (unsigned)_cfg.rateHz, _info.actualRateHz,
                  (unsigned)_cfg.depth, (unsigned)_info.samples,
                  (long long)_info.triggerIndex, (unsigned)_captureCount,
                  trigModeName(_trig.mode), (unsigned)_trig.posPercent);

    Serial.flush();
    Serial.print("\"trig_cond\":[");
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        Serial.printf("%s\"%s\"", ch ? "," : "", condName(_trig.cond[ch]));
    }
    Serial.flush();
    Serial.print("],\"chan\":[");
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        Serial.printf("%s{\"pin\":%d,\"on\":%s,\"inv\":%s}", ch ? "," : "",
                      (int)kChannelPin[ch],
                      _chan[ch].enabled ? "true" : "false",
                      _chan[ch].invert ? "true" : "false");
    }
    char how[96] = {0}, howEsc[128];
    uint32_t lossless = 0;
    if (_sampler) {
        _sampler->describeLast(how, sizeof(how));
        lossless = _sampler->losslessDepth();
    }
    jsonEscape(how, howEsc, sizeof(howEsc));

    Serial.printf("],\"view\":{\"start\":%.2f,\"spp\":%.6f,\"w\":%d},"
                  "\"cursor\":{\"a\":%lld,\"b\":%lld},"
                  "\"decoder\":\"%s\",\"ann\":%u,\"sd\":%s,"
                  "\"capture_mode\":\"%s\",\"lossless_depth\":%u,"
                  "\"mem\":{\"psram_free\":%u,\"int_free\":%u}}\n",
                  _wave.start(), _wave.samplesPerPixel(), _wave.plotW(),
                  (long long)_wave.cursorA, (long long)_wave.cursorB,
                  kindName(_dec.kind), (unsigned)_anns.size(),
                  _sd.mounted() ? "true" : "false",
                  howEsc, (unsigned)lossless,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void App::apiConfig(const char* line) {
    uint32_t u = 0;
    char w[16];
    bool touched = false;

    if (argU32(line, "rate", &u) && u > 0) {
        // Snap to the nearest entry of the rate menu, so the UI and the API can
        // never disagree about what is selected.
        int best = 0;
        double bestErr = 1e30;
        for (int i = 0; i < kRateMenuCount; ++i) {
            const double err = fabs(log(static_cast<double>(kRateMenu[i]) / u));
            if (err < bestErr) { bestErr = err; best = i; }
        }
        _rateIndex = best;
        touched = true;
    }
    if (argU32(line, "depth", &u) && u > 0) {
        int shift = 16;
        while ((1u << shift) < u && shift < 23) shift++;
        _depthShift = shift;
        touched = true;
    }
    if (argWord(line, "engine", w, sizeof(w))) {
        if (wordIs(w, "auto")) _cfg.engine = Engine::Auto;
        else if (wordIs(w, "parlio")) _cfg.engine = Engine::Parlio;
        else if (wordIs(w, "cpu")) _cfg.engine = Engine::Cpu;
        else { apiFail("engine must be auto|parlio|cpu"); return; }
        touched = true;
    }
    if (touched) applyConfig();
    apiStatus();
}

void App::apiTrigger(const char* line) {
    char w[16];
    uint32_t u = 0;

    if (argWord(line, "clear", w, sizeof(w))) {
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) _trig.cond[ch] = TrigCond::Ignore;
    }
    if (argWord(line, "mode", w, sizeof(w))) {
        if (wordIs(w, "auto")) _trig.mode = TrigMode::Auto;
        else if (wordIs(w, "normal")) _trig.mode = TrigMode::Normal;
        else if (wordIs(w, "single")) _trig.mode = TrigMode::Single;
        else { apiFail("mode must be auto|normal|single"); return; }
    }
    if (argU32(line, "pos", &u)) {
        _trig.posPercent = static_cast<uint8_t>(u > 95 ? 95 : u);
    }
    if (argU32(line, "timeout", &u)) {
        _trig.normalTimeoutMs = u;
    }
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        char key[8];
        snprintf(key, sizeof(key), "ch%d", ch);
        if (!argWord(line, key, w, sizeof(w))) continue;
        TrigCond c;
        if (!parseCond(w, &c)) { apiFail("cond must be off|rise|fall|both|high|low"); return; }
        _trig.cond[ch] = c;
    }
    _dirtyChrome = true;
    apiStatus();
}

void App::apiChannel(const char* line) {
    int32_t n = -1;
    if (!argI32(line, "n", &n) || n < 0 || n >= LA_MAX_CHANNELS) {
        apiFail("n must be 0..7");
        return;
    }
    int32_t v = 0;
    if (argI32(line, "on", &v)) _chan[n].enabled = (v != 0);
    if (argI32(line, "inv", &v)) _chan[n].invert = (v != 0);
    _dirtyWave = _dirtyPanel = true;
    apiStatus();
}

void App::apiDecode(const char* line) {
    char w[16];
    int32_t v = 0;
    uint32_t u = 0;

    if (argWord(line, "kind", w, sizeof(w))) {
        if (wordIs(w, "off") || wordIs(w, "none")) _dec.kind = DecoderKind::None;
        else if (wordIs(w, "uart")) _dec.kind = DecoderKind::Uart;
        else if (wordIs(w, "i2c")) _dec.kind = DecoderKind::I2c;
        else if (wordIs(w, "spi")) _dec.kind = DecoderKind::Spi;
        else { apiFail("kind must be off|uart|i2c|spi"); return; }
    }

    if (_dec.kind == DecoderKind::Uart) {
        if (argI32(line, "line", &v)) _dec.uart.channel = static_cast<int8_t>(v);
        if (argWord(line, "baud", w, sizeof(w))) {
            if (wordIs(w, "auto")) _dec.uart.autoBaud = true;
            else { _dec.uart.autoBaud = false; _dec.uart.baud = strtoul(w, nullptr, 0); }
        }
        if (argU32(line, "bits", &u)) _dec.uart.dataBits = static_cast<uint8_t>(u);
        if (argWord(line, "parity", w, sizeof(w))) {
            _dec.uart.parity = static_cast<char>(toupper((unsigned char)w[0]));
        }
        if (argU32(line, "stop", &u)) _dec.uart.stopBits = static_cast<uint8_t>(u);
        if (argI32(line, "invert", &v)) _dec.uart.invert = (v != 0);
        if (argWord(line, "order", w, sizeof(w))) _dec.uart.lsbFirst = wordIs(w, "lsb");
    } else if (_dec.kind == DecoderKind::I2c) {
        if (argI32(line, "scl", &v)) _dec.i2c.sclChannel = static_cast<int8_t>(v);
        if (argI32(line, "sda", &v)) _dec.i2c.sdaChannel = static_cast<int8_t>(v);
        if (argI32(line, "acks", &v)) _dec.i2c.showAcks = (v != 0);
    } else if (_dec.kind == DecoderKind::Spi) {
        if (argI32(line, "clk", &v))  _dec.spi.clkChannel  = static_cast<int8_t>(v);
        if (argI32(line, "mosi", &v)) _dec.spi.mosiChannel = static_cast<int8_t>(v);
        if (argI32(line, "miso", &v)) _dec.spi.misoChannel = static_cast<int8_t>(v);
        if (argI32(line, "cs", &v))   _dec.spi.csChannel   = static_cast<int8_t>(v);
        if (argI32(line, "cpol", &v)) _dec.spi.cpol = (v != 0);
        if (argI32(line, "cpha", &v)) _dec.spi.cpha = (v != 0);
        if (argWord(line, "order", w, sizeof(w))) _dec.spi.msbFirst = wordIs(w, "msb");
        if (argU32(line, "bits", &u)) _dec.spi.wordBits = static_cast<uint8_t>(u);
    }

    runDecoderNow();
    _dirtyWave = _dirtyPanel = true;
    Serial.printf("{\"ok\":true,\"decoder\":\"%s\",\"ann\":%u,\"truncated\":%s}\n",
                  kindName(_dec.kind), (unsigned)_anns.size(),
                  _anns.truncated() ? "true" : "false");
}

void App::apiStats() {
    const double sps = _info.secondsPerSample();
    Serial.printf("{\"ok\":true,\"sec_per_sample\":%.12g,\"samples\":%u,\"stats\":[",
                  sps, (unsigned)_info.samples);
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        const ChannelStats& s = _stats[ch];
        Serial.printf("%s{\"ch\":%d,\"edges\":%u,\"rising\":%u,\"freq\":%.6g,"
                      "\"duty\":%.4g,\"min_high\":%.9g,\"min_low\":%.9g,"
                      "\"high_ratio\":%.6g}",
                      ch ? "," : "", ch, (unsigned)s.edges, (unsigned)s.risingEdges,
                      s.freqHz, s.dutyPercent, s.minHighSec, s.minLowSec,
                      s.highRatio);
        // This response runs past 800 bytes and the CDC transmit buffer is
        // smaller, so drain as we go.  One flush at the end of the command is
        // not enough - the tail comes back truncated and the host sees
        // malformed JSON.
        Serial.flush();
    }
    Serial.print("]}\n");
}

void App::apiAnnotations(const char* line) {
    uint32_t from = 0, count = 128;
    argU32(line, "from", &from);
    argU32(line, "count", &count);
    if (count > 512) count = 512;          // keep one response to a sane size

    const uint32_t total = _anns.size();
    if (from > total) from = total;
    uint32_t n = total - from;
    if (n > count) n = count;

    Serial.printf("{\"ok\":true,\"decoder\":\"%s\",\"total\":%u,\"from\":%u,\"ann\":[",
                  kindName(_dec.kind), (unsigned)total, (unsigned)from);
    char esc[64];
    for (uint32_t i = 0; i < n; ++i) {
        const Annotation& a = _anns[from + i];
        jsonEscape(a.text, esc, sizeof(esc));
        Serial.printf("%s{\"s\":%u,\"e\":%u,\"k\":\"%s\",\"r\":%u,\"t\":\"%s\"}",
                      i ? "," : "", (unsigned)a.startSample, (unsigned)a.endSample,
                      annKindName(a.kind), (unsigned)a.row, esc);
    }
    Serial.print("]}\n");
}

void App::apiEdges(const char* line) {
    int32_t ch = 0;
    uint32_t from = 0, count = 1024;
    argI32(line, "ch", &ch);
    argU32(line, "from", &from);
    argU32(line, "count", &count);
    if (ch < 0 || ch >= LA_MAX_CHANNELS) { apiFail("ch must be 0..7"); return; }
    if (count > 2048) count = 2048;

    const uint32_t total = _buf.count();
    if (total == 0) { apiFail("no capture"); return; }
    if (from >= total) from = total - 1;

    const uint8_t mask = static_cast<uint8_t>(1u << ch);
    const bool inv = _chan[ch].invert;
    const uint8_t* p = _buf.data();

    bool prev = ((p[from] & mask) != 0) != inv;
    Serial.printf("{\"ok\":true,\"ch\":%d,\"from\":%u,\"level\":%d,"
                  "\"sec_per_sample\":%.12g,\"edges\":[",
                  (int)ch, (unsigned)from, prev ? 1 : 0, _info.secondsPerSample());

    uint32_t emitted = 0;
    uint32_t i = from + 1;
    for (; i < total && emitted < count; ++i) {
        const bool cur = ((p[i] & mask) != 0) != inv;
        if (cur == prev) continue;
        Serial.printf("%s%u", emitted ? "," : "", (unsigned)i);
        prev = cur;
        emitted++;
    }
    Serial.printf("],\"next\":%u,\"more\":%s}\n",
                  (unsigned)i, (i < total) ? "true" : "false");
}

void App::apiRead(const char* line) {
    uint32_t from = 0, count = 256;
    argU32(line, "from", &from);
    argU32(line, "count", &count);
    if (count > 4096) count = 4096;

    const uint32_t total = _buf.count();
    if (total == 0) { apiFail("no capture"); return; }
    if (from >= total) { apiFail("from beyond capture"); return; }
    if (from + count > total) count = total - from;

    Serial.printf("{\"ok\":true,\"from\":%u,\"n\":%u,\"hex\":\"",
                  (unsigned)from, (unsigned)count);
    const uint8_t* p = _buf.data() + from;
    // Chunked so we never build a multi-kB string in RAM.
    char chunk[129];
    uint32_t done = 0;
    while (done < count) {
        uint32_t n = count - done;
        if (n > 64) n = 64;
        for (uint32_t i = 0; i < n; ++i) {
            static const char* hex = "0123456789abcdef";
            chunk[i * 2 + 0] = hex[p[done + i] >> 4];
            chunk[i * 2 + 1] = hex[p[done + i] & 0x0F];
        }
        chunk[n * 2] = 0;
        Serial.print(chunk);
        done += n;
    }
    Serial.print("\"}\n");
}

void App::apiView(const char* line) {
    char w[16];
    double d = 0;
    const uint32_t n = _buf.count();

    if (argWord(line, "fit", w, sizeof(w))) _wave.fit(n);
    if (argDouble(line, "zoom", &d) && d > 0) {
        _wave.zoom(d, _wave.plotX() + _wave.plotW() / 2, n);
    }
    if (argDouble(line, "center", &d)) _wave.centerOn(d, n);
    if (argWord(line, "trig", w, sizeof(w)) && _info.triggerIndex >= 0) {
        _wave.centerOn(static_cast<double>(_info.triggerIndex), n);
    }
    _dirtyWave = _dirtyPanel = true;
    Serial.printf("{\"ok\":true,\"start\":%.2f,\"spp\":%.6f,\"w\":%d,"
                  "\"span_samples\":%.1f}\n",
                  _wave.start(), _wave.samplesPerPixel(), _wave.plotW(),
                  _wave.plotW() * _wave.samplesPerPixel());
}

void App::apiCursor(const char* line) {
    char w[16];
    int32_t v = 0;
    if (argWord(line, "clear", w, sizeof(w))) {
        _wave.cursorA = _wave.cursorB = -1;
    }
    if (argI32(line, "a", &v)) _wave.cursorA = v;
    if (argI32(line, "b", &v)) _wave.cursorB = v;
    _dirtyWave = _dirtyPanel = true;

    const double sps = _info.secondsPerSample();
    double dt = 0;
    if (_wave.cursorA >= 0 && _wave.cursorB >= 0) {
        dt = (_wave.cursorB - _wave.cursorA) * sps;
    }
    Serial.printf("{\"ok\":true,\"a\":%lld,\"b\":%lld,\"dt\":%.12g,\"freq\":%.6g}\n",
                  (long long)_wave.cursorA, (long long)_wave.cursorB, dt,
                  dt != 0 ? 1.0 / fabs(dt) : 0.0);
}

// ---------------------------------------------------------------------------
//  Built-in test signal
// ---------------------------------------------------------------------------
//  Drives a probe pin with LEDC while PARLIO keeps sampling it, so the whole
//  chain - GPIO matrix, peripheral, DMA, copy, buffer - can be validated
//  against a known frequency without any external gear.  ledc_set_pin() puts
//  the pad in output-only mode, so the input path has to be switched back on
//  afterwards or the sampler would read a constant level.
// ---------------------------------------------------------------------------
void App::apiGen(const char* line) {
    char w[16];

    if (argWord(line, "off", w, sizeof(w))) {
        for (int i = 0; i < kMaxGenPins; ++i) {
            if (_genPins[i] < 0) continue;
            const gpio_num_t g = static_cast<gpio_num_t>(_genPins[i]);
            ledcDetach(_genPins[i]);
            // Detaching alone can leave the pad still driving.  That matters
            // when a spare pin is wired to a probe for a loopback test: a pin
            // left holding a level fights whatever drives the other end and the
            // capture reads a flat line.  Force it back to input only.
            gpio_set_direction(g, GPIO_MODE_INPUT);
            gpio_input_enable(g);
            _genPins[i] = -1;
        }
        Serial.print("{\"ok\":true,\"gen\":\"off\"}\n");
        return;
    }

    int32_t ch = -1, pinArg = -1;
    uint32_t freq = 1000000, duty = 50;
    argI32(line, "ch", &ch);
    argI32(line, "pin", &pinArg);
    argU32(line, "freq", &freq);
    argU32(line, "duty", &duty);

    int pin;
    if (pinArg >= 0) {
        pin = pinArg;                       // any GPIO, for external loopback
    } else if (ch >= 0 && ch < LA_MAX_CHANNELS) {
        pin = kChannelPin[ch];
    } else {
        apiFail("give ch=0..7 or pin=<gpio>");
        return;
    }
    if (freq == 0) { apiFail("freq must be > 0"); return; }
    if (duty > 100) duty = 100;

    // ledcAttach refuses a pin that is already driven, so a second `gen` on the
    // same pin would silently leave the old frequency running.
    int slot = -1;
    for (int i = 0; i < kMaxGenPins; ++i) {
        if (_genPins[i] == pin) {
            ledcDetach(pin);
            _genPins[i] = -1;
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < kMaxGenPins; ++i) {
            if (_genPins[i] < 0) { slot = i; break; }
        }
    }
    if (slot < 0) { apiFail("no free generator slot; send gen off=1"); return; }

    // LEDC counts to 2^resolution per period off its source clock, so the
    // resolution has to shrink as the frequency rises.  Which source the
    // hardware picks depends on the target, so start from the theoretical
    // best and walk down until the driver accepts it.
    int res = 14;
    while (res > 1 && (static_cast<uint64_t>(freq) << res) > 80000000ULL) res--;
    bool attached = false;
    for (; res >= 1; --res) {
        if (ledcAttach(pin, freq, static_cast<uint8_t>(res))) { attached = true; break; }
    }
    if (!attached) {
        apiFail("ledcAttach failed (frequency out of range?)");
        return;
    }
    const uint32_t maxDuty = (1u << res) - 1;
    ledcWrite(pin, (maxDuty * duty) / 100);
    // ledc_set_pin leaves the pad output-only; the sampler needs the input path
    // back on, and for an external loopback the driven pad may itself be probed.
    gpio_input_enable(static_cast<gpio_num_t>(pin));
    _genPins[slot] = static_cast<int8_t>(pin);

    // Report which probe channel, if any, sits on that pin.
    int onChannel = -1;
    for (int i = 0; i < LA_MAX_CHANNELS; ++i) {
        if (kChannelPin[i] == pin) { onChannel = i; break; }
    }
    Serial.printf("{\"ok\":true,\"gen\":\"on\",\"pin\":%d,\"channel\":%d,"
                  "\"freq\":%u,\"duty\":%u,\"res_bits\":%d}\n",
                  pin, onChannel, (unsigned)freq, (unsigned)duty, res);
}

void App::apiSave(const char* line) {
    char w[16];
    if (!argWord(line, "what", w, sizeof(w))) {
        apiFail("what must be vcd|csv|dec|png");
        return;
    }
    bool ok = false;
    if (wordIs(w, "vcd")) {
        ok = _sd.writeVcd(_buf, _info, _chan);
    } else if (wordIs(w, "csv")) {
        const uint32_t first = _wave.start() < 0 ? 0 : (uint32_t)_wave.start();
        const uint32_t len = (uint32_t)(_wave.plotW() * _wave.samplesPerPixel());
        ok = _sd.writeCsv(_buf, _info, _chan, first, len ? len : 1);
    } else if (wordIs(w, "dec")) {
        ok = _sd.writeAnnotations(_anns, _info, kindName(_dec.kind));
    } else if (wordIs(w, "png") || wordIs(w, "bmp") || wordIs(w, "shot")) {
        ok = _sd.writeScreenshot();
    } else {
        apiFail("what must be vcd|csv|dec|png");
        return;
    }
    if (!ok) { apiFail(_sd.lastError()); return; }
    char esc[64];
    jsonEscape(_sd.lastPath(), esc, sizeof(esc));
    Serial.printf("{\"ok\":true,\"path\":\"%s\"}\n", esc);
}
