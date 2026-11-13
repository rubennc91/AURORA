// =============================================================================
// AURORA - Tests de los kernels nuevos (hw/) y del software del PS (sw/)
// Uso: test_kernels   (todo sintetico, no necesita los batches)
// =============================================================================
#include <cmath>
#include <cstdlib>
#include <random>
#include <set>
#include <string>

#include "../../sw/aurora_host.h"
#include "synth.h"
#include "test_framework.h"

using namespace aurora;

// ----------------------------------------------------------------- modelos ---
// Referencias "de libro", O(N^2) o directas, independientes de los kernels.

static std::vector<uint32_t> brute_histogram(const Detections &a, const Detections &b, int64_t lag_min,
                                             uint32_t shift, uint32_t bins) {
    std::vector<uint32_t> h(bins, 0);
    const int64_t lag_max = lag_min + ((int64_t)bins << shift);
    for (auto tb : b.tags)
        for (auto ta : a.tags) {
            int64_t d = (int64_t)ta - (int64_t)tb;
            if (d >= lag_min && d < lag_max) h[(uint64_t)(d - lag_min) >> shift]++;
        }
    return h;
}

struct RefSift { uint32_t coinc = 0; Bits ka, kb; };
static RefSift brute_sift(const Detections &a, const Detections &b, int64_t lag, int64_t w) {
    RefSift r;
    std::vector<bool> used(a.size(), false);
    for (size_t j = 0; j < b.size(); ++j) {
        for (size_t i = 0; i < a.size(); ++i) {
            if (used[i]) continue;
            int64_t d = (int64_t)a.tags[i] - (int64_t)b.tags[j] - lag;
            if (d < -w) continue;
            if (d > w) break;
            used[i] = true;
            r.coinc++;
            if ((a.states[i] >> 1) == (b.states[j] >> 1)) {
                r.ka.push_back(a.states[i] & 1);
                r.kb.push_back(b.states[j] & 1);
            }
            break;
        }
    }
    return r;
}

static Detections random_dets(std::mt19937_64 &rng, size_t n, uint64_t t0, uint64_t mean_gap) {
    Detections d;
    std::uniform_int_distribution<uint64_t> gap(0, 2 * mean_gap);
    std::uniform_int_distribution<int> st(0, 3);
    uint64_t t = t0;
    for (size_t i = 0; i < n; ++i) { t += gap(rng); d.tags.push_back(t); d.states.push_back((uint8_t)st(rng)); }
    return d;
}

static uint32_t gf2_rank(std::vector<std::vector<uint8_t>> m) {
    uint32_t rank = 0;
    if (m.empty()) return 0;
    const size_t rows = m.size(), cols = m[0].size();
    for (size_t c = 0; c < cols && rank < rows; ++c) {
        size_t p = rank;
        while (p < rows && !m[p][c]) ++p;
        if (p == rows) continue;
        std::swap(m[p], m[rank]);
        for (size_t r = 0; r < rows; ++r)
            if (r != rank && m[r][c])
                for (size_t k = c; k < cols; ++k) m[r][k] ^= m[rank][k];
        ++rank;
    }
    return rank;
}

// ------------------------------------------------------------ sincronizacion --
TEST(sync_histogram_matches_bruteforce) {
    std::mt19937_64 rng(11);
    int incomplete = 0;
    for (int trial = 0; trial < 20; ++trial) {
        Detections a = random_dets(rng, 300, 1000 + trial, 400);
        Detections b = random_dets(rng, 200, 900, 600);
        int64_t lag_min = -(int64_t)(rng() % 5000);
        uint32_t shift = (uint32_t)(rng() % 6), bins = 1 + (uint32_t)(rng() % AURORA_SYNC_MAX_BINS);
        std::vector<uint32_t> h(AURORA_SYNC_MAX_BINS);
        SyncResult r{};
        aurora_sync(a.tags.data(), (uint32_t)a.size(), b.tags.data(), (uint32_t)b.size(), lag_min, shift, bins,
                    h.data(), &r);
        auto ref = brute_histogram(a, b, lag_min, shift, bins);
        // Con rangos muy anchos la ventana del kernel (32 tags) puede
        // desbordarse: entonces debe indicarlo (complete = 0). Si dice que el
        // resultado es completo, tiene que ser EXACTO.
        if (!r.complete) { ++incomplete; continue; }
        bool same = true;
        uint64_t tot = 0;
        for (uint32_t i = 0; i < bins; ++i) { same &= h[i] == ref[i]; tot += ref[i]; }
        CHECK_MSG(same, "trial " + std::to_string(trial));
        CHECK(r.total_pairs == tot);
        CHECK(r.peak_count == *std::max_element(ref.begin(), ref.end()));
    }
    CHECK_MSG(incomplete <= 5, std::to_string(incomplete) + " casos incompletos de 20");
}

