// =============================================================================
// AURORA - Tests de la reconciliacion LDPC (sw/aurora_ldpc.*)
// =============================================================================
#include <map>
#include <random>
#include <string>

#include "../../sw/aurora_auth.h"
#include "../../sw/aurora_ldpc.h"
#include "../../sw/aurora_random.h"
#include "test_framework.h"

using namespace aurora;

static Bits random_bits(std::mt19937_64 &r, size_t n) {
    Bits b(n);
    for (auto &v : b) v = r() & 1;
    return b;
}
static Bits bsc(std::mt19937_64 &r, const Bits &a, double q) {
    Bits b = a;
    std::bernoulli_distribution flip(q);
    for (auto &v : b) v ^= flip(r) ? 1 : 0;
    return b;
}
static std::vector<float> llr_for(const Bits &y, double q) {
    const float l0 = (float)std::log((1 - q) / q);
    std::vector<float> l(y.size());
    for (size_t i = 0; i < y.size(); ++i) l[i] = y[i] ? -l0 : l0;
    return l;
}

// --------------------------------------------------------------- codigos ----
TEST(code_structure_matches_design) {
    LdpcDegrees d;
    d.d_high = 12;
    d.frac_high = 0.25;
    LdpcCode c = ldpc_build(16384, 2458, 77, d);
    CHECK(c.n == 16384 && c.m == 2458);
    std::vector<int> vdeg(c.n, 0);
    for (uint32_t i = 0; i < c.m; ++i)
        for (uint32_t e = c.chk_ptr[i]; e < c.chk_ptr[i + 1]; ++e) ++vdeg[c.chk_var[e]];
    std::map<int, int> hist;
    for (int v : vdeg) ++hist[v];
    CHECK_MSG(hist[2] == 2457, std::to_string(hist[2]));           // escalera: m - 1 nodos de grado 2
    CHECK_MSG(hist[12] == 4096, std::to_string(hist[12]));         // 25 % de grado alto
    CHECK(hist[2] + hist[3] + hist[12] == 16384);
    CHECK(ldpc_girth_at_least_6(c));
    // Grados de comprobacion casi uniformes.
    uint32_t dmin = 1u << 30, dmax = 0;
    for (uint32_t i = 0; i < c.m; ++i) {
        dmin = std::min(dmin, c.chk_ptr[i + 1] - c.chk_ptr[i]);
        dmax = std::max(dmax, c.chk_ptr[i + 1] - c.chk_ptr[i]);
    }
    CHECK_MSG(dmax - dmin <= 3, std::to_string(dmin) + ".." + std::to_string(dmax));
}

TEST(family_codes_are_deterministic_and_cached) {
    auto a = ldpc_code(0.85), b = ldpc_code(0.85);
    CHECK(a.get() == b.get());
    LdpcCode c = ldpc_build(LDPC_N, a->m, 0x4C4450434155524Full ^ ((uint64_t)LDPC_N << 32) ^ a->m,
                            ldpc_default_degrees(0.85));
    CHECK(c.chk_var == a->chk_var && c.chk_ptr == a->chk_ptr);   // Alice y Bob construyen la misma H
    for (double R : ldpc_family_rates()) {
        auto code = ldpc_code(R);
        CHECK(std::fabs(code->rate() - R) < 1e-3);
        // Sin ciclos de 4 hasta R = 0.90. En R = 0.95 hay demasiado pocas
        // comprobaciones (819) para los grados elegidos: quedan algunos.
        if (R <= 0.901) CHECK_MSG(ldpc_girth_at_least_6(*code), "R=" + std::to_string(R));
    }
}

