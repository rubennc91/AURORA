// =============================================================================
// AURORA - Tests de la autenticacion Wegman-Carter del canal clasico
// =============================================================================
#include <random>
#include <string>

#include "../../sw/aurora_auth.h"
#include "../../sw/aurora_random.h"
#include "test_framework.h"

using namespace aurora;

static U128 rnd128(std::mt19937_64 &r) { return U128{r(), r()}; }

static Bits random_bits(std::mt19937_64 &r, size_t n) {
    Bits b(n);
    for (auto &v : b) v = r() & 1;
    return b;
}

// Par de claves cribadas con QBER dado.
static void sifted_pair(std::mt19937_64 &r, size_t n, double q, Bits &a, Bits &b) {
    a = random_bits(r, n);
    b = a;
    std::bernoulli_distribution flip(q);
    for (auto &v : b) v ^= flip(r) ? 1 : 0;
}

// ----------------------------------------------------------------- GF(2^128) --
TEST(gf128_is_a_field) {
    std::mt19937_64 r(1);
    const U128 one{0, 1}, zero{0, 0};
    for (int t = 0; t < 200; ++t) {
        U128 a = rnd128(r), b = rnd128(r), c = rnd128(r);
        CHECK(gf128_mul(a, b) == gf128_mul(b, a));
        CHECK(gf128_mul(gf128_mul(a, b), c) == gf128_mul(a, gf128_mul(b, c)));
        CHECK(gf128_mul(a, b ^ c) == (gf128_mul(a, b) ^ gf128_mul(a, c)));
        CHECK(gf128_mul(a, one) == a);
        CHECK(gf128_mul(a, zero) == zero);
    }
    // Reduccion: x^127 * x = x^128 = x^7 + x^2 + x + 1.
    CHECK(gf128_mul(U128{1ULL << 63, 0}, U128{0, 2}) == (U128{0, 0x87}));
}

TEST(gf128_every_nonzero_element_has_inverse) {
    // a^(2^128 - 2) = a^-1 solo si el polinomio de reduccion es irreducible.
    std::mt19937_64 r(2);
    for (int t = 0; t < 20; ++t) {
        U128 a = rnd128(r), inv{0, 1}, sq = a;
        for (int i = 1; i < 128; ++i) {   // producto de a^(2^i), i = 1..127
            sq = gf128_mul(sq, sq);
            inv = gf128_mul(inv, sq);
        }
        CHECK(gf128_mul(a, inv) == (U128{0, 1}));
    }
}

TEST(poly_hash_distinguishes_padding_and_length) {
    std::mt19937_64 r(3);
    U128 k = rnd128(r);
    const uint8_t m1[] = {'a', 'b', 'c'}, m2[] = {'a', 'b', 'c', 0};
    CHECK(poly_hash(k, m1, 3) != poly_hash(k, m2, 4));
    CHECK(poly_hash(k, m2 + 3, 0) != poly_hash(k, m2 + 3, 1));   // vacio vs un byte 0
    CHECK(poly_hash(k, m1, 3) == poly_hash(k, m1, 3));
}

TEST(u128_hex_roundtrip) {
    std::mt19937_64 r(4);
    for (int t = 0; t < 50; ++t) {
        U128 a = rnd128(r), b;
        CHECK(u128_from_hex(u128_hex(a), b) && a == b);
    }
    U128 x;
    CHECK(!u128_from_hex("xyz", x));
}

// ---------------------------------------------------------------- reserva ----
TEST(key_pool_never_reuses_bits) {
    std::mt19937_64 r(5);
    Bits init = random_bits(r, 200000);
    KeyPool p(init);
    Bits got, chunk;
    while (p.take(1000, chunk)) got.insert(got.end(), chunk.begin(), chunk.end());
    CHECK(got == init);                       // en orden, sin solapes ni huecos
    CHECK(p.offset() == 200000 && p.available() == 0);
    CHECK(!p.take(1, chunk));                 // agotada
    p.add(Bits(10, 1));
    CHECK(p.available() == 10 && p.take(10, chunk) && p.offset() == 200010);
}

TEST(key_pool_does_not_consume_on_failure) {
    KeyPool p(Bits(100, 1));
    Bits b;
    CHECK(!p.take(101, b));
    CHECK(p.available() == 100 && p.offset() == 0);
}

