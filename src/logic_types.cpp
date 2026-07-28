#include "logic_types.h"

#include <math.h>
#include <stdio.h>

const char* engineName(Engine e) {
    switch (e) {
        case Engine::Parlio: return "PARLIO";
        case Engine::Cpu:    return "CPU";
        case Engine::Auto:
        default:             return "AUTO";
    }
}

const char* trigCondName(TrigCond c) {
    switch (c) {
        case TrigCond::Rising:  return "Rise";
        case TrigCond::Falling: return "Fall";
        case TrigCond::Either:  return "Both";
        case TrigCond::High:    return "High";
        case TrigCond::Low:     return "Low";
        case TrigCond::Ignore:
        default:                return "--";
    }
}

const char* trigModeName(TrigMode m) {
    switch (m) {
        case TrigMode::Normal: return "NORMAL";
        case TrigMode::Single: return "SINGLE";
        case TrigMode::Auto:
        default:               return "AUTO";
    }
}

void formatHz(double hz, char* buf, size_t len) {
    if (!(hz > 0) || !isfinite(hz)) { snprintf(buf, len, "--"); return; }
    if (hz >= 1e6)      snprintf(buf, len, "%.3f MHz", hz / 1e6);
    else if (hz >= 1e3) snprintf(buf, len, "%.3f kHz", hz / 1e3);
    else                snprintf(buf, len, "%.2f Hz", hz);
}

void formatSeconds(double s, char* buf, size_t len) {
    if (!isfinite(s)) { snprintf(buf, len, "--"); return; }
    const double a = fabs(s);
    if (a == 0.0)        snprintf(buf, len, "0");
    else if (a >= 1.0)   snprintf(buf, len, "%.3f s", s);
    else if (a >= 1e-3)  snprintf(buf, len, "%.3f ms", s * 1e3);
    else if (a >= 1e-6)  snprintf(buf, len, "%.3f us", s * 1e6);
    else                 snprintf(buf, len, "%.1f ns", s * 1e9);
}

void formatCount(uint64_t n, char* buf, size_t len) {
    if (n >= 1000000ULL) {
        if (n % 1000000ULL == 0) snprintf(buf, len, "%lluM", (unsigned long long)(n / 1000000ULL));
        else snprintf(buf, len, "%.2fM", n / 1e6);
    } else if (n >= 1000ULL) {
        if (n % 1000ULL == 0) snprintf(buf, len, "%lluk", (unsigned long long)(n / 1000ULL));
        else snprintf(buf, len, "%.1fk", n / 1e3);
    } else {
        snprintf(buf, len, "%llu", (unsigned long long)n);
    }
}
