// =============================================================================
// AURORA - Driver del TRNG (rtl/trng/trng_axi.v) para el PS
// =============================================================================
// Independiente de la plataforma: el acceso a registros se hace a traves de
// TrngRegs. En bare-metal o con el puntero de /dev/mem (Linux) se usa
// MmioRegs; en los tests, un modelo del periferico.
//
// Secuencia de init():
//   1. Comprueba el registro ID.
//   2. Programa DECIM y los umbrales de los tests de salud para la H_min dada.
//   3. Autodiagnostico: inyecta los 3 fallos (bloqueo, sesgo, osciladores
//      apagados) y exige que salte una alarma en cada caso.
//   4. Arranque normal y espera a startup_done sin alarmas.
// =============================================================================
#ifndef AURORA_TRNG_DRIVER_H
#define AURORA_TRNG_DRIVER_H

#include <cstdint>

#include "aurora_trng.h"

namespace aurora {

struct TrngRegs {
    virtual ~TrngRegs() = default;
    virtual uint32_t read(uint32_t offset) = 0;
    virtual void write(uint32_t offset, uint32_t value) = 0;
    virtual void wait_us(uint32_t us) = 0;
};

// Acceso directo a memoria (bare-metal, o puntero obtenido con mmap de /dev/mem).
class MmioRegs : public TrngRegs {
public:
    explicit MmioRegs(volatile uint32_t *base) : base_(base) {}
    uint32_t read(uint32_t off) override { return base_[off / 4]; }
    void write(uint32_t off, uint32_t v) override { base_[off / 4] = v; }
    void wait_us(uint32_t us) override {
        for (volatile uint32_t i = 0; i < us * 100u; i = i + 1) {}   // sustituir por usleep/Xil_Sleep
    }
private:
    volatile uint32_t *base_;
};

struct TrngConfig {
    double h_min = 0.5;          // entropia asumida (medirla en la placa)
    uint32_t decim = 64;
    uint32_t timeout_us = 200000;
    bool self_test = true;
};

enum class TrngStatus { OK, NOT_FOUND, SELFTEST_FAILED, STARTUP_TIMEOUT, ALARM, TIMEOUT, BAD_SEED };
const char *trng_status_str(TrngStatus s);

class TrngDriver {
public:
    explicit TrngDriver(TrngRegs &regs) : r_(regs) {}
    TrngStatus init(const TrngConfig &cfg = TrngConfig());
    // Bits brutos (ya filtrados por los tests de salud del hardware).
    TrngStatus read_raw(Bits &out, size_t n_bits);
    // Bits casi uniformes: bits brutos + extractor Toeplitz con la semilla
    // publica fija del extractor (ver extract_random).
    TrngStatus read_random(Bits &out, size_t n_bits, const Bits &extractor_seed);
private:
    bool wait_status(uint32_t mask, uint32_t &st);
    TrngRegs &r_;
    TrngConfig cfg_;
    bool ready_ = false;
};

} // namespace aurora

#endif