TEST(sync_flags_window_overflow) {
    // Muchisimos tags de Alice dentro del rango de cada Bob: el kernel no puede
    // contarlos todos y debe marcarlo; el PS no debe dar el lag por bueno.
    Detections a, b;
    for (uint64_t i = 0; i < 5000; ++i) { a.tags.push_back(1000000 + i * 10); a.states.push_back(0); }
    for (uint64_t i = 0; i < 100; ++i) { b.tags.push_back(1000000 + i * 500); b.states.push_back(0); }
    std::vector<uint32_t> h(AURORA_SYNC_MAX_BINS);
    SyncResult r{};
    aurora_sync(a.tags.data(), (uint32_t)a.size(), b.tags.data(), (uint32_t)b.size(), -2000, 3, 512, h.data(), &r);
    CHECK(r.complete == 0);
    CHECK(!estimate_lag(a, b).ok);
}

TEST(sync_recovers_injected_lag) {
    // Incluye lags fuera del rango +-500 ps de la version anterior.
    const int64_t lags[] = {0, 137, -250, -3000, 90'000, -400'000};
    for (int64_t L : lags) {
        synth::Params p;
        p.lag = L;
        p.seed = 100 + (uint64_t)std::llabs(L);
        Detections a, b;
        synth::generate(p, a, b);
        LagEstimate e = estimate_lag(a, b);
        CHECK_MSG(e.ok, "lag " + std::to_string(L));
        CHECK_MSG(std::fabs(e.lag - (double)L) < 20.0,
                  "lag inyectado " + std::to_string(L) + " estimado " + std::to_string(e.lag));
        CHECK_MSG(std::fabs(e.sigma - p.jitter) < 40.0, "sigma " + std::to_string(e.sigma));
    }
}

TEST(sync_rejects_uncorrelated_data) {
    synth::Params p;
    p.pair_rate = 0;   // solo ruido
    Detections a, b;
    synth::generate(p, a, b);
    LagEstimate e = estimate_lag(a, b);
    CHECK_MSG(!e.ok, "snr " + std::to_string(e.snr));
}

TEST(sync_handles_empty_inputs) {
    Detections a, b;
    LagEstimate e = estimate_lag(a, b);
    CHECK(!e.ok);
}

// ------------------------------------------------------------------ sifting --
TEST(sift_matches_bruteforce_reference) {
    std::mt19937_64 rng(7);
    for (int trial = 0; trial < 40; ++trial) {
        Detections a = random_dets(rng, 400, (trial % 4 == 0) ? 0 : 5000, 300);  // a veces empieza en t=0
        Detections b = random_dets(rng, 250, 0, 500);
        int64_t lag = (int64_t)(rng() % 2001) - 1000;
        int64_t w = 50 + (int64_t)(rng() % 400);
        SiftOutput s = sift_batch(a, b, lag, (uint32_t)w);
        RefSift r = brute_sift(a, b, lag, w);
        CHECK_MSG(s.n_coincidences == r.coinc, "trial " + std::to_string(trial) + ": " +
                                                   std::to_string(s.n_coincidences) + " vs " + std::to_string(r.coinc));
        CHECK_MSG(s.alice_key == r.ka && s.bob_key == r.kb, "claves distintas, trial " + std::to_string(trial));
    }
}

