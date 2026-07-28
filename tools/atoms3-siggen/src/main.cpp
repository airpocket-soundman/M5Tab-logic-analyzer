// ---------------------------------------------------------------------------
//  AtomS3 signal generator
// ---------------------------------------------------------------------------
//
//  A small, independent source of known signals for validating the M5Tab5
//  Logic Analyzer.  The analyzer's own test generator drives one of its own
//  pads, so however carefully it is measured it is still the same chip talking
//  to itself; this is a separate board with its own clock, driving real wire.
//
//  Wiring (six channels):
//
//      AtomS3 G1 -> Tab5 CH0 (G2)      AtomS3 G6 -> Tab5 CH3 (G5)
//      AtomS3 G2 -> Tab5 CH1 (G3)      AtomS3 G7 -> Tab5 CH4 (G16)
//      AtomS3 G5 -> Tab5 CH2 (G4)      AtomS3 G8 -> Tab5 CH5 (G17)
//      AtomS3 GND -> Tab5 GND (M5-Bus pin 1 / 3 / 5)
//
//  Control is the same shape as the analyzer's API - one line in, one line of
//  JSON out over USB CDC - so the same host script style drives both ends.
//
//      ping
//      square [pin=<gpio>] freq=1000000 [duty=50]
//      uart   [pin=<gpio>] baud=115200 [text=M5Tab5] [gap=200]
//      counter period=<us>      synchronous binary counter across all outputs
//      spi    [clk_hz=] [bytes=]  bit-banged mode 0 SPI on the first four pins
//      off
//
#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_reg.h>

namespace {

// Output order matches Tab5 CH0..CH5.
constexpr int kPins[] = {1, 2, 5, 6, 7, 8};
constexpr int kNumPins = sizeof(kPins) / sizeof(kPins[0]);

enum class Mode : uint8_t { Off, Square, Uart, Counter, Spi };

Mode     g_mode = Mode::Off;
char     g_text[32] = "M5Tab5";
uint32_t g_gapUs = 200;
int      g_squarePin = kPins[0];

hw_timer_t* g_timer = nullptr;
volatile uint32_t g_count = 0;
uint32_t g_allMask = 0;

char g_line[128];
int  g_len = 0;

// ---------------------------------------------------------------------------
//  Argument parsing (same rules as the analyzer's API)
// ---------------------------------------------------------------------------
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
    *out = strtoul(v, nullptr, 0);
    return true;
}

bool argWord(const char* line, const char* key, char* out, size_t n) {
    const char* v = findArg(line, key);
    if (!v) return false;
    size_t i = 0;
    while (v[i] && !isspace((unsigned char)v[i]) && i + 1 < n) { out[i] = v[i]; ++i; }
    out[i] = 0;
    return true;
}

void fail(const char* why) {
    Serial.printf("{\"ok\":false,\"err\":\"%s\"}\n", why);
    Serial.flush();
}

// ---------------------------------------------------------------------------
//  Output control
// ---------------------------------------------------------------------------
// Every counter bit lands on the pads in the same write, so the analyzer sees
// all six channels change on one sample.  That is what makes this a test of
// channel alignment and not just of six independent signals.
void IRAM_ATTR onTick() {
    const uint32_t v = ++g_count;
    uint32_t set = 0, clr = 0;
    for (int i = 0; i < kNumPins; ++i) {
        const uint32_t bit = 1u << kPins[i];
        if (v & (1u << i)) set |= bit; else clr |= bit;
    }
    REG_WRITE(GPIO_OUT_W1TS_REG, set);
    REG_WRITE(GPIO_OUT_W1TC_REG, clr);
}

void releaseOutput() {
    if (g_timer) {
        timerDetachInterrupt(g_timer);
        timerEnd(g_timer);
        g_timer = nullptr;
    }
    switch (g_mode) {
        case Mode::Square: ledcDetach(g_squarePin); break;
        case Mode::Uart:   Serial1.end(); break;
        default: break;
    }
    g_mode = Mode::Off;
    // Idle high on every output, matching a resting UART line and an unloaded
    // probe.  Leaving a pad driving would fight whatever else is on the wire.
    for (int i = 0; i < kNumPins; ++i) {
        pinMode(kPins[i], OUTPUT);
        digitalWrite(kPins[i], HIGH);
    }
}

