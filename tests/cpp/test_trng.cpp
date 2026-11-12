// =============================================================================
// AURORA - Tests del software del TRNG (modelo de salud, entropia, extractor)
// =============================================================================
#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>

#include "../../sw/aurora_trng.h"
#include "test_framework.h"

using namespace aurora;

static Bits iid_bits(size_t n, double p1, uint64_t seed) {
    std::mt19937_64 r(seed);
    std::bernoulli_distribution d(p1);
    Bits b(n);
    for (auto &v : b) v = d(r) ? 1 : 0;
    return b;
}

static double monobit_z(const Bits &b) {
    long s = 0;
    for (auto v : b) s += v ? 1 : -1;
    return std::fabs((double)s) / std::sqrt((double)b.size());
}

// ----------------------------------------------------------------- umbrales --
TEST(cutoffs_match_sp800_90b) {
    CHECK(rct_cutoff(1.0) == 21);     // SP 800-90B 4.4.1 (alpha = 2^-20)
    CHECK(rct_cutoff(0.5) == 41);
    CHECK_MSG(apt_cutoff(1.0) == 589, std::to_string(apt_cutoff(1.0)));   // tabla de 4.4.2, W = 1024
    CHECK(apt_cutoff(0.5) == 793);
    // Menos entropia asumida => umbrales mas altos (mas tolerantes).
    for (double h = 0.2; h < 1.0; h += 0.1) {
        CHECK(rct_cutoff(h) >= rct_cutoff(h + 0.1));
        CHECK(apt_cutoff(h) >= apt_cutoff(h + 0.1));
    }
}

TEST(apt_false_alarm_rate_is_bounded) {
    // Con la fuente exactamente en el limite (p = 2^-H), la probabilidad de
    // alarma por ventana no debe superar alpha. Se comprueba con alpha grande.
    const double h = 0.8, alpha_log2 = -6;     // alpha = 1/64
    const uint32_t c = apt_cutoff(h, alpha_log2);
    const double p = std::pow(2.0, -h);
    std::mt19937_64 r(1);
    std::bernoulli_distribution d(p);
    int alarms = 0, windows = 20000;
    for (int w = 0; w < windows; ++w) {
        uint32_t b = 1;   // la primera muestra es la de referencia; la fijamos al valor probable
        for (uint32_t i = 1; i < APT_WINDOW; ++i) b += d(r) ? 1 : 0;
        alarms += b >= c;
    }
    double rate = (double)alarms / windows;
    CHECK_MSG(rate <= 1.0 / 64 * 1.3, "tasa " + std::to_string(rate));
}

// ------------------------------------------------------------ tests de salud --
TEST(health_no_false_alarm_on_good_source) {
    Bits b = iid_bits(2'000'000, 0.5, 2);
    HealthResult r = run_health_tests(b);
    CHECK(r.healthy() && r.startup_ok);
}

TEST(health_detects_stuck_source) {
    Bits b = iid_bits(5000, 0.5, 3);
    for (size_t i = 3000; i < b.size(); ++i) b[i] = 1;
    HealthResult r = run_health_tests(b);
    CHECK(r.rct_fail_idx > 3000 && r.rct_fail_idx <= 3000 + 40);
    Bits z(100, 0);
    r = run_health_tests(z);
    CHECK(r.rct_fail_idx == 40);   // 41 ceros seguidos (indices 0..40)
    CHECK(!r.startup_ok);
}

TEST(health_detects_biased_source) {
    Bits b = iid_bits(4096, 0.875, 4);
    HealthConfig hc;
    hc.rct_cutoff = 100000;        // solo el APT
    HealthResult r = run_health_tests(b, hc);
    CHECK(r.apt_fail_idx >= 0 && r.apt_fail_idx < 1024);
}

TEST(health_tests_miss_alternating_pattern_but_entropy_estimate_catches_it) {
    // Limitacion conocida de RCT/APT: 0101... pasa ambos tests.
    Bits b(100000);
    for (size_t i = 0; i < b.size(); ++i) b[i] = i & 1;
    CHECK(run_health_tests(b).healthy());
    EntropyEstimate e = estimate_min_entropy(b);
    CHECK_MSG(e.markov < 0.01, std::to_string(e.markov));
    CHECK(e.min < 0.01);
}

// ---------------------------------------------------------------- entropia --
TEST(entropy_estimates_are_consistent) {
    EntropyEstimate e = estimate_min_entropy(iid_bits(1'000'000, 0.5, 5));
    CHECK_MSG(e.mcv > 0.99 && e.markov > 0.99, std::to_string(e.mcv) + " " + std::to_string(e.markov));
    // p = 0.75 => H_min = -log2(0.75) = 0.415
    e = estimate_min_entropy(iid_bits(1'000'000, 0.75, 6));
    CHECK_MSG(std::fabs(e.mcv - 0.415) < 0.01, std::to_string(e.mcv));
    CHECK(e.min <= e.mcv);
    // Fuente correlada (Markov con P(repetir) = 0.9): MCV ~1 pero Markov la detecta.
    std::mt19937_64 r(7);
    std::bernoulli_distribution keep(0.9);
    Bits m(1'000'000);
    m[0] = 0;
    for (size_t i = 1; i < m.size(); ++i) m[i] = keep(r) ? m[i - 1] : 1 - m[i - 1];
    e = estimate_min_entropy(m);
    CHECK(e.mcv > 0.98);
    CHECK_MSG(std::fabs(e.markov - (-std::log2(0.9))) < 0.02, std::to_string(e.markov));
}

// --------------------------------------------------------------- extractor --
TEST(extractor_length_formula) {
    CHECK(extractor_output_length(10000, 0.5) == 10000 / 2 - 128);
    CHECK(extractor_output_length(100, 0.5) == 0);
    CHECK(extractor_output_length(1000, 1.0, -64) == 1000 - 128);
}

TEST(extractor_removes_bias) {
    // Fuente muy sesgada (p = 0.8 => H = 0.32): la salida debe estar equilibrada.
    Bits raw = iid_bits(400000, 0.8, 8);
    CHECK(monobit_z(raw) > 100);
    int bad = 0;
    for (uint64_t s = 1; s <= 10; ++s) {
        Bits out = extract_random(raw, 0.3, s);
        CHECK(out.size() == extractor_output_length(raw.size(), 0.3));
        bad += monobit_z(out) > 3.29;    // p < 0.001
    }
    CHECK_MSG(bad <= 1, std::to_string(bad) + " semillas con sesgo");
    CHECK(extract_random(raw, 0.3, 1) != extract_random(raw, 0.3, 2));
}

TEST(load_capture_words) {
    const char *path = "trng_words_tmp.txt";
    {
        std::ofstream f(path);
        f << "# comentario\n80000001\n0xFFFFFFFF\r\n";
    }
    Bits b;
    std::string err;
    CHECK(load_trng_words(path, b, &err));
    CHECK(b.size() == 64 && b[0] == 1 && b[1] == 0 && b[31] == 1 && b[32] == 1);
    {
        std::ofstream f(path);
        f << "zz\n";
    }
    CHECK(!load_trng_words(path, b, &err));
    std::remove(path);
}

int main() { return tf::run_all("AURORA TRNG (software)"); }