TEST(sift_no_unsigned_underflow_near_zero) {
    // Tags pegados a t=0 y lag negativo: la version anterior perdia coincidencias.
    Detections a{{100, 5000, 9000}, {0, 1, 2}};
    Detections b{{150, 5050, 9050}, {0, 1, 2}};
    SiftOutput s = sift_batch(a, b, -200, 350);
    CHECK(s.n_coincidences == 3);
    CHECK(s.alice_key.size() == 3);
}

TEST(sift_chunked_equals_single_pass) {
    synth::Params p;
    p.duration = 3'000'000'000ULL;
    Detections a, b;
    synth::generate(p, a, b);
    SiftOutput ref = sift_batch(a, b, 0, 350, AURORA_MAX_TAGS);
    for (uint32_t chunk : {3u, 17u, 256u, 1000u, 4096u}) {
        SiftOutput s = sift_batch(a, b, 0, 350, chunk);
        CHECK_MSG(s.n_coincidences == ref.n_coincidences && s.alice_key == ref.alice_key && s.bob_key == ref.bob_key,
                  "chunk " + std::to_string(chunk) + ": " + std::to_string(s.n_coincidences) + " vs " +
                      std::to_string(ref.n_coincidences));
        CHECK(s.n_chunks >= 1);
    }
}

TEST(sift_processes_batches_larger_than_kernel) {
    // ~170k eventos de Alice: mas que AURORA_MAX_TAGS => troceado automatico.
    synth::Params p;
    p.duration = 120'000'000'000ULL;
    Detections a, b;
    synth::generate(p, a, b);
    CHECK(a.size() > AURORA_MAX_TAGS);
    SiftOutput s = sift_batch(a, b, 0, 350);
    SiftOutput small = sift_batch(a, b, 0, 350, 5000);
    CHECK(s.n_chunks > 1);
    CHECK(s.alice_key == small.alice_key);
    // Fraccion de pares reales recuperados ~ P(|N(0,200)| <= 350) = 0.92
    double expected = 30e-9 * 120e9 * 0.92;
    CHECK_MSG(std::fabs(s.n_coincidences - expected) < 0.05 * expected, std::to_string(s.n_coincidences));
}

TEST(sift_qber_matches_injected) {
    for (double q : {0.0, 0.01, 0.05}) {
        synth::Params p;
        p.qber = q;
        p.duration = 100'000'000'000ULL;
        p.seed = 42;
        Detections a, b;
        synth::generate(p, a, b);
        SiftOutput s = sift_batch(a, b, 0, 350);
        uint32_t e = 0;
        for (size_t k = 0; k < s.alice_key.size(); ++k) e += s.alice_key[k] != s.bob_key[k];
        double meas = (double)e / s.alice_key.size();
        // Los accidentales anaden ~ 0.5 * (acc/coinc) de error.
        CHECK_MSG(std::fabs(meas - q) < 0.01, "QBER inyectado " + std::to_string(q) + " medido " + std::to_string(meas));
        CHECK(s.alice_key.size() > 0.45 * s.n_coincidences && s.alice_key.size() < 0.55 * s.n_coincidences);
    }
}

TEST(sift_handles_empty_inputs) {
    Detections a, b{{10, 20}, {0, 1}};
    SiftOutput s = sift_batch(a, b, 0, 350);
    CHECK(s.n_coincidences == 0 && s.alice_key.empty());
    s = sift_batch(b, a, 0, 350);
    CHECK(s.n_coincidences == 0 && s.alice_key.empty());
}