// ------------------------------------------------------------- etiquetas -----
TEST(mac_accepts_honest_and_rejects_any_modification) {
    std::mt19937_64 r(6);
    Bits psk = random_bits(r, 128 * 1000);
    KeyPool pa(psk), pb(psk);
    Authenticator A(pa), B(pb);
    CHECK(A.ready() && B.ready());
    std::vector<uint8_t> msg(40);
    for (auto &v : msg) v = (uint8_t)r();

    uint64_t seq = 0;
    AuthTag t;
    CHECK(A.tag(ALICE_TO_BOB, seq, msg, t));
    CHECK(B.verify(ALICE_TO_BOB, seq, msg, t));
    ++seq;

    // Cualquier bit del mensaje.
    for (size_t bit = 0; bit < msg.size() * 8; ++bit) {
        auto bad = msg;
        bad[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        CHECK(A.tag(ALICE_TO_BOB, seq, msg, t));
        CHECK_MSG(!B.verify(ALICE_TO_BOB, seq, bad, t), "bit " + std::to_string(bit));
        ++seq;
    }
    // Cualquier bit de la etiqueta.
    for (int bit = 0; bit < 128; ++bit) {
        CHECK(A.tag(ALICE_TO_BOB, seq, msg, t));
        if (bit < 64) t.value.lo ^= 1ULL << bit; else t.value.hi ^= 1ULL << (bit - 64);
        CHECK(!B.verify(ALICE_TO_BOB, seq, msg, t));
        ++seq;
    }
    // Direccion (reflexion) y secuencia (reordenacion).
    CHECK(A.tag(ALICE_TO_BOB, seq, msg, t));
    CHECK(!B.verify(BOB_TO_ALICE, seq, msg, t));
    ++seq;
    CHECK(A.tag(ALICE_TO_BOB, seq, msg, t));
    CHECK(!B.verify(ALICE_TO_BOB, seq + 1, msg, t));
    ++seq;
    // Ambas reservas siguen sincronizadas tras los rechazos.
    CHECK(pa.offset() == pb.offset());
    CHECK(A.tag(ALICE_TO_BOB, seq, msg, t) && B.verify(ALICE_TO_BOB, seq, msg, t));
}

TEST(mac_rejects_replayed_tag) {
    std::mt19937_64 r(7);
    Bits psk = random_bits(r, 128 * 10);
    KeyPool pa(psk), pb(psk);
    Authenticator A(pa), B(pb);
    std::vector<uint8_t> msg = {1, 2, 3};
    AuthTag t;
    CHECK(A.tag(ALICE_TO_BOB, 0, msg, t) && B.verify(ALICE_TO_BOB, 0, msg, t));
    // Repetir la misma etiqueta: la OTP ya se gasto y el offset no coincide.
    CHECK(!B.verify(ALICE_TO_BOB, 0, msg, t));
}

TEST(mac_rejects_wrong_key_offset) {
    // Etiqueta valida pero con un offset que no coincide con la posicion del
    // receptor (desincronizacion o manipulacion de la cabecera): se rechaza.
    std::mt19937_64 r(13);
    Bits psk = random_bits(r, 128 * 10);
    KeyPool pa(psk), pb(psk);
    Authenticator A(pa), B(pb);
    std::vector<uint8_t> msg = {4, 5, 6};
    AuthTag t;
    CHECK(A.tag(ALICE_TO_BOB, 0, msg, t));
    t.key_offset += 128;
    CHECK(!B.verify(ALICE_TO_BOB, 0, msg, t));
}

TEST(mac_fails_without_key) {
    KeyPool small(Bits(128 + 64, 1));   // da para la clave del hash pero no para una OTP
    Authenticator A(small);
    AuthTag t;
    CHECK(A.ready());
    CHECK(!A.tag(ALICE_TO_BOB, 0, {1}, t));
    KeyPool empty;
    Authenticator B(empty);
    CHECK(!B.ready());
}

// ---------------------------------------------- post-procesado autenticado ---
// Fuente aleatoria de Alice para los tests (en la placa: el TRNG).
static SimulatedRandomSource &test_rng() {
    static SimulatedRandomSource r(0xA11CE);
    return r;
}

static AuthenticatedConfig default_cfg() {
    AuthenticatedConfig c;
    c.cascade.qber_hint = 0.02;
    return c;
}

TEST(authenticated_pipeline_honest_run) {
    std::mt19937_64 r(8);
    Bits a, b;
    sifted_pair(r, 40000, 0.017, a, b);
    Bits psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), test_rng());
    CHECK_MSG(res.ok, res.abort_reason);
    CHECK(res.alice_key == res.bob_key);
    CHECK(!res.alice_key.empty());
    // 128 (clave del hash) + 4 fases x 2 sentidos x 128 (OTP) = 1152 bits.
    CHECK_MSG(res.auth_key_bits == 1152, std::to_string(res.auth_key_bits));
    CHECK(res.net_key_bits == (int64_t)res.alice_key.size());
    // La reserva se repone: vuelve a tener lo mismo que al principio.
    CHECK(pa.available() == psk.size() && pb.available() == psk.size());
    CHECK(pa.offset() == pb.offset());
}

