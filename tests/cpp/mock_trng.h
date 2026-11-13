// Modelo del periferico TRNG (rtl/trng/trng_axi.v) a nivel de registros, para
// probar el driver y la aplicacion bare-metal sin hardware. La equivalencia
// con el RTL la comprueba tests/trng/test_trng_rtl.py.
#ifndef AURORA_MOCK_TRNG_H
#define AURORA_MOCK_TRNG_H

#include <deque>
#include <random>

#include "../../sw/aurora_trng_driver.h"

namespace aurora {

class MockTrng : public TrngRegs {
public:
    // Averias simulables
    bool wrong_id = false;
    bool health_broken = false;        // los tests de salud nunca disparan
    int64_t die_after_samples = -1;    // en modo normal, la fuente se bloquea tras N muestras

    uint32_t read(uint32_t off) override {
        using namespace trng_reg;
        switch (off) {
            case ID: return wrong_id ? 0xDEADBEEF : ID_VALUE;
            case STATUS: {
                uint32_t st = 0;
                if (startup_) st |= ST_STARTUP;
                if (rct_alarm_) st |= ST_RCT;
                if (apt_alarm_) st |= ST_APT;
                if (fifo_.empty()) st |= ST_EMPTY;
                return st | ((uint32_t)fifo_.size() << 16);
            }
            case DATA: {
                if (fifo_.empty()) return 0;
                uint32_t w = fifo_.front();
                fifo_.pop_front();
                return w;
            }
            case DECIM: return decim_;
            case RCT_CUT: return rct_cut_;
            case APT_CUT: return apt_cut_;
            case SAMPLES: return (uint32_t)samples_;
            default: return 0;
        }
    }
    void write(uint32_t off, uint32_t v) override {
        using namespace trng_reg;
        if (off == CTRL) {
            if (v & CTRL_CLEAR) reset_state();
            enable_ = v & CTRL_ENABLE;
            fault_ = (v >> 4) & 3;
            bypass_ = v & CTRL_BYPASS;
            ++ctrl_writes;
        } else if (off == DECIM) decim_ = v ? v : 1;
        else if (off == RCT_CUT) rct_cut_ = v;
        else if (off == APT_CUT) apt_cut_ = v;
    }
    void wait_us(uint32_t us) override {
        if (!enable_) return;
        uint64_t n = (uint64_t)us * 100 / decim_;   // reloj de 100 MHz
        for (uint64_t i = 0; i < n; ++i) sample();
    }
    int ctrl_writes = 0;

private:
    void reset_state() {
        samples_ = 0; startup_ = rct_alarm_ = apt_alarm_ = false;
        rct_b_ = 0; apt_b_ = 0; nbits_ = 0; shreg_ = 0;
        fifo_.clear();
    }
    uint8_t source_bit() {
        bool dead = die_after_samples >= 0 && (int64_t)samples_ >= die_after_samples;
        switch (dead ? 1 : fault_) {
            case 1: case 3: return 0;
            case 2: return (rng_() & 1) & (rng_() & 1) & (rng_() & 1);
            default: return rng_() & 1;
        }
    }
    void sample() {
        uint8_t x = source_bit();
        const bool healthy_before = startup_ && !rct_alarm_ && !apt_alarm_;
        if (!health_broken) {
            if (rct_b_ && x == rct_a_) { if (++rct_b_ >= rct_cut_) rct_alarm_ = true; }
            else { rct_a_ = x; rct_b_ = 1; }
            if (samples_ % APT_WINDOW == 0) { apt_a_ = x; apt_b_ = 1; }
            else if (x == apt_a_ && ++apt_b_ >= apt_cut_) apt_alarm_ = true;
        }
        if (samples_ + 1 >= APT_WINDOW && !rct_alarm_ && !apt_alarm_) startup_ = true;
        ++samples_;
        if ((healthy_before && !rct_alarm_ && !apt_alarm_) || bypass_) {
            shreg_ = (shreg_ << 1) | x;
            if (++nbits_ == 32) { nbits_ = 0; if (fifo_.size() < 256) fifo_.push_back(shreg_); }
        }
    }
    std::mt19937_64 rng_{123};
    bool enable_ = false, bypass_ = false, startup_ = false, rct_alarm_ = false, apt_alarm_ = false;
    uint32_t fault_ = 0, decim_ = 64, rct_cut_ = 41, apt_cut_ = 793;
    uint64_t samples_ = 0;
    uint8_t rct_a_ = 0, apt_a_ = 0;
    uint32_t rct_b_ = 0, apt_b_ = 0, nbits_ = 0, shreg_ = 0;
    std::deque<uint32_t> fifo_;
};

} // namespace aurora

#endif
