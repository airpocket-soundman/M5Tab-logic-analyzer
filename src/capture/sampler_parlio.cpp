// ---------------------------------------------------------------------------
//  sampler_parlio.cpp - PARLIO RX + GDMA sampling backend (ESP32-P4)
// ---------------------------------------------------------------------------
//
//  The PARLIO receiver latches all eight probe lines on every edge of an
//  internally divided clock and hands the bytes to GDMA, so sampling costs no
//  CPU time at all and has no jitter.
//
//  One constraint shapes the design: the driver refuses a DMA payload that is
//  not in internal RAM (unless it makes its own internal copy of the same
//  size), so DMA cannot write the multi-megabyte PSRAM buffer directly.  We
//  therefore keep an internal ring and run an *infinite* transaction over it -
//  the descriptor chain is circular, so the hardware never stops between
//  blocks and the capture is gapless.
//
//  There are then two ways to get the data out, and which one we use decides
//  the achievable sample rate:
//
//    DIRECT  (capture fits in the ring)  The ISR only counts bytes.  Nothing is
//            copied while sampling, so there is *no* real-time constraint at
//            all and the rate is limited purely by the peripheral - 80 MSa/s if
//            you ask for it.  The ring is copied to PSRAM after the sweep ends.
//
//    STREAM  (capture is deeper than the ring)  The ISR copies each finished
//            descriptor to PSRAM as it lands.  Now the copy has to keep up with
//            the sample rate, and it competes with the MIPI-DSI panel for PSRAM
//            bandwidth.  We time the ISR and report the headroom rather than
//            letting an overrun corrupt data silently.
//
//  So: shallow captures go as fast as the silicon allows, deep captures trade
//  rate for depth, and the UI is told which regime it is in.
//
#include "sampler.h"

#if LA_HAVE_PARLIO

#include <driver/gpio.h>
#include <driver/parlio_rx.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

namespace {

static const char* TAG = "parlio";

// 65280 = 0xFF00: 64 byte aligned and within the delimiter's 16 bit EOF length
// register, so an EOF lands exactly on a ring boundary.  The ring is a whole
// number of these.
constexpr size_t kRingChunk = 65280;

constexpr int    kRingChunkTry[] = {3, 2, 1};

class SamplerParlio final : public ISampler {
public:
    const char* name() const override { return "PARLIO"; }

