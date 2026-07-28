#include "exporter.h"

#include <M5Unified.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>

namespace {

int8_t pinOr(m5::pin_name_t name, int8_t fallback) {
    const int8_t p = M5.getPin(name);
    return p >= 0 ? p : fallback;
}

// Picks the coarsest VCD time unit that still keeps at least ~10 steps per
// sample, so timestamps stay small without losing resolution.
struct VcdScale {
    uint64_t    unitPs;
    const char* text;
};

VcdScale pickVcdScale(double secondsPerSample) {
    static const VcdScale table[] = {
        {1ULL,             "1ps"},   {10ULL,            "10ps"},
        {100ULL,           "100ps"}, {1000ULL,          "1ns"},
        {10000ULL,         "10ns"},  {100000ULL,        "100ns"},
        {1000000ULL,       "1us"},   {10000000ULL,      "10us"},
        {100000000ULL,     "100us"}, {1000000000ULL,    "1ms"},
    };
    const double periodPs = secondsPerSample * 1e12;
    VcdScale best = table[0];
    for (const auto& s : table) {
        if (static_cast<double>(s.unitPs) * 10.0 <= periodPs) best = s;
    }
    return best;
}

const char kVcdIds[] = "!\"#$%&'()";   // one printable identifier per channel

}  // namespace

bool Exporter::mount() {
    if (_mounted) return true;

    const int8_t clk = pinOr(m5::pin_name_t::sd_mmc_clk, LA_SD_CLK);
    const int8_t cmd = pinOr(m5::pin_name_t::sd_mmc_cmd, LA_SD_CMD);
    const int8_t d0  = pinOr(m5::pin_name_t::sd_mmc_d0,  LA_SD_D0);
    const int8_t d1  = pinOr(m5::pin_name_t::sd_mmc_d1,  LA_SD_D1);
    const int8_t d2  = pinOr(m5::pin_name_t::sd_mmc_d2,  LA_SD_D2);
    const int8_t d3  = pinOr(m5::pin_name_t::sd_mmc_d3,  LA_SD_D3);

    if (!SD_MMC.setPins(clk, cmd, d0, d1, d2, d3)) {
        snprintf(_err, sizeof(_err), "SD_MMC.setPins failed");
        return false;
    }
    if (!SD_MMC.begin("/sdcard", false, false, BOARD_MAX_SDMMC_FREQ, 5)) {
        snprintf(_err, sizeof(_err), "no card / mount failed");
        return false;
    }
    _mounted = true;
    _err[0] = 0;
    ensureDir();
    return true;
}

void Exporter::unmount() {
    if (_mounted) SD_MMC.end();
    _mounted = false;
}

bool Exporter::ensureDir() {
    if (!SD_MMC.exists(LA_EXPORT_DIR)) {
        if (!SD_MMC.mkdir(LA_EXPORT_DIR)) {
            snprintf(_err, sizeof(_err), "cannot create " LA_EXPORT_DIR);
            return false;
        }
    }
    return true;
}

bool Exporter::nextPath(const char* prefix, const char* ext) {
    if (!ensureDir()) return false;
    for (uint32_t i = 0; i < 10000; ++i) {
        snprintf(_lastPath, sizeof(_lastPath), "%s/%s%04u.%s",
                 LA_EXPORT_DIR, prefix, (unsigned)((_seq + i) % 10000), ext);
        if (!SD_MMC.exists(_lastPath)) {
            _seq = (_seq + i + 1) % 10000;
            return true;
        }
    }
    snprintf(_err, sizeof(_err), "no free filename");
    return false;
}