// Output drive strength.  A hard, fast edge into an unterminated flying lead
// rings, and rings enough to cross the receiver's threshold more than once -
// which a logic analyzer faithfully records as extra edges.  Backing the drive
// off slows the edge and is the one knob available without soldering a series
// resistor.
uint32_t g_drive = 2;   // GPIO_DRIVE_CAP_2 is the chip default

void applyDrive() {
    for (int i = 0; i < kNumPins; ++i) {
        gpio_set_drive_capability(static_cast<gpio_num_t>(kPins[i]),
                                  static_cast<gpio_drive_cap_t>(g_drive));
    }
}

int resolveOutPin(const char* args, int fallback) {
    uint32_t v = 0;
    if (argU32(args, "pin", &v)) return static_cast<int>(v);
    if (argU32(args, "ch", &v) && v < kNumPins) return kPins[v];
    return fallback;
}

void cmdSquare(const char* args) {
    uint32_t freq = 1000000, duty = 50;
    argU32(args, "freq", &freq);
    argU32(args, "duty", &duty);
    const int pin = resolveOutPin(args, kPins[0]);
    if (freq == 0) { fail("freq must be > 0"); return; }
    if (duty > 100) duty = 100;

    releaseOutput();
    g_squarePin = pin;

    // LEDC counts to 2^resolution per period, so the resolution has to shrink
    // as the frequency rises.  Start at the theoretical best and walk down
    // until the driver accepts it.
    int res = 14;
    while (res > 1 && (static_cast<uint64_t>(freq) << res) > 80000000ULL) res--;
    bool ok = false;
    for (; res >= 1; --res) {
        if (ledcAttach(pin, freq, static_cast<uint8_t>(res))) { ok = true; break; }
    }
    if (!ok) { fail("ledcAttach failed (frequency out of range?)"); return; }

    const uint32_t maxDuty = (1u << res) - 1;
    ledcWrite(pin, (maxDuty * duty) / 100);
    applyDrive();
    g_mode = Mode::Square;
    Serial.printf("{\"ok\":true,\"mode\":\"square\",\"pin\":%d,\"freq\":%u,"
                  "\"duty\":%u,\"res_bits\":%d}\n",
                  pin, (unsigned)freq, (unsigned)duty, res);
    Serial.flush();
}

void cmdUart(const char* args) {
    uint32_t baud = 115200;
    argU32(args, "baud", &baud);
    argU32(args, "gap", &g_gapUs);
    argWord(args, "text", g_text, sizeof(g_text));
    const int pin = resolveOutPin(args, kPins[0]);
    if (baud == 0) { fail("baud must be > 0"); return; }

    releaseOutput();
    Serial1.begin(baud, SERIAL_8N1, -1, pin);   // TX only
    g_mode = Mode::Uart;
    Serial.printf("{\"ok\":true,\"mode\":\"uart\",\"pin\":%d,\"baud\":%u,"
                  "\"text\":\"%s\",\"gap_us\":%u}\n",
                  pin, (unsigned)baud, g_text, (unsigned)g_gapUs);
    Serial.flush();
}

void cmdCounter(const char* args) {
    uint32_t periodUs = 10;
    argU32(args, "period", &periodUs);
    if (periodUs < 2) periodUs = 2;

    releaseOutput();
    g_count = 0;
    g_timer = timerBegin(1000000);              // 1 MHz tick
    if (!g_timer) { fail("no hardware timer"); return; }
    timerAttachInterrupt(g_timer, &onTick);
    timerAlarm(g_timer, periodUs, true, 0);
    applyDrive();
    g_mode = Mode::Counter;

    Serial.printf("{\"ok\":true,\"mode\":\"counter\",\"period_us\":%u,\"pins\":%d,"
                  "\"ch0_hz\":%.2f}\n",
                  (unsigned)periodUs, kNumPins, 1e6 / (2.0 * periodUs));
    Serial.flush();
}