    bool begin(const char** reason) override {
        if (_ring) return true;

        // Take as much internal DMA RAM as we can get: every byte here is a
        // byte of capture that needs no real-time copying.
        for (int mult : kRingChunkTry) {
            const size_t want = kRingChunk * mult;
            _ring = static_cast<uint8_t*>(heap_caps_aligned_alloc(
                64, want, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
            if (_ring) { _ringBytes = want; break; }
        }
        if (!_ring) {
            if (reason) *reason = "no internal DMA RAM for the PARLIO ring";
            return false;
        }
        _doneSem = xSemaphoreCreateBinary();
        if (!_doneSem) {
            if (reason) *reason = "no memory for the PARLIO semaphore";
            return false;
        }
        // Pull-ups on idle probes so an unconnected channel reads as a steady
        // high rather than picking up noise.
#if LA_PROBE_PULLUP
        gpio_config_t io = {};
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_ENABLE;
        for (int ch = 0; ch < LA_MAX_CHANNELS; ++ch) {
            io.pin_bit_mask = 1ULL << kChannelPin[ch];
            gpio_config(&io);
        }
#endif
        ESP_LOGI(TAG, "ring %u bytes at %p", (unsigned)_ringBytes, _ring);
        if (reason) *reason = nullptr;
        return true;
    }

    uint32_t losslessDepth() const override { return _ringBytes; }

    bool configure(const CaptureConfig& cfg, CaptureBuffer& buf,
                   double* achievedRateHz, const char** reason) override {
        (void)buf;
        teardown();
        if (!_ring && !begin(reason)) return false;

        parlio_rx_unit_config_t uc = {};
        uc.trans_queue_depth = 1;
        uc.max_recv_size     = _ringBytes;
        uc.data_width        = LA_MAX_CHANNELS;
        uc.clk_src           = PARLIO_CLK_SRC_DEFAULT;
        uc.ext_clk_freq_hz   = 0;
        uc.exp_clk_freq_hz   = cfg.rateHz;
        uc.clk_in_gpio_num   = GPIO_NUM_NC;
        uc.clk_out_gpio_num  = GPIO_NUM_NC;
        uc.valid_gpio_num    = static_cast<gpio_num_t>(LA_PIN_VALID);
        for (int i = 0; i < PARLIO_RX_UNIT_MAX_DATA_WIDTH; ++i) {
            uc.data_gpio_nums[i] = (i < LA_MAX_CHANNELS)
                                       ? static_cast<gpio_num_t>(kChannelPin[i])
                                       : GPIO_NUM_NC;
        }
        uc.flags.free_clk    = 0;
        uc.flags.clk_gate_en = 0;
        uc.flags.io_loop_back = 0;

        esp_err_t err = parlio_new_rx_unit(&uc, &_unit);
        if (err != ESP_OK) {
            _unit = nullptr;
            if (reason) *reason = "parlio_new_rx_unit failed";
            ESP_LOGE(TAG, "new_rx_unit: %s", esp_err_to_name(err));
            return false;
        }

        parlio_rx_event_callbacks_t cbs = {};
        cbs.on_partial_receive = &SamplerParlio::onPartial;
        err = parlio_rx_unit_register_event_callbacks(_unit, &cbs, this);
        if (err != ESP_OK) {
            if (reason) *reason = "parlio callback registration failed";
            teardown();
            return false;
        }

        // Hold the enable line asserted from the chip's own pad so the level
        // delimiter never sees a frame end.
        gpio_set_direction(static_cast<gpio_num_t>(LA_PIN_VALID),
                           GPIO_MODE_INPUT_OUTPUT);
        gpio_set_level(static_cast<gpio_num_t>(LA_PIN_VALID), 1);

        parlio_rx_level_delimiter_config_t ld = {};
        ld.valid_sig_line_id = LA_MAX_CHANNELS;   // first line past the data
        ld.sample_edge       = PARLIO_SAMPLE_EDGE_POS;
        ld.bit_pack_order    = PARLIO_BIT_PACK_ORDER_LSB;
        ld.eof_data_len      = 0;                 // end only on enable inactive
        ld.timeout_ticks     = 0;
        ld.flags.active_low_en = 0;
        err = parlio_new_rx_level_delimiter(&ld, &_deli);
        _usingLevel = (err == ESP_OK);

        if (!_usingLevel) {
            // Fall back to the soft delimiter rather than refuse to sample; the
            // periodic EOF costs about one spurious edge per 65280 samples.
            ESP_LOGW(TAG, "level delimiter unavailable (%s), falling back",
                     esp_err_to_name(err));
            parlio_rx_soft_delimiter_config_t sd = {};
            sd.sample_edge     = PARLIO_SAMPLE_EDGE_POS;
            sd.bit_pack_order  = PARLIO_BIT_PACK_ORDER_LSB;
            sd.eof_data_len    = kRingChunk;
            sd.timeout_ticks   = 0;
            err = parlio_new_rx_soft_delimiter(&sd, &_deli);
        }
        if (err != ESP_OK) {
            if (reason) *reason = "parlio delimiter creation failed";
            teardown();
            return false;
        }

        _requestedRate = cfg.rateHz;
        if (achievedRateHz) *achievedRateHz = estimateRate(cfg.rateHz);
        if (reason) *reason = nullptr;
        return true;
    }

    uint32_t capture(CaptureBuffer& buf, uint32_t samples,
                     double* measuredRateHz) override {
        if (!_unit || !_deli || !buf.data()) return 0;
        if (samples > buf.capacity()) samples = buf.capacity();

        // The whole point: if it fits in the ring, do not copy anything while
        // the peripheral is running.
        _direct  = (samples <= _ringBytes);
        _dst     = buf.data();
        _target  = samples;
        _written = 0;
        _isrUs   = 0;
        _isrMaxUs = 0;
        _pendPtr = nullptr;
        _pendLen = 0;
        xSemaphoreTake(_doneSem, 0);

        esp_err_t err = parlio_rx_unit_enable(_unit, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "enable: %s", esp_err_to_name(err));
            return 0;
        }

        parlio_receive_config_t rc = {};
        rc.delimiter = _deli;
        rc.flags.partial_rx_en  = 1;   // infinite, circular over the ring
        rc.flags.indirect_mount = 0;   // DMA writes our ring directly

        gpio_set_level(static_cast<gpio_num_t>(LA_PIN_VALID), 1);
        const int64_t t0 = esp_timer_get_time();
        err = parlio_rx_unit_receive(_unit, _ring, _ringBytes, &rc);
        // A level delimiter starts as soon as its enable is asserted, which it
        // permanently is; only the soft delimiter needs an explicit kick.
        if (err == ESP_OK && !_usingLevel) {
            err = parlio_rx_soft_delimiter_start_stop(_unit, _deli, true);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "start: %s", esp_err_to_name(err));
            parlio_rx_unit_disable(_unit);
            return 0;
        }

        // Generous ceiling: three times the nominal duration plus a fixed
        // allowance so a stalled peripheral surfaces as a short capture rather
        // than a hang.
        const double nominal = estimateRate(_requestedRate);
        uint32_t waitMs = static_cast<uint32_t>(
            (nominal > 0 ? (samples / nominal) * 3000.0 : 1000.0) + 500.0);
        if (waitMs > 20000) waitMs = 20000;

        const BaseType_t got = xSemaphoreTake(_doneSem, pdMS_TO_TICKS(waitMs));
        const int64_t t1 = esp_timer_get_time();

        if (!_usingLevel) parlio_rx_soft_delimiter_start_stop(_unit, _deli, false);
        parlio_rx_unit_disable(_unit);

        uint32_t written = _written;
        if (written > samples) written = samples;
        if (_direct && written > 0) {
            // Sampling is over; now the copy costs nothing but wall clock.
            memcpy(_dst, _ring, written);
        }
        _dst = nullptr;

        const double elapsedSec = (t1 - t0) / 1e6;
        _lastElapsedUs = static_cast<uint32_t>(t1 - t0);
        _wallRateHz = (elapsedSec > 0 && written > 0) ? written / elapsedSec : 0.0;
        // Report the *nominal* rate, not the wall-clock one.  PARLIO is clocked
        // by a hardware divider, so the nominal figure is exact, whereas the
        // wall-clock measurement also counts arming and teardown and would put
        // a systematic ~1% stretch on the time axis.  The measured value is
        // kept for describeLast() as a sanity check that the DMA kept up.
        if (measuredRateHz) *measuredRateHz = nominal;
        if (!got) ESP_LOGW(TAG, "capture timed out after %u samples", (unsigned)written);
        return written;
    }