bool Exporter::writeCsv(const CaptureBuffer& buf, const CaptureInfo& info,
                        const ChannelConfig channels[LA_MAX_CHANNELS],
                        uint32_t first, uint32_t len) {
    if (!mount()) return false;
    if (buf.count() == 0) { snprintf(_err, sizeof(_err), "nothing captured"); return false; }
    if (first >= buf.count()) first = 0;
    if (first + len > buf.count()) len = buf.count() - first;
    if (!nextPath("cap", "csv")) return false;

    File f = SD_MMC.open(_lastPath, FILE_WRITE);
    if (!f) { snprintf(_err, sizeof(_err), "open failed"); return false; }

    char line[160];
    int n = snprintf(line, sizeof(line), "time_s");
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!channels[ch].enabled) continue;
        n += snprintf(line + n, sizeof(line) - n, ",%s",
                      channels[ch].name[0] ? channels[ch].name : "CH");
    }
    n += snprintf(line + n, sizeof(line) - n, "\n");
    f.write(reinterpret_cast<const uint8_t*>(line), n);

    const double sps = info.secondsPerSample();
    const int64_t origin = info.triggerIndex >= 0 ? info.triggerIndex : 0;
    const uint8_t* p = buf.data();

    for (uint32_t i = 0; i < len; ++i) {
        const uint32_t idx = first + i;
        const double t = (static_cast<int64_t>(idx) - origin) * sps;
        n = snprintf(line, sizeof(line), "%.9f", t);
        const uint8_t v = p[idx];
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            if (!channels[ch].enabled) continue;
            const int bit = ((v >> ch) & 1u) ^ (channels[ch].invert ? 1 : 0);
            n += snprintf(line + n, sizeof(line) - n, ",%d", bit);
        }
        n += snprintf(line + n, sizeof(line) - n, "\n");
        if (f.write(reinterpret_cast<const uint8_t*>(line), n) != (size_t)n) {
            snprintf(_err, sizeof(_err), "write failed (card full?)");
            f.close();
            return false;
        }
    }
    f.close();
    _err[0] = 0;
    return true;
}

bool Exporter::writeVcd(const CaptureBuffer& buf, const CaptureInfo& info,
                        const ChannelConfig channels[LA_MAX_CHANNELS]) {
    if (!mount()) return false;
    if (buf.count() == 0) { snprintf(_err, sizeof(_err), "nothing captured"); return false; }
    if (!nextPath("cap", "vcd")) return false;

    File f = SD_MMC.open(_lastPath, FILE_WRITE);
    if (!f) { snprintf(_err, sizeof(_err), "open failed"); return false; }

    const double sps = info.secondsPerSample();
    const VcdScale scale = pickVcdScale(sps);
    const double ticksPerSample =
        sps * 1e12 / static_cast<double>(scale.unitPs);

    char line[128];
    int n = snprintf(line, sizeof(line),
                     "$version M5Tab5 Logic Analyzer " LA_BUILD_VERSION " $end\n"
                     "$timescale %s $end\n"
                     "$scope module logic $end\n", scale.text);
    f.write(reinterpret_cast<const uint8_t*>(line), n);

    uint8_t activeMask = 0;
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!channels[ch].enabled) continue;
        activeMask |= static_cast<uint8_t>(1u << ch);
        n = snprintf(line, sizeof(line), "$var wire 1 %c %s $end\n",
                     kVcdIds[ch],
                     channels[ch].name[0] ? channels[ch].name : "CH");
        f.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    n = snprintf(line, sizeof(line), "$upscope $end\n$enddefinitions $end\n");
    f.write(reinterpret_cast<const uint8_t*>(line), n);

    const uint8_t* p = buf.data();
    const uint32_t count = buf.count();

    uint8_t invMask = 0;
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (channels[ch].invert) invMask |= static_cast<uint8_t>(1u << ch);
    }

    // Initial state at t=0.
    uint8_t prev = static_cast<uint8_t>(p[0] ^ invMask);
    n = snprintf(line, sizeof(line), "#0\n");
    f.write(reinterpret_cast<const uint8_t*>(line), n);
    for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
        if (!(activeMask & (1u << ch))) continue;
        n = snprintf(line, sizeof(line), "%d%c\n", (prev >> ch) & 1u, kVcdIds[ch]);
        f.write(reinterpret_cast<const uint8_t*>(line), n);
    }

    for (uint32_t i = 1; i < count; ++i) {
        const uint8_t cur = static_cast<uint8_t>(p[i] ^ invMask);
        uint8_t changed = static_cast<uint8_t>((cur ^ prev) & activeMask);
        if (!changed) continue;
        const uint64_t ticks =
            static_cast<uint64_t>(static_cast<double>(i) * ticksPerSample + 0.5);
        n = snprintf(line, sizeof(line), "#%llu\n", (unsigned long long)ticks);
        f.write(reinterpret_cast<const uint8_t*>(line), n);
        while (changed) {
            const int ch = __builtin_ctz(changed);
            changed = static_cast<uint8_t>(changed & (changed - 1));
            n = snprintf(line, sizeof(line), "%d%c\n", (cur >> ch) & 1u, kVcdIds[ch]);
            f.write(reinterpret_cast<const uint8_t*>(line), n);
        }
        prev = cur;
    }
    f.close();
    _err[0] = 0;
    return true;
}

