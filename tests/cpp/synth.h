// Generador de datos sinteticos con verdad conocida (lag, jitter, QBER, ruido).
#ifndef AURORA_SYNTH_H
#define AURORA_SYNTH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "../../sw/aurora_host.h"

namespace synth {

struct Params {
    uint64_t t0 = 1'000'000;          // ps, inicio de la captura
    uint64_t duration = 25'000'000'000ULL;   // 25 ms
    double pair_rate = 30e-9;         // pares reales por ps (30 kHz)
    double alice_noise_rate = 1.35e-6;// tags de Alice sin pareja por ps (~1.35 MHz)
    double bob_noise_rate = 0.65e-6;  // cuentas oscuras/ruido de Bob por ps
    double bob_efficiency = 1.0;      // fraccion de pares detectados por Bob
    int64_t lag = 0;                  // ps, t_alice - t_bob
    double jitter = 200.0;            // ps, desviacion tipica
    double qber = 0.02;
    uint64_t seed = 1;
};

struct Truth {
    uint64_t n_pairs = 0;
};

inline Truth generate(const Params &p, aurora::Detections &alice, aurora::Detections &bob) {
    std::mt19937_64 rng(p.seed);
    std::uniform_real_distribution<double> U(0.0, 1.0);
    std::normal_distribution<double> N(0.0, p.jitter);
    std::uniform_int_distribution<int> state4(0, 3);
    struct Ev { uint64_t t; uint8_t s; };
    std::vector<Ev> A, B;
    Truth tr;

    auto poisson_times = [&](double rate, auto &&emit) {
        if (rate <= 0) return;
        std::exponential_distribution<double> E(rate);
        double t = (double)p.t0;
        const double end = (double)(p.t0 + p.duration);
        while ((t += E(rng)) < end) emit((uint64_t)t);
    };

    poisson_times(p.pair_rate, [&](uint64_t t) {
        int sa = state4(rng);
        int64_t ta = (int64_t)t + p.lag;
        int64_t tb = (int64_t)t + (int64_t)std::llround(N(rng));
        if (ta < 0 || tb < 0) return;
        A.push_back({(uint64_t)ta, (uint8_t)sa});
        if (U(rng) >= p.bob_efficiency) return;
        int basis_b = U(rng) < 0.5 ? 0 : 1;
        int bit_b;
        if (basis_b == (sa >> 1)) bit_b = (sa & 1) ^ (U(rng) < p.qber ? 1 : 0);
        else bit_b = U(rng) < 0.5 ? 0 : 1;
        B.push_back({(uint64_t)tb, (uint8_t)(basis_b * 2 + bit_b)});
        tr.n_pairs++;
    });
    poisson_times(p.alice_noise_rate, [&](uint64_t t) { A.push_back({t + (uint64_t)std::max<int64_t>(0, p.lag), (uint8_t)state4(rng)}); });
    poisson_times(p.bob_noise_rate, [&](uint64_t t) { B.push_back({t, (uint8_t)state4(rng)}); });

    auto finish = [](std::vector<Ev> &v, aurora::Detections &d) {
        std::sort(v.begin(), v.end(), [](const Ev &x, const Ev &y) { return x.t < y.t; });
        d.tags.clear(); d.states.clear();
        for (auto &e : v) { d.tags.push_back(e.t); d.states.push_back(e.s); }
    };
    finish(A, alice);
    finish(B, bob);
    return tr;
}

} // namespace synth

#endif