// Bit-banged mode 0 SPI so the analyzer's SPI decoder has something real to
// chew on: CLK on the first output, MOSI on the second, MISO on the third
// (echoing an inverted pattern) and CS on the fourth.
void cmdSpi(const char* args) {
    uint32_t clkHz = 100000, nbytes = 4;
    argU32(args, "clk_hz", &clkHz);
    argU32(args, "bytes", &nbytes);
    if (clkHz == 0) clkHz = 100000;
    if (nbytes == 0 || nbytes > 16) nbytes = 4;

    releaseOutput();
    g_mode = Mode::Spi;
    Serial.printf("{\"ok\":true,\"mode\":\"spi\",\"clk\":%d,\"mosi\":%d,\"miso\":%d,"
                  "\"cs\":%d,\"clk_hz\":%u,\"bytes\":%u}\n",
                  kPins[0], kPins[1], kPins[2], kPins[3],
                  (unsigned)clkHz, (unsigned)nbytes);
    Serial.flush();
    // Parameters are picked up by loop().
    g_gapUs = 1000000 / clkHz / 2;
    if (g_gapUs == 0) g_gapUs = 1;
    g_count = nbytes;
}

void handle(char* line) {
    char verb[16] = {0};
    size_t i = 0;
    while (line[i] && !isspace((unsigned char)line[i]) && i + 1 < sizeof(verb)) {
        verb[i] = static_cast<char>(tolower((unsigned char)line[i]));
        ++i;
    }
    const char* args = line + i;

    if (!strcmp(verb, "ping")) {
        const char* m = g_mode == Mode::Square  ? "square"
                      : g_mode == Mode::Uart    ? "uart"
                      : g_mode == Mode::Counter ? "counter"
                      : g_mode == Mode::Spi     ? "spi" : "off";
        Serial.printf("{\"ok\":true,\"dev\":\"atoms3-siggen\",\"mode\":\"%s\","
                      "\"pins\":[%d,%d,%d,%d,%d,%d],\"cpu_mhz\":%u}\n",
                      m, kPins[0], kPins[1], kPins[2], kPins[3], kPins[4], kPins[5],
                      (unsigned)getCpuFrequencyMhz());
        Serial.flush();
        return;
    }
    if (!strcmp(verb, "drive")) {
        uint32_t v = 0;
        if (argU32(args, "cap", &v)) { g_drive = v > 3 ? 3 : v; applyDrive(); }
        Serial.printf("{\"ok\":true,\"drive_cap\":%u}\n", (unsigned)g_drive);
        Serial.flush();
        return;
    }
    if (!strcmp(verb, "square"))  { cmdSquare(args); return; }
    if (!strcmp(verb, "uart"))    { cmdUart(args); return; }
    if (!strcmp(verb, "counter")) { cmdCounter(args); return; }
    if (!strcmp(verb, "spi"))     { cmdSpi(args); return; }
    if (!strcmp(verb, "off")) {
        releaseOutput();
        Serial.print("{\"ok\":true,\"mode\":\"off\"}\n");
        Serial.flush();
        return;
    }
    fail("unknown command");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(500);
    for (int i = 0; i < kNumPins; ++i) g_allMask |= 1u << kPins[i];
    releaseOutput();
}

void loop() {
    while (Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            g_line[g_len] = 0;
            if (g_len > 0) handle(g_line);
            g_len = 0;
            continue;
        }
        if (g_len < static_cast<int>(sizeof(g_line)) - 1) {
            g_line[g_len++] = static_cast<char>(c);
        }
    }

    if (g_mode == Mode::Uart) {
        Serial1.write(reinterpret_cast<const uint8_t*>(g_text), strlen(g_text));
        Serial1.flush();
        delayMicroseconds(g_gapUs);
    } else if (g_mode == Mode::Spi) {
        static const uint8_t kTx[] = {0x9F, 0xA5, 0x3C, 0x55, 0x0F, 0xF0, 0xCC, 0x33,
                                      0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
        const int clk = kPins[0], mosi = kPins[1], miso = kPins[2], cs = kPins[3];
        const uint32_t half = g_gapUs;
        const uint32_t n = g_count;

        digitalWrite(cs, LOW);
        delayMicroseconds(half);
        for (uint32_t b = 0; b < n; ++b) {
            for (int bit = 7; bit >= 0; --bit) {
                digitalWrite(mosi, (kTx[b] >> bit) & 1);
                digitalWrite(miso, ((~kTx[b]) >> bit) & 1);
                delayMicroseconds(half);
                digitalWrite(clk, HIGH);          // mode 0: sample on rising
                delayMicroseconds(half);
                digitalWrite(clk, LOW);
            }
        }
        delayMicroseconds(half);
        digitalWrite(cs, HIGH);
        delay(2);
    }
}
