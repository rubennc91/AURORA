// =============================================================================
// AURORA - Fuente de aleatoriedad del sistema
// =============================================================================
// Todas las semillas PUBLICAS del post-procesado (Cascade, verificacion,
// Toeplitz de la PA) salen de una RandomSource, siempre del lado de Alice, y
// se envian a Bob por el canal clasico autenticado.
//
//   TrngRandomSource       produccion: TRNG de la PL (driver + extractor)
//   SimulatedRandomSource  SOLO PC/pruebas: PRNG determinista
//   os_random_bits()       SOLO instalacion: generador del sistema operativo,
//                          para la semilla publica del extractor del TRNG y
//                          la clave precompartida inicial
// =============================================================================
#ifndef AURORA_RANDOM_H
#define AURORA_RANDOM_H

#include <cstdint>

#include "aurora_trng_driver.h"

namespace aurora {

struct RandomSource {
    virtual ~RandomSource() = default;
    // n bits aleatorios uniformes. false si la fuente falla (p. ej. alarma del TRNG).
    virtual bool random_bits(size_t n, Bits &out) = 0;
    virtual const char *name() const = 0;
    virtual bool simulated() const { return false; }
    bool random_u64(uint64_t &v) {
        Bits b;
        if (!random_bits(64, b)) return false;
        v = 0;
        for (int i = 0; i < 64; ++i) v |= (uint64_t)b[(size_t)i] << i;
        return true;
    }
};

class TrngRandomSource : public RandomSource {
public:
    TrngRandomSource(TrngDriver &drv, Bits extractor_seed) : drv_(drv), seed_(std::move(extractor_seed)) {}
    bool random_bits(size_t n, Bits &out) override {
        last_ = drv_.read_random(out, n, seed_);
        return last_ == TrngStatus::OK;
    }
    const char *name() const override { return "TRNG (osciladores en anillo)"; }
    TrngStatus last_status() const { return last_; }
private:
    TrngDriver &drv_;
    Bits seed_;
    TrngStatus last_ = TrngStatus::OK;
};

class SimulatedRandomSource : public RandomSource {
public:
    explicit SimulatedRandomSource(uint64_t seed) : rng_(seed) {}
    bool random_bits(size_t n, Bits &out) override {
        out.resize(n);
        for (size_t i = 0; i < n; i += 64) {
            uint64_t w = rng_.next();
            for (size_t j = 0; j < 64 && i + j < n; ++j) out[i + j] = (w >> j) & 1;
        }
        return true;
    }
    const char *name() const override { return "SIMULADA (PRNG, solo pruebas)"; }
    bool simulated() const override { return true; }
private:
    SplitMix64 rng_;
};

// Generador del sistema operativo (std::random_device: /dev/urandom en Linux,
// BCryptGenRandom en Windows con MinGW >= 9.2). Solo para la instalacion.
bool os_random_bits(size_t n, Bits &out, std::string *err = nullptr);

} // namespace aurora

#endif
