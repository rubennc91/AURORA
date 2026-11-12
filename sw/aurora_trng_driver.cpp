// =============================================================================
// AURORA - Driver del TRNG: implementacion
// =============================================================================
#include "aurora_trng_driver.h"

#include <cmath>

namespace aurora {

const char *trng_status_str(TrngStatus s) {
    switch (s) {
        case TrngStatus::OK: return "OK";
        case TrngStatus::NOT_FOUND: return "periferico no encontrado (ID incorrecto)";
        case TrngStatus::SELFTEST_FAILED: return "autodiagnostico fallido: un fallo inyectado no disparo alarma";
        case TrngStatus::STARTUP_TIMEOUT: return "el test de arranque no termino";
        case TrngStatus::ALARM: return "alarma de los tests de salud";
        case TrngStatus::TIMEOUT: return "tiempo de espera agotado";
    }
    return "?";
}

bool TrngDriver::wait_status(uint32_t mask, uint32_t &st) {
    const uint32_t step = 100;
    for (uint32_t t = 0; t <= cfg_.timeout_us; t += step) {
        st = r_.read(trng_reg::STATUS);
        if (st & mask) return true;
        r_.wait_us(step);
    }
    return false;
}

TrngStatus TrngDriver::init(const TrngConfig &cfg) {
    using namespace trng_reg;
    cfg_ = cfg;
    ready_ = false;
    if (r_.read(ID) != ID_VALUE) return TrngStatus::NOT_FOUND;

    r_.write(CTRL, CTRL_CLEAR);
    r_.write(DECIM, cfg.decim);
    r_.write(RCT_CUT, rct_cutoff(cfg.h_min));
    r_.write(APT_CUT, apt_cutoff(cfg.h_min));

    uint32_t st = 0;
    if (cfg.self_test) {
        for (uint32_t fault = 1; fault <= 3; ++fault) {
            r_.write(CTRL, CTRL_ENABLE | CTRL_CLEAR | ctrl_fault(fault));
            if (!wait_status(ST_RCT | ST_APT, st)) {
                r_.write(CTRL, CTRL_CLEAR);
                return TrngStatus::SELFTEST_FAILED;
            }
            // Con una alarma no puede haber salido ningun dato.
            if (!(st & ST_EMPTY) || (st & ST_STARTUP)) {
                r_.write(CTRL, CTRL_CLEAR);
                return TrngStatus::SELFTEST_FAILED;
            }
        }
    }

    r_.write(CTRL, CTRL_ENABLE | CTRL_CLEAR);
    if (!wait_status(ST_STARTUP | ST_RCT | ST_APT, st)) return TrngStatus::STARTUP_TIMEOUT;
    if (st & (ST_RCT | ST_APT)) return TrngStatus::ALARM;
    ready_ = true;
    return TrngStatus::OK;
}

TrngStatus TrngDriver::read_raw(Bits &out, size_t n_bits) {
    using namespace trng_reg;
    out.clear();
    if (!ready_) return TrngStatus::ALARM;
    uint32_t waited = 0;
    while (out.size() < n_bits) {
        uint32_t st = r_.read(STATUS);
        if (st & (ST_RCT | ST_APT)) {
            ready_ = false;
            out.clear();          // no se entregan bits de una fuente en fallo
            return TrngStatus::ALARM;
        }
        if (st & ST_EMPTY) {
            if (waited >= cfg_.timeout_us) return TrngStatus::TIMEOUT;
            r_.wait_us(100);
            waited += 100;
            continue;
        }
        waited = 0;
        uint32_t w = r_.read(DATA);
        for (int b = 31; b >= 0 && out.size() < n_bits; --b) out.push_back((w >> b) & 1);
    }
    return TrngStatus::OK;
}

TrngStatus TrngDriver::read_random(Bits &out, size_t n_bits, uint64_t extractor_seed) {
    // n_raw tal que floor(n_raw * h - 128) >= n_bits.
    const size_t n_raw = (size_t)std::ceil((n_bits + 128.0) / cfg_.h_min) + 1;
    Bits raw;
    TrngStatus s = read_raw(raw, n_raw);
    if (s != TrngStatus::OK) { out.clear(); return s; }
    out = extract_random(raw, cfg_.h_min, extractor_seed);
    out.resize(n_bits);
    return TrngStatus::OK;
}

} // namespace aurora
