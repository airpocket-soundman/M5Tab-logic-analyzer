// ---------------------------------------------------------------------------
//  shotgen.cpp - headless screenshot generator for the documentation
// ---------------------------------------------------------------------------
//
//  Runs exactly the same code as the browser preview - the same App, the same
//  renderer, the same synthetic capture - but against an off-screen RGB565
//  framebuffer and a scripted touch stream, then writes each frame out as a
//  BMP.  Driving it by script instead of by hand keeps the screenshots in the
//  documentation reproducible: rebuild and every image regenerates identically.
//
//  It implements the same `m5gfx_simulator_*` entry points the web runtime
//  provides, so nothing above this file knows the difference.
//
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static constexpr int32_t kW = 1280;
static constexpr int32_t kH = 720;
static uint16_t g_fb[kW * kH];

static uint32_t g_ms = 0;
static int32_t  g_touchX = 0, g_touchY = 0;
static bool     g_touchDown = false;

extern "C" {

void m5gfx_simulator_draw_pixel(int32_t x, int32_t y, uint16_t color) {
    if (x < 0 || y < 0 || x >= kW || y >= kH) return;
    g_fb[y * kW + x] = color;
}

uint16_t m5gfx_simulator_read_pixel(int32_t x, int32_t y) {
    if (x < 0 || y < 0 || x >= kW || y >= kH) return 0;
    return g_fb[y * kW + x];
}

void m5gfx_simulator_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > kW) w = kW - x;
    if (y + h > kH) h = kH - y;
    for (int32_t j = 0; j < h; ++j) {
        uint16_t* row = &g_fb[(y + j) * kW + x];
        for (int32_t i = 0; i < w; ++i) row[i] = color;
    }
}

int32_t m5gfx_simulator_width(void) { return kW; }
int32_t m5gfx_simulator_height(void) { return kH; }

int m5gfx_simulator_get_touch(int32_t* x, int32_t* y) {
    if (x) *x = g_touchX;
    if (y) *y = g_touchY;
    return g_touchDown ? 1 : 0;
}

uint32_t lvgl_web_simulator_elapsed_ms(void) { return g_ms; }

void setup(void);
void loop(void);

}  // extern "C"

// ---------------------------------------------------------------------------
//  Frame driving
// ---------------------------------------------------------------------------
static void frame(int n = 1) {
    for (int i = 0; i < n; ++i) {
        g_ms += 16;
        loop();
    }
}

// A tap has to span several frames: the UI only acts on the press edge, and the
// release edge has to be observed too or the next tap looks like a drag.
static void tap(int x, int y) {
    g_touchX = x;
    g_touchY = y;
    g_touchDown = true;
    frame(2);
    g_touchDown = false;
    frame(3);
}

static void tapN(int x, int y, int times) {
    for (int i = 0; i < times; ++i) tap(x, y);
}