bool Exporter::writeAnnotations(const AnnotationList& anns, const CaptureInfo& info,
                                const char* decoderName) {
    if (!mount()) return false;
    if (anns.size() == 0) { snprintf(_err, sizeof(_err), "nothing decoded"); return false; }
    if (!nextPath("dec", "txt")) return false;

    File f = SD_MMC.open(_lastPath, FILE_WRITE);
    if (!f) { snprintf(_err, sizeof(_err), "open failed"); return false; }

    const double sps = info.secondsPerSample();
    const int64_t origin = info.triggerIndex >= 0 ? info.triggerIndex : 0;

    char line[128];
    int n = snprintf(line, sizeof(line), "# decoder\t%s\n# time_s\trow\ttext\n",
                     decoderName ? decoderName : "?");
    f.write(reinterpret_cast<const uint8_t*>(line), n);

    for (uint32_t i = 0; i < anns.size(); ++i) {
        const Annotation& a = anns[i];
        const double t = (static_cast<int64_t>(a.startSample) - origin) * sps;
        n = snprintf(line, sizeof(line), "%.9f\t%u\t%s\n", t, (unsigned)a.row, a.text);
        f.write(reinterpret_cast<const uint8_t*>(line), n);
    }
    f.close();
    _err[0] = 0;
    return true;
}

bool Exporter::writeScreenshot() {
    if (!mount()) return false;
    if (!nextPath("shot", "bmp")) return false;

    const int w = M5.Display.width();
    const int h = M5.Display.height();
    const int rowBytes = (w * 3 + 3) & ~3;

    File f = SD_MMC.open(_lastPath, FILE_WRITE);
    if (!f) { snprintf(_err, sizeof(_err), "open failed"); return false; }

    const uint32_t dataSize = static_cast<uint32_t>(rowBytes) * h;
    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    const uint32_t fileSize = 54 + dataSize;
    memcpy(&header[2], &fileSize, 4);
    const uint32_t offset = 54;
    memcpy(&header[10], &offset, 4);
    const uint32_t dibSize = 40;
    memcpy(&header[14], &dibSize, 4);
    memcpy(&header[18], &w, 4);
    memcpy(&header[22], &h, 4);
    header[26] = 1;                       // planes
    header[28] = 24;                      // bits per pixel
    memcpy(&header[34], &dataSize, 4);
    f.write(header, sizeof(header));

    auto* rgb565 = static_cast<uint16_t*>(heap_caps_malloc(w * 2, MALLOC_CAP_DEFAULT));
    auto* row = static_cast<uint8_t*>(heap_caps_malloc(rowBytes, MALLOC_CAP_DEFAULT));
    if (!rgb565 || !row) {
        if (rgb565) heap_caps_free(rgb565);
        if (row) heap_caps_free(row);
        f.close();
        snprintf(_err, sizeof(_err), "no memory for screenshot");
        return false;
    }
    memset(row, 0, rowBytes);

    // BMP rows run bottom-up.
    for (int y = h - 1; y >= 0; --y) {
        M5.Display.readRect(0, y, w, 1, rgb565);
        for (int x = 0; x < w; ++x) {
            const uint16_t c = rgb565[x];
            row[x * 3 + 0] = static_cast<uint8_t>((c & 0x001F) << 3);          // B
            row[x * 3 + 1] = static_cast<uint8_t>((c & 0x07E0) >> 3);          // G
            row[x * 3 + 2] = static_cast<uint8_t>((c & 0xF800) >> 8);          // R
        }
        f.write(row, rowBytes);
    }
    heap_caps_free(rgb565);
    heap_caps_free(row);
    f.close();
    _err[0] = 0;
    return true;
}