    void describeLast(char* out, size_t len) const override {
        if (!len) return;
        if (_direct) {
            snprintf(out, len, "direct (<=%uKiB ring, no realtime copy), %s, wall %.2f MSa/s",
                     (unsigned)(_ringBytes / 1024),
                     _usingLevel ? "no EOF" : "soft EOF", _wallRateHz / 1e6);
        } else {
            // How much of the capture window the copy ISR consumed.  Past ~80%
            // the DMA is about to lap the copier and samples get corrupted.
            const double duty = _lastElapsedUs > 0
                ? 100.0 * static_cast<double>(_isrUs) / _lastElapsedUs
                : 0.0;
            snprintf(out, len, "stream, copy ISR %.0f%% busy (max %uus), %s, wall %.2f MSa/s%s",
                     duty, (unsigned)_isrMaxUs, _usingLevel ? "no EOF" : "soft EOF",
                     _wallRateHz / 1e6, duty > 80.0 ? " OVERRUN RISK" : "");
        }
    }

    void end() override { teardown(); }

private:
    static bool IRAM_ATTR onPartial(parlio_rx_unit_handle_t unit,
                                    const parlio_rx_event_data_t* edata,
                                    void* ctx) {
        (void)unit;
        auto* self = static_cast<SamplerParlio*>(ctx);
        uint8_t* dst = self->_dst;
        if (!dst) return false;

        if (self->_direct) {
            // Nothing is copied while sampling; just count.
            uint32_t written = self->_written;
            if (written >= self->_target) return false;
            uint32_t n = edata->recv_bytes;
            if (written + n > self->_target) n = self->_target - written;
            self->_written = written + n;
            if (self->_written >= self->_target) {
                BaseType_t hpw = pdFALSE;
                xSemaphoreGiveFromISR(self->_doneSem, &hpw);
                return hpw == pdTRUE;
            }
            return false;
        }

        // Copy the descriptor *before* the one that just completed.  Reading a
        // descriptor the moment GDMA reports it done leaves its last byte
        // unsettled - one corrupted sample per boundary, which showed up as a
        // steady ~1 extra edge per 4092 samples at every rate and depth, and
        // looked exactly like probe noise.  Staying one descriptor behind costs
        // 4 kB of latency and nothing else.
        const void* prevPtr = self->_pendPtr;
        const uint32_t prevLen = self->_pendLen;
        self->_pendPtr = edata->data;
        self->_pendLen = edata->recv_bytes;
        if (!prevPtr) return false;

        uint32_t written = self->_written;
        if (written >= self->_target) return false;
        uint32_t n = prevLen;
        if (written + n > self->_target) n = self->_target - written;

        const int64_t t0 = esp_timer_get_time();
        memcpy(dst + written, prevPtr, n);
        const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        self->_isrUs += us;
        if (us > self->_isrMaxUs) self->_isrMaxUs = us;

        self->_written = written + n;
        if (self->_written >= self->_target) {
            BaseType_t hpw = pdFALSE;
            xSemaphoreGiveFromISR(self->_doneSem, &hpw);
            return hpw == pdTRUE;
        }
        return false;
    }