TEST(untainted_puncturing_keeps_punctured_bits_recoverable) {
    auto c = ldpc_code(0.80);
    std::mt19937_64 r(12);
    SplitMix64 pub(3);
    std::vector<uint32_t> order = random_permutation(c->n, pub);
    std::vector<uint32_t> p = ldpc_untainted_puncturing(*c, order, 1638);
    CHECK_MSG(p.size() > 300, std::to_string(p.size()));
    // Ninguna comprobacion conecta con dos perforados.
    std::vector<int> cnt(c->m, 0);
    for (uint32_t v : p)
        for (uint32_t e = c->var_ptr[v]; e < c->var_ptr[v + 1]; ++e) ++cnt[c->var_chk[e]];
    for (int x : cnt) CHECK(x <= 1);
}

TEST(syndrome_is_linear) {
    std::mt19937_64 r(1);
    auto c = ldpc_code(0.85);
    Bits x = random_bits(r, c->n), y = random_bits(r, c->n), z(c->n);
    for (uint32_t i = 0; i < c->n; ++i) z[i] = x[i] ^ y[i];
    Bits sx = ldpc_syndrome(*c, x), sy = ldpc_syndrome(*c, y), sz = ldpc_syndrome(*c, z);
    for (uint32_t i = 0; i < c->m; ++i) CHECK(sz[i] == (sx[i] ^ sy[i]));
}

// ----------------------------------------------------------- decodificador --
TEST(decoder_corrects_below_threshold_both_algorithms) {
    std::mt19937_64 r(2);
    auto c = ldpc_code(0.85);
    for (LdpcAlgo algo : {LdpcAlgo::SUM_PRODUCT, LdpcAlgo::MIN_SUM}) {
        for (int t = 0; t < 4; ++t) {
            Bits xa = random_bits(r, c->n), xb = bsc(r, xa, 0.01), x;
            LdpcDecodeResult d = ldpc_decode(*c, ldpc_syndrome(*c, xa), llr_for(xb, 0.01), x, 100, algo);
            CHECK(d.ok && x == xa);
        }
    }
}

TEST(decoder_reports_failure_above_capacity) {
    std::mt19937_64 r(3);
    auto c = ldpc_code(0.90);   // h(0.03) = 0.19 > 1 - R = 0.10: imposible
    Bits xa = random_bits(r, c->n), xb = bsc(r, xa, 0.03), x;
    LdpcDecodeResult d = ldpc_decode(*c, ldpc_syndrome(*c, xa), llr_for(xb, 0.03), x, 30);
    CHECK(!d.ok);
    CHECK(d.iterations == 30);
}

// ---------------------------------------------------------- planificacion ---
TEST(frame_plan_hits_target_efficiency) {
    for (double q : {0.01, 0.017, 0.03, 0.05}) {
        for (uint32_t k_avail : {100000u, 16384u, 9000u, 3000u}) {
            LdpcFramePlan p = ldpc_plan_frame(k_avail, q, 1.25);
            const std::string w = "q=" + std::to_string(q) + " k=" + std::to_string(k_avail);
            CHECK_MSG(p.k > 0 && p.k <= k_avail, w);
            CHECK_MSG(p.k + p.p + p.s == LDPC_N, w);
            CHECK_MSG(p.p <= LDPC_N / 10, w);
            const double f = p.leak() / (p.k * binary_entropy(q));
            CHECK_MSG(f >= 1.24 && f < 1.25 + 2.0 / (p.k * binary_entropy(q)) + 1e-9, w + " f=" + std::to_string(f));
        }
    }
    CHECK(ldpc_plan_frame(1000, 0.3, 1.25).k == 0);   // f h(Q) >= 1: no hay codigo
}

// ---------------------------------------------------------- reconciliacion --
static std::function<bool(size_t, Bits &)> prng_bits(uint64_t seed) {
    auto r = std::make_shared<std::mt19937_64>(seed);
    return [r](size_t n, Bits &o) {
        o.resize(n);
        for (auto &v : o) v = (*r)() & 1;
        return true;
    };
}