// ------------------------------------------------------------------ Toeplitz --
TEST(toeplitz_matches_definition) {
    std::mt19937_64 rng(3);
    for (int trial = 0; trial < 30; ++trial) {
        uint32_t n = 1 + (uint32_t)(rng() % 300), m = 1 + (uint32_t)(rng() % n);
        Bits x(n), seed(n + m - 1);
        for (auto &v : x) v = rng() & 1;
        for (auto &v : seed) v = rng() & 1;
        auto xs = pack_bits(x), ss = pack_bits(seed);
        std::vector<uint64_t> out((m + 63) / 64 + 1, ~0ULL);
        aurora_toeplitz(xs.data(), n, ss.data(), out.data(), m);
        Bits got = unpack_bits(out.data(), m);
        bool same = true;
        for (uint32_t i = 0; i < m; ++i) {
            uint8_t v = 0;
            for (uint32_t j = 0; j < n; ++j) v ^= seed[i + n - 1 - j] & x[j];
            same &= got[i] == v;
        }
        CHECK_MSG(same, "n=" + std::to_string(n) + " m=" + std::to_string(m));
        // Bits por encima de m en la ultima palabra deben quedar a 0.
        if (m % 64) CHECK((out[(m - 1) / 64] >> (m % 64)) == 0);
    }
}

TEST(toeplitz_has_full_rank) {
    // La version anterior tenia rango 32 para 1000 -> 500 bits.
    const uint32_t n = 1000, m = 500;
    std::vector<std::vector<uint8_t>> mat(m, std::vector<uint8_t>(n));
    for (uint32_t j = 0; j < n; ++j) {
        Bits e(n, 0);
        e[j] = 1;
        Bits col = toeplitz_hash(e, m, 99999);
        for (uint32_t i = 0; i < m; ++i) mat[i][j] = col[i];
    }
    uint32_t r = gf2_rank(mat);
    CHECK_MSG(r == m, "rango " + std::to_string(r));
}

TEST(toeplitz_is_linear_and_seed_dependent) {
    std::mt19937_64 rng(5);
    Bits x(2000), y(2000), xy(2000);
    for (size_t i = 0; i < x.size(); ++i) { x[i] = rng() & 1; y[i] = rng() & 1; xy[i] = x[i] ^ y[i]; }
    Bits hx = toeplitz_hash(x, 1500, 1), hy = toeplitz_hash(y, 1500, 1), hxy = toeplitz_hash(xy, 1500, 1);
    bool lin = true;
    for (size_t i = 0; i < hx.size(); ++i) lin &= (hx[i] ^ hy[i]) == hxy[i];
    CHECK(lin);
    CHECK(toeplitz_hash(x, 1500, 1) != toeplitz_hash(x, 1500, 2));
}

TEST(toeplitz_hash_handles_inputs_larger_than_kernel) {
    // Regresion: el kernel ignora entradas > AURORA_PA_MAX_IN_BITS y el host
    // devolvia una clave de ceros (igual en Alice y Bob).
    std::mt19937_64 rng(21);
    const uint32_t n = 3 * AURORA_PA_MAX_IN_BITS + 12345, m = (uint32_t)(0.7 * n);
    Bits x(n);
    for (auto &v : x) v = rng() & 1;
    Bits h = toeplitz_hash(x, m, 99);
    CHECK(h.size() == m);
    size_t ones = 0;
    for (auto v : h) ones += v;
    CHECK_MSG(std::fabs((double)ones - m / 2.0) < 5 * std::sqrt(m / 4.0), std::to_string(ones));
    CHECK(h == toeplitz_hash(x, m, 99));
    // Cada bloque depende de su parte de la entrada.
    Bits y = x;
    y[n - 1] ^= 1;
    Bits hy = toeplitz_hash(y, m, 99);
    CHECK(hy != h);
    CHECK(std::equal(h.begin(), h.begin() + m / 2, hy.begin()));   // bloques iniciales intactos
    // Cerca del limite, con un unico bloque, coincide con el kernel directo.
    Bits small(AURORA_PA_MAX_IN_BITS);
    for (auto &v : small) v = rng() & 1;
    CHECK(toeplitz_hash(small, 1000, 5).size() == 1000);
}

// ------------------------------------------------------------ Cascade y PA ----
TEST(permutation_is_bijection) {
    // La version anterior fallaba en n = 901, 1802, 2703, 3604.
    SplitMix64 rng(1);
    for (uint32_t n = 1; n <= 4096; ++n) {
        auto p = random_permutation(n, rng);
        std::vector<uint8_t> seen(n, 0);
        bool ok = true;
        for (auto v : p) { ok &= v < n && !seen[v]; if (v < n) seen[v] = 1; }
        CHECK_MSG(ok, "n=" + std::to_string(n));
        if (!ok) break;
    }
}