// ---------------------------------------------------------------------------
//  BMP writer (24 bit, bottom-up, no compression)
// ---------------------------------------------------------------------------
static void writeBmp(const char* path) {
    const int rowBytes = (kW * 3 + 3) & ~3;
    const uint32_t dataSize = static_cast<uint32_t>(rowBytes) * kH;

    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return; }

    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    const uint32_t fileSize = 54 + dataSize;
    memcpy(&hdr[2], &fileSize, 4);
    const uint32_t offset = 54;
    memcpy(&hdr[10], &offset, 4);
    const uint32_t dib = 40;
    memcpy(&hdr[14], &dib, 4);
    const int32_t w = kW, h = kH;
    memcpy(&hdr[18], &w, 4);
    memcpy(&hdr[22], &h, 4);
    hdr[26] = 1;
    hdr[28] = 24;
    memcpy(&hdr[34], &dataSize, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    uint8_t* row = static_cast<uint8_t*>(calloc(1, rowBytes));
    for (int y = kH - 1; y >= 0; --y) {
        for (int x = 0; x < kW; ++x) {
            const uint16_t c = g_fb[y * kW + x];
            // Replicate the high bits into the low ones so full-scale channels
            // reach 0xFF instead of stopping short.
            const uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        fwrite(row, 1, rowBytes, f);
    }
    free(row);
    fclose(f);
    printf("wrote %s\n", path);
}

static char g_outDir[512] = ".";

static void shot(const char* name) {
    char path[640];
    snprintf(path, sizeof(path), "%s/%s.bmp", g_outDir, name);
    writeBmp(path);
}

// ---------------------------------------------------------------------------
//  Widget coordinates, mirroring the layout in app.cpp
// ---------------------------------------------------------------------------
namespace hit {
// top bar (y centre 26)
constexpr int kTopY = 26;
constexpr int kRun = 50, kSingle = 144;
constexpr int kRateDn = 220, kRateUp = 392;
constexpr int kDepthDn = 444, kDepthUp = 600;
constexpr int kEngine = 698, kTrigMode = 838;

// bottom bar (y centre 688)
constexpr int kBotY = 688;
constexpr int kZoomOut = 38, kZoomIn = 106, kFit = 170, kAuto = 230;
constexpr int kHome = 286, kPageL = 338, kPageR = 390, kEnd = 442;
constexpr int kGoTrig = 502, kCurA = 566, kCurB = 626, kCurClr = 686;
constexpr int kOvTrigger = 778, kOvChannels = 878, kOvDecode = 978;
constexpr int kOvSave = 1070, kOvInfo = 1150;

// overlay (940x560 centred in 1280x720 -> origin 170,80)
constexpr int kOx = 170, kOy = 80;
constexpr int kClose = kOx + 940 - 55, kCloseY = kOy + 30;
constexpr int kDecOff = kOx + 94, kDecUart = kOx + 244;
constexpr int kDecI2c = kOx + 394, kDecSpi = kOx + 544;
constexpr int kDecKindY = kOy + 92;
}  // namespace hit

int main(int argc, char** argv) {
    if (argc > 1) snprintf(g_outDir, sizeof(g_outDir), "%s", argv[1]);

    setup();
    frame(6);                       // let the first capture land and draw

    // --- overview at full capture depth ---------------------------------
    shot("01-overview");

    // --- shallower capture, then zoom in until the protocol detail reads --
    tapN(hit::kDepthDn, hit::kTopY, 5);      // 2 MSa  -> 64 kSa
    tapN(hit::kRateUp, hit::kTopY, 2);       // 1 MSa/s -> 5 MSa/s
    tap(hit::kSingle, hit::kTopY);
    frame(8);
    shot("02-capture-64k");

    // Five doublings puts roughly 400 us on screen: one I2C transaction, a few
    // UART frames and a couple of SPI bursts, all at once.
    tapN(hit::kZoomIn, hit::kBotY, 5);
    frame(4);
    shot("03-zoomed");

    // --- decoders --------------------------------------------------------
    tap(hit::kOvDecode, hit::kBotY);
    frame(3);
    shot("04-decode-panel");

    tap(hit::kDecI2c, hit::kDecKindY);
    frame(3);
    tap(hit::kClose, hit::kCloseY);
    frame(3);
    shot("05-decode-i2c");

    tap(hit::kOvDecode, hit::kBotY);
    frame(2);
    tap(hit::kDecUart, hit::kDecKindY);
    frame(3);
    tap(hit::kClose, hit::kCloseY);
    frame(3);
    shot("06-decode-uart");

    tap(hit::kOvDecode, hit::kBotY);
    frame(2);
    tap(hit::kDecSpi, hit::kDecKindY);
    frame(3);
    tap(hit::kClose, hit::kCloseY);
    frame(3);
    shot("07-decode-spi");

    // --- cursors ---------------------------------------------------------
    tap(hit::kCurA, hit::kBotY);
    tap(400, 300);
    frame(2);
    tap(hit::kCurB, hit::kBotY);
    tap(760, 300);
    frame(3);
    shot("08-cursors");

    // --- overlays --------------------------------------------------------
    tap(hit::kOvTrigger, hit::kBotY);
    frame(3);
    shot("09-trigger");
    tap(hit::kClose, hit::kCloseY);
    frame(2);

    tap(hit::kOvChannels, hit::kBotY);
    frame(3);
    shot("10-channels");
    tap(hit::kClose, hit::kCloseY);
    frame(2);

    tap(hit::kOvDecode, hit::kBotY);
    frame(3);
    shot("11-decode-options");
    tap(hit::kClose, hit::kCloseY);
    frame(2);

    tap(hit::kOvSave, hit::kBotY);
    frame(3);
    shot("12-save");
    tap(hit::kClose, hit::kCloseY);
    frame(2);

    tap(hit::kOvInfo, hit::kBotY);
    frame(3);
    shot("13-info");
    tap(hit::kClose, hit::kCloseY);
    frame(2);

    return 0;
}