TEST(authenticated_pipeline_sustains_itself) {
    // Con una clave precompartida pequena, el sistema puede funcionar
    // indefinidamente reponiendo la reserva con clave QKD.
    std::mt19937_64 r(9);
    Bits psk = random_bits(r, 1152);
    KeyPool pa(psk), pb(psk);
    for (int round = 0; round < 10; ++round) {
        Bits a, b;
        sifted_pair(r, 20000, 0.02, a, b);
        AuthenticatedConfig c = default_cfg();
        AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, c, test_rng());
        CHECK_MSG(res.ok, "ronda " + std::to_string(round) + ": " + res.abort_reason);
        CHECK(res.alice_key == res.bob_key && res.net_key_bits > 0);
    }
}

struct FlipParity : ChannelAttacker {
    uint64_t target;
    explicit FlipParity(uint64_t t) : target(t) {}
    uint8_t parity(uint64_t i, uint8_t p) override { return i == target ? p ^ 1 : p; }
};
struct TamperMessage : ChannelAttacker {
    int phase;
    explicit TamperMessage(int ph) : phase(ph) {}
    void message(int ph, std::vector<uint8_t> &m) override { if (ph == phase && !m.empty()) m.back() ^= 1; }
};
struct TamperTag : ChannelAttacker {
    int phase;
    explicit TamperTag(int ph) : phase(ph) {}
    void tag(int ph, AuthTag &t) override { if (ph == phase) t.value.lo ^= 1; }
};
struct ReplayTag : ChannelAttacker {
    bool have = false;
    AuthTag saved;
    void tag(int, AuthTag &t) override {
        if (!have) { saved = t; have = true; } else t = saved;   // sustituye por una etiqueta antigua
    }
};

static void expect_abort(ChannelAttacker &eve, const std::string &what) {
    std::mt19937_64 r(10);
    Bits a, b;
    sifted_pair(r, 20000, 0.02, a, b);
    Bits psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), test_rng(), &eve);
    CHECK_MSG(!res.ok, what + ": el ataque no se detecto");
    CHECK_MSG(res.alice_key.empty() && res.bob_key.empty(), what + ": se entrego clave");
    CHECK_MSG(!res.abort_reason.empty(), what);
}

TEST(attack_on_cascade_parity_is_detected) {
    for (uint64_t idx : {0ull, 7ull, 500ull, 2000ull}) {
        FlipParity eve(idx);
        expect_abort(eve, "paridad " + std::to_string(idx));
    }
}

TEST(attack_on_protocol_messages_is_detected) {
    for (int ph : {PHASE_PARAMS, PHASE_VERIFY, PHASE_PA}) {
        TamperMessage eve(ph);
        expect_abort(eve, "mensaje de la fase " + std::to_string(ph));
    }
}

TEST(attack_on_tags_is_detected) {
    for (int ph : {PHASE_PARAMS, PHASE_CASCADE, PHASE_VERIFY, PHASE_PA}) {
        TamperTag eve(ph);
        expect_abort(eve, "etiqueta de la fase " + std::to_string(ph));
    }
    ReplayTag replay;
    expect_abort(replay, "repeticion de etiqueta");
}