TEST(reconcile_real_qber_gives_equal_keys) {
    std::mt19937_64 r(4);
    Bits a = random_bits(r, 40755), b = bsc(r, a, 0.0172);
    LdpcConfig cfg;
    cfg.qber_hint = 0.0172;
    LdpcReconcileResult res = ldpc_reconcile(a, b, cfg, 99, prng_bits(5));
    CHECK(res.failed == 0);
    CHECK(res.alice == res.bob);
    CHECK(res.alice == a);                 // Alice conserva su clave tal cual
    CHECK(res.discarded_bits == 0 && res.alice.size() == a.size());
    uint32_t e = 0;
    for (size_t i = 0; i < a.size(); ++i) e += a[i] != b[i];
    CHECK(res.corrected == e);
    const double f = res.leaked_bits / (a.size() * binary_entropy((double)e / a.size()));
    CHECK_MSG(f > 1.0 && f < 1.6, "f=" + std::to_string(f));
}

TEST(reconcile_discards_failed_frames_and_keeps_rest_consistent) {
    // QBER real (6 %) muy por encima del estimado (1 %): las primeras tramas
    // fallan y se descartan; la estimacion sube y las siguientes funcionan.
    std::mt19937_64 r(6);
    Bits a = random_bits(r, 120000), b = bsc(r, a, 0.06);
    LdpcConfig cfg;
    cfg.qber_hint = 0.01;
    LdpcReconcileResult res = ldpc_reconcile(a, b, cfg, 7, prng_bits(8));
    CHECK(res.failed >= 1);
    CHECK(res.alice == res.bob);
    CHECK(res.discarded_bits + res.alice.size() == a.size());
    CHECK_MSG(res.failed < res.frames, "nunca se recupera");
}

TEST(reconcile_messages_are_one_round_trip_per_frame) {
    std::mt19937_64 r(9);
    Bits a = random_bits(r, 30000), b = bsc(r, a, 0.02);
    int a2b = 0, b2a = 0;
    LdpcIo io;
    io.alice_to_bob = [&](const std::vector<uint8_t> &m) { ++a2b; return m; };
    io.bob_to_alice = [&](const std::vector<uint8_t> &m) { ++b2a; return m; };
    LdpcReconcileResult res = ldpc_reconcile(a, b, LdpcConfig(), 1, prng_bits(2), &io);
    CHECK(a2b == (int)res.frames && b2a == (int)res.frames);
    CHECK(res.frames >= 2 && res.frames <= 3);
}

// ------------------------------------------------ cadena autenticada (LDPC) --
static AuthenticatedResult run_ldpc(ChannelAttacker *eve, uint64_t seed = 10) {
    std::mt19937_64 r(seed);
    Bits a = random_bits(r, 40000), b = bsc(r, a, 0.017), psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    AuthenticatedConfig cfg;
    cfg.reconciliation = Reconciliation::LDPC;
    SimulatedRandomSource rng(seed * 31 + 1);
    return run_authenticated_postprocessing(a, b, pa, pb, cfg, rng, eve);
}

TEST(authenticated_pipeline_with_ldpc) {
    AuthenticatedResult res = run_ldpc(nullptr);
    CHECK_MSG(res.ok, res.abort_reason);
    CHECK(!res.alice_key.empty() && res.alice_key == res.bob_key);
    CHECK(res.ec_frames >= 3 && res.ec_failed == 0 && res.reconciled == 40000);
    CHECK(res.auth_key_bits == 1152);
}

struct FlipLdpcMessage : ChannelAttacker {
    size_t which, byte;
    size_t seen = 0;
    FlipLdpcMessage(size_t w, size_t b) : which(w), byte(b) {}
    void message(int ph, std::vector<uint8_t> &m) override {
        if (ph == PHASE_CASCADE && seen++ == which && byte < m.size()) m[byte] ^= 1;
    }
};