    // PARLIO divides its source clock by an integer, so only some rates are
    // reachable.  Report the one the hardware will really produce.
    static double estimateRate(uint32_t requested) {
        if (requested == 0) return 0.0;
        const double src = 160000000.0;   // PARLIO_CLK_SRC_PLL_F160M
        uint32_t div = static_cast<uint32_t>(src / requested + 0.5);
        if (div < 1) div = 1;
        if (div > 256) div = 256;         // PARLIO_LL_RX_MAX_CLK_INT_DIV
        return src / div;
    }

    void teardown() {
        if (_deli) {
            parlio_del_rx_delimiter(_deli);
            _deli = nullptr;
        }
        if (_unit) {
            parlio_del_rx_unit(_unit);
            _unit = nullptr;
        }
    }

    uint8_t*                     _ring      = nullptr;
    size_t                       _ringBytes = 0;
    parlio_rx_unit_handle_t      _unit      = nullptr;
    parlio_rx_delimiter_handle_t _deli      = nullptr;
    SemaphoreHandle_t            _doneSem   = nullptr;

    uint8_t* volatile _dst      = nullptr;
    volatile uint32_t _written  = 0;
    const void* volatile _pendPtr = nullptr;   // descriptor awaiting its copy
    volatile uint32_t _pendLen  = 0;
    volatile uint32_t _isrUs    = 0;
    volatile uint32_t _isrMaxUs = 0;
    volatile bool     _direct   = true;
    uint32_t          _target   = 0;
    uint32_t          _requestedRate = 1000000;
    bool              _usingLevel    = false;
    uint32_t          _lastElapsedUs = 0;
    double            _wallRateHz    = 0.0;
};

}  // namespace

ISampler* createParlioSampler() {
    static SamplerParlio s;
    return &s;
}

#else  // !LA_HAVE_PARLIO

ISampler* createParlioSampler() { return nullptr; }

#endif