TEST(pipeline_aborts_without_preshared_key) {
    std::mt19937_64 r(11);
    Bits a, b;
    sifted_pair(r, 10000, 0.02, a, b);
    KeyPool pa(Bits(500, 0)), pb(Bits(500, 0));
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), test_rng());
    CHECK(!res.ok && res.alice_key.empty());
}

TEST(pipeline_aborts_if_errors_remain) {
    // QBER muy alto y Cascade de 1 pase: quedan errores => la verificacion aborta.
    std::mt19937_64 r(12);
    Bits a, b;
    sifted_pair(r, 20000, 0.08, a, b);
    Bits psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    AuthenticatedConfig c = default_cfg();
    c.cascade.passes = 1;
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, c, test_rng());
    CHECK(!res.ok && res.alice_key.empty());
    CHECK_MSG(res.abort_reason.find("verificacion") != std::string::npos, res.abort_reason);
}

// ------------------------------------------------------ semillas publicas ---
// Fuente que registra cuantos bits se piden y puede fallar en la llamada N.
struct RecordingSource : RandomSource {
    std::vector<size_t> requests;
    int fail_at = -1;
    SimulatedRandomSource inner{7};
    bool random_bits(size_t n, Bits &out) override {
        requests.push_back(n);
        if ((int)requests.size() - 1 == fail_at) return false;
        return inner.random_bits(n, out);
    }
    const char *name() const override { return "registro"; }
};

TEST(seeds_are_drawn_fresh_in_protocol_order) {
    std::mt19937_64 r(20);
    Bits a, b;
    sifted_pair(r, 30000, 0.02, a, b);
    Bits psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    RecordingSource src;
    AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), src);
    CHECK_MSG(res.ok, res.abort_reason);
    // Cascade (64), verificacion (64, tras Cascade), Toeplitz completa (tras verificar).
    CHECK(src.requests.size() == 3);
    if (src.requests.size() == 3) {
        CHECK(src.requests[0] == 64 && src.requests[1] == 64);
        CHECK_MSG(src.requests[2] == toeplitz_seed_length(a.size(), res.pa_length),
                  std::to_string(src.requests[2]));
        CHECK(src.requests[2] == a.size() + res.pa_length - 1);   // un solo bloque: n + m - 1
    }
    CHECK(res.random_bits_used == 128 + src.requests.back());
}

TEST(each_round_uses_new_seeds) {
    // Mismas claves cribadas en dos rondas: las semillas cambian, la clave final tambien.
    std::mt19937_64 r(21);
    Bits a, b;
    sifted_pair(r, 20000, 0.02, a, b);
    Bits psk = random_bits(r, 4096);
    KeyPool pa(psk), pb(psk);
    SimulatedRandomSource src(99);
    AuthenticatedResult r1 = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), src);
    AuthenticatedResult r2 = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), src);
    CHECK(r1.ok && r2.ok);
    CHECK(r1.alice_key != r2.alice_key);
}

TEST(random_source_failure_aborts_without_key) {
    for (int fail_at : {0, 1, 2}) {
        std::mt19937_64 r(22);
        Bits a, b;
        sifted_pair(r, 20000, 0.02, a, b);
        Bits psk = random_bits(r, 4096);
        KeyPool pa(psk), pb(psk);
        RecordingSource src;
        src.fail_at = fail_at;
        AuthenticatedResult res = run_authenticated_postprocessing(a, b, pa, pb, default_cfg(), src);
        CHECK_MSG(!res.ok && res.alice_key.empty(), "fallo en la peticion " + std::to_string(fail_at));
        CHECK(res.abort_reason.find("fuente aleatoria") != std::string::npos);
    }
}

TEST(tampered_toeplitz_seed_is_detected) {
    // El atacante cambia un bit de la semilla Toeplitz (dentro del mensaje de PA).
    struct FlipSeedBit : ChannelAttacker {
        void message(int ph, std::vector<uint8_t> &m) override { if (ph == PHASE_PA && m.size() > 20) m[20] ^= 4; }
    } eve;
    expect_abort(eve, "semilla Toeplitz alterada");
}

int main() { return tf::run_all("AURORA autenticacion Wegman-Carter"); }