TEST(attack_on_ldpc_messages_is_detected) {
    // 0: plan de la trama 1; 20: sindrome de la trama 1; mensaje 1: respuesta
    // de Bob (exito); 3: sindrome de la trama 2.
    for (auto wb : {std::make_pair<size_t, size_t>(0, 7), {0, 20}, {1, 0}, {1, 4}, {2, 100}}) {
        FlipLdpcMessage eve(wb.first, wb.second);
        AuthenticatedResult res = run_ldpc(&eve);
        const std::string w = "mensaje " + std::to_string(wb.first) + " byte " + std::to_string(wb.second);
        CHECK_MSG(!res.ok, w + ": no detectado");
        CHECK_MSG(res.alice_key.empty() && res.bob_key.empty(), w + ": se entrego clave");
    }
}

// ------------------------------------------------- coma fija (kernel de la PL) --
static int g_hw_calls = 0;
static void counting_decoder(const uint32_t *a, const uint16_t *b, uint32_t n, uint32_t m, uint32_t load,
                             const int8_t *llr, const uint8_t *syn, uint32_t it, uint8_t *x, uint32_t *st) {
    ++g_hw_calls;
    aurora_ldpc_dec(a, b, n, m, load, llr, syn, it, x, st);
}

TEST(reconcile_with_fixed_point_decoder) {
    std::mt19937_64 r(30);
    Bits a = random_bits(r, 40755), b = bsc(r, a, 0.0172);
    LdpcConfig cfg = ldpc_config_fixed(counting_decoder);
    cfg.qber_hint = 0.0172;
    g_hw_calls = 0;
    LdpcReconcileResult res = ldpc_reconcile(a, b, cfg, 5, prng_bits(6));
    CHECK(g_hw_calls == (int)res.frames);           // todas las tramas pasan por el decodificador
    CHECK(res.failed == 0 && res.alice == res.bob && res.alice == a);
    uint32_t e = 0;
    for (size_t i = 0; i < a.size(); ++i) e += a[i] != b[i];
    const double f = res.leaked_bits / (a.size() * binary_entropy((double)e / a.size()));
    CHECK_MSG(f > 1.3 && f < 1.7, "f=" + std::to_string(f));
}

TEST(fixed_point_decoder_is_deterministic_hw_sw_equivalent) {
    // El modelo SW y la PL ejecutan el mismo fuente: misma entrada => misma
    // salida e iteraciones. Aqui: dos instancias (distinto estado de "codigo
    // cargado") deben coincidir bit a bit.
    std::mt19937_64 r(31);
    auto c = ldpc_code(0.80);
    Bits xa = random_bits(r, c->n), xb = bsc(r, xa, 0.015), x1, x2;
    std::vector<float> llr = llr_for(xb, 0.015);
    LdpcDecodeResult d1 = ldpc_decode(*c, ldpc_syndrome(*c, xa), llr, x1, 60, LdpcAlgo::FIXED_MIN_SUM);
    LdpcDecodeResult d2 = ldpc_decode(*c, ldpc_syndrome(*c, xa), llr, x2, 60, LdpcAlgo::FIXED_MIN_SUM, 0.8f,
                                      counting_decoder);
    CHECK(d1.ok == d2.ok && d1.iterations == d2.iterations && x1 == x2);
    CHECK(d1.ok && x1 == xa);
}

TEST(authenticated_pipeline_with_fixed_point_ldpc) {
    std::mt19937_64 r(32);
    Bits a = random_bits(r, 40000), b = bsc(r, a, 0.017), psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    AuthenticatedConfig cfg;
    cfg.reconciliation = Reconciliation::LDPC;
    cfg.ldpc = ldpc_config_fixed();
    SimulatedRandomSource rng(77);
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, cfg, rng);
    CHECK_MSG(res.ok, res.abort_reason);
    CHECK(!res.alice_key.empty() && res.alice_key == res.bob_key);
}

int main() { return tf::run_all("AURORA reconciliacion LDPC"); }