TEST(cascade_corrects_all_errors) {
    std::mt19937_64 rng(9);
    for (double q : {0.0, 0.005, 0.02, 0.05, 0.08}) {
        const uint32_t n = 40000;
        Bits a(n), b(n);
        uint32_t injected = 0;
        std::bernoulli_distribution flip(q);
        for (uint32_t i = 0; i < n; ++i) { a[i] = rng() & 1; b[i] = a[i] ^ (flip(rng) ? 1 : 0); injected += a[i] != b[i]; }
        CascadeConfig cfg;
        cfg.qber_hint = std::max(q, 0.005);
        CascadeResult r = cascade_correct(a, b, cfg);
        CHECK_MSG(a == b, "QBER " + std::to_string(q) + ": quedan errores");
        CHECK(r.corrected == injected);
        if (q > 0) {
            double f = r.leaked_bits / (n * binary_entropy((double)injected / n));
            CHECK_MSG(f < 1.35, "eficiencia f=" + std::to_string(f) + " con QBER " + std::to_string(q));
        }
    }
}

TEST(verification_hash_detects_single_bit_difference) {
    std::mt19937_64 rng(13);
    Bits a(10000);
    for (auto &v : a) v = rng() & 1;
    for (int t = 0; t < 200; ++t) {
        Bits b = a;
        b[rng() % b.size()] ^= 1;
        CHECK(verification_hash(a, 77 + t) != verification_hash(b, 77 + t));
    }
    CHECK(verification_hash(a, 5) == verification_hash(a, 5));
}

TEST(pa_length_is_sane) {
    CHECK(pa_output_length(100000, 0.0, 0, 0) > 99000);
    CHECK(pa_output_length(100000, 0.02, 20000, 64) < pa_output_length(100000, 0.01, 20000, 64));
    CHECK(pa_output_length(100000, 0.11, 50000, 64) == 0);   // por encima del limite BB84
    CHECK(pa_output_length(100, 0.02, 30, 64) == 0);          // bloque demasiado pequeno
}

// ------------------------------------------------------------------- E/S -----
TEST(loader_validates_input) {
    Detections ok{{1, 2, 3}, {0, 1, 3}}, unsorted{{1, 3, 2}, {0, 1, 2}}, bad_state{{1, 2}, {0, 4}};
    std::string err;
    CHECK(validate_detections(ok, &err));
    CHECK(!validate_detections(unsorted, &err));
    CHECK(!validate_detections(bad_state, &err));
    Detections d;
    CHECK(!load_detections("/no/existe", d, &err));
}

TEST(end_to_end_synthetic_pipeline) {
    synth::Params p;
    p.lag = 12345;
    p.qber = 0.025;
    p.duration = 400'000'000'000ULL;   // 0.4 s
    Detections a, b;
    synth::generate(p, a, b);
    LagEstimate e = estimate_lag(a, b);
    CHECK(e.ok && std::fabs(e.lag - p.lag) < 20);
    SiftOutput s = sift_batch(a, b, (int64_t)std::llround(e.lag), 350);
    Bits ka = s.alice_key, kb = s.bob_key;
    CascadeConfig cc;
    cc.qber_hint = 0.03;
    CascadeResult cr = cascade_correct(ka, kb, cc);
    CHECK(ka == kb);
    double q = (double)cr.corrected / ka.size();
    uint32_t m = pa_output_length((uint32_t)ka.size(), q, cr.leaked_bits, 64);
    CHECK_MSG(m > 0.5 * ka.size(), "m=" + std::to_string(m) + " de " + std::to_string(ka.size()));
    CHECK(toeplitz_hash(ka, m, 1) == toeplitz_hash(kb, m, 1));
}

int main() { return tf::run_all("AURORA kernels nuevos + PS"); }
