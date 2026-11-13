// =============================================================================
// AURORA - Autenticacion Wegman-Carter: implementacion
// =============================================================================
#include "aurora_auth.h"
#include "aurora_random.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurora {

// --------------------------------------------------------------- GF(2^128) --
// Multiplicacion sin ramas dependientes de los datos (tiempo constante).
U128 gf128_mul(U128 a, U128 b) {
    U128 r{0, 0};
    U128 x = a;
    for (int i = 0; i < 128; ++i) {
        uint64_t bit = (i < 64) ? (b.lo >> i) & 1 : (b.hi >> (i - 64)) & 1;
        uint64_t m = 0 - bit;
        r.hi ^= x.hi & m;
        r.lo ^= x.lo & m;
        uint64_t carry = x.hi >> 63;
        x.hi = (x.hi << 1) | (x.lo >> 63);
        x.lo = (x.lo << 1) ^ ((0 - carry) & 0x87ULL);   // x^128 = x^7 + x^2 + x + 1
    }
    return r;
}

U128 u128_from_bytes(const uint8_t *b) {
    U128 v;
    for (int i = 0; i < 8; ++i) v.hi = (v.hi << 8) | b[i];
    for (int i = 8; i < 16; ++i) v.lo = (v.lo << 8) | b[i];
    return v;
}

U128 poly_hash(U128 k, const uint8_t *msg, size_t len) {
    U128 h{0, 0};
    uint8_t blk[16];
    for (size_t off = 0; off < len; off += 16) {
        size_t n = (len - off < 16) ? len - off : 16;
        for (size_t i = 0; i < 16; ++i) blk[i] = (i < n) ? msg[off + i] : 0;
        h = gf128_mul(h ^ u128_from_bytes(blk), k);
    }
    // Bloque de longitud (en bits): evita colisiones por relleno con ceros.
    U128 lenblk{0, (uint64_t)len * 8};
    return gf128_mul(h ^ lenblk, k);
}

std::string u128_hex(U128 v) {
    static const char *hx = "0123456789abcdef";
    std::string s(32, '0');
    for (int i = 0; i < 16; ++i) {
        uint64_t w = (i < 8) ? v.hi : v.lo;
        uint8_t byte = (uint8_t)(w >> (56 - 8 * (i % 8)));
        s[2 * i] = hx[byte >> 4];
        s[2 * i + 1] = hx[byte & 15];
    }
    return s;
}

bool u128_from_hex(const std::string &s, U128 &out) {
    if (s.size() != 32) return false;
    uint8_t b[16];
    for (int i = 0; i < 16; ++i) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int h = nib(s[2 * i]), l = nib(s[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        b[i] = (uint8_t)(h * 16 + l);
    }
    out = u128_from_bytes(b);
    return true;
}

bool ct_equal(U128 a, U128 b) {
    uint64_t d = (a.hi ^ b.hi) | (a.lo ^ b.lo);
    return ((d | (0 - d)) >> 63) == 0;
}

// ---------------------------------------------------------------- reserva ---
bool KeyPool::take(size_t n, Bits &out) {
    if (available() < n) return false;
    out.assign(bits_.begin() + (std::ptrdiff_t)pos_, bits_.begin() + (std::ptrdiff_t)(pos_ + n));
    // Los bits consumidos se borran de memoria (no deben poder reutilizarse).
    for (size_t i = pos_; i < pos_ + n; ++i) bits_[i] = 0;
    pos_ += n;
    if (pos_ > 1u << 16 && pos_ * 2 > bits_.size()) {   // compactacion
        bits_.erase(bits_.begin(), bits_.begin() + (std::ptrdiff_t)pos_);
        base_ += pos_;
        pos_ = 0;
    }
    return true;
}

bool KeyPool::take_u128(U128 &out) {
    Bits b;
    if (!take(128, b)) return false;
    out = U128{};
    for (int i = 0; i < 64; ++i) out.hi |= (uint64_t)b[i] << (63 - i);
    for (int i = 0; i < 64; ++i) out.lo |= (uint64_t)b[64 + i] << (63 - i);
    return true;
}

void KeyPool::add(const Bits &bits) { bits_.insert(bits_.end(), bits.begin(), bits.end()); }

// --------------------------------------------------------- autenticador -----
Authenticator::Authenticator(KeyPool &pool) : pool_(pool) {
    // k = 0 haria el hash constante: se descarta (probabilidad 2^-128).
    do {
        if (!pool_.take_u128(k_)) return;
        consumed_ += 128;
    } while (k_.is_zero());
    ready_ = true;
}

U128 Authenticator::mac(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg) const {
    std::vector<uint8_t> m;
    m.reserve(msg.size() + 9);
    m.push_back((uint8_t)dir);   // impide reflejar un mensaje al emisor
    for (int i = 7; i >= 0; --i) m.push_back((uint8_t)(seq >> (8 * i)));   // impide reordenar/repetir
    m.insert(m.end(), msg.begin(), msg.end());
    return poly_hash(k_, m.data(), m.size());
}

bool Authenticator::tag(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg, AuthTag &out) {
    if (!ready_) return false;
    out.key_offset = pool_.offset();
    U128 r;
    if (!pool_.take_u128(r)) return false;
    consumed_ += 128;
    out.value = mac(dir, seq, msg) ^ r;
    return true;
}

bool Authenticator::verify(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg, const AuthTag &t) {
    if (!ready_) return false;
    const bool offset_ok = t.key_offset == pool_.offset();
    U128 r;
    if (!pool_.take_u128(r)) return false;
    consumed_ += 128;
    const bool tag_ok = ct_equal(mac(dir, seq, msg) ^ r, t.value);
    return offset_ok && tag_ok;
}

// ------------------------------------------- post-procesado autenticado -----
namespace {
uint64_t dbl_bits(double d) {
    uint64_t u;
    static_assert(sizeof u == sizeof d, "");
    std::memcpy(&u, &d, sizeof u);
    return u;
}
} // namespace

AuthenticatedResult run_authenticated_postprocessing(const Bits &alice_sifted, const Bits &bob_sifted,
                                                     KeyPool &alice_pool, KeyPool &bob_pool,
                                                     const AuthenticatedConfig &cfg, RandomSource &alice_rng,
                                                     ChannelAttacker *attacker) {
    AuthenticatedResult res;
    ChannelAttacker passive;
    ChannelAttacker &eve = attacker ? *attacker : passive;
    res.sifted = (uint32_t)alice_sifted.size();

    Authenticator auth_a(alice_pool), auth_b(bob_pool);
    if (!auth_a.ready() || !auth_b.ready()) {
        res.abort_reason = "reserva de clave insuficiente para la clave del hash";
        return res;
    }
    uint64_t seq_a = 0, seq_b = 0;

    // Intercambio de etiquetas al final de una fase: Alice firma lo que envio y
    // Bob lo comprueba con lo que recibio; despues al reves.
    auto close_phase = [&](int phase, const Transcript &alice_view, const Transcript &bob_view) -> bool {
        AuthTag t;
        if (!auth_a.tag(ALICE_TO_BOB, seq_a, alice_view.bytes, t)) { res.abort_reason = "sin clave (Alice)"; return false; }
        eve.tag(phase, t);
        if (!auth_b.verify(ALICE_TO_BOB, seq_a++, bob_view.bytes, t)) {
            res.abort_reason = "autenticacion fallida (Bob rechaza a Alice) en la fase " + std::to_string(phase);
            return false;
        }
        if (!auth_b.tag(BOB_TO_ALICE, seq_b, bob_view.bytes, t)) { res.abort_reason = "sin clave (Bob)"; return false; }
        eve.tag(phase, t);
        if (!auth_a.verify(BOB_TO_ALICE, seq_b++, alice_view.bytes, t)) {
            res.abort_reason = "autenticacion fallida (Alice rechaza a Bob) en la fase " + std::to_string(phase);
            return false;
        }
        return true;
    };

    // Envio de un mensaje de Alice a Bob por el canal (el atacante puede tocarlo).
    auto send = [&](int phase, const std::vector<uint8_t> &msg, Transcript &alice_view, Transcript &bob_view) {
        std::vector<uint8_t> wire = msg;
        eve.message(phase, wire);
        alice_view.raw(msg);
        bob_view.raw(wire);
        return wire;
    };
    auto rd64 = [](const std::vector<uint8_t> &m, size_t off) {
        uint64_t v = 0;
        for (size_t i = 0; i < 8 && off + i < m.size(); ++i) v = (v << 8) | m[off + i];
        return v;
    };

    // Semillas publicas: SIEMPRE de la fuente aleatoria de Alice, nuevas en cada
    // ronda y extraidas justo antes de usarlas (la de verificacion despues de
    // Cascade y la de Toeplitz despues de la verificacion).
    auto draw = [&](size_t n, Bits &out) -> bool {
        if (alice_rng.random_bits(n, out) && out.size() == n) {
            res.random_bits_used += n;
            return true;
        }
        res.abort_reason = std::string("fuente aleatoria no disponible: ") + alice_rng.name();
        return false;
    };
    auto draw_u64 = [&](uint64_t &v) -> bool {
        Bits b;
        if (!draw(64, b)) return false;
        v = 0;
        for (int i = 0; i < 64; ++i) v |= (uint64_t)b[(size_t)i] << i;
        return true;
    };

    // ---- Fase 0: parametros ------------------------------------------------
    uint64_t cascade_seed = 0;
    if (!draw_u64(cascade_seed)) return res;
    {
        Transcript ta, tb, msg;
        msg.u64(alice_sifted.size());
        msg.u64((uint64_t)cfg.lag_ps);
        msg.u32(cfg.half_window);
        msg.u64(cascade_seed);
        msg.u32((uint32_t)cfg.cascade.passes);
        msg.u64(dbl_bits(cfg.cascade.qber_hint));
        send(PHASE_PARAMS, msg.bytes, ta, tb);
        if (!close_phase(PHASE_PARAMS, ta, tb)) return res;
    }

    // ---- Fase 1: Cascade ----------------------------------------------------
    Bits kb = bob_sifted;
    {
        Transcript ta, tb;
        CascadeConfig cc = cfg.cascade;
        cc.seed = cascade_seed;   // permutaciones: pseudoaleatorias a partir de 64 bits (suficiente: no afectan a la seguridad)
        cc.channel = [&](uint64_t idx, uint8_t p) { return eve.parity(idx, p); };
        cc.on_parity = [&](uint32_t pass, uint32_t lo, uint32_t hi, uint8_t sent, uint8_t recv) {
            // Peticion de Bob (pase, rango) + respuesta de Alice.
            for (Transcript *t : {&ta, &tb}) { t->u32(pass); t->u32(lo); t->u32(hi); }
            ta.u8(sent);
            tb.u8(recv);
        };
        CascadeResult cr = cascade_correct(alice_sifted, kb, cc);
        res.corrected = cr.corrected;
        res.leaked_ec = cr.leaked_bits;
        // Primero se autentica la transcripcion: si un atacante altero las
        // paridades, este es el motivo real del fallo.
        if (!close_phase(PHASE_CASCADE, ta, tb)) return res;
        if (!cr.converged) { res.abort_reason = "Cascade no converge"; return res; }
    }

    // ---- Fase 2: verificacion ----------------------------------------------
    {
        uint64_t verify_seed = 0;
        if (!draw_u64(verify_seed)) return res;
        Transcript ta, tb, msg;
        msg.u64(verify_seed);
        msg.u64(verification_hash(alice_sifted, verify_seed));
        std::vector<uint8_t> wire = send(PHASE_VERIFY, msg.bytes, ta, tb);
        if (!close_phase(PHASE_VERIFY, ta, tb)) return res;
        uint64_t seed_rx = rd64(wire, 0), hash_rx = rd64(wire, 8);
        if (verification_hash(kb, seed_rx) != hash_rx) {
            res.abort_reason = "verificacion fallida: quedan errores tras Cascade";
            return res;
        }
    }

    // ---- Fase 3: amplificacion de privacidad --------------------------------
    const double qber = res.sifted ? (double)res.corrected / res.sifted : 0.0;
    // La autenticacion no filtra informacion de la clave (etiquetas cifradas con OTP).
    uint32_t m = pa_output_length(res.sifted, qber, res.leaked_ec, 64);
    if (m == 0) { res.abort_reason = "clave demasiado corta para la amplificacion de privacidad"; return res; }
    Bits fa, fb;
    {
        // Semilla Toeplitz COMPLETA (n + m - 1 bits por bloque), uniforme.
        Bits seed;
        if (!draw(toeplitz_seed_length(alice_sifted.size(), m), seed)) return res;
        Transcript ta, tb, msg;
        msg.u32(m);
        msg.u64(seed.size());
        std::vector<uint8_t> sb((seed.size() + 7) / 8, 0);
        for (size_t i = 0; i < seed.size(); ++i) sb[i >> 3] |= (uint8_t)(seed[i] << (i & 7));
        msg.bytes.insert(msg.bytes.end(), sb.begin(), sb.end());
        std::vector<uint8_t> wire = send(PHASE_PA, msg.bytes, ta, tb);
        if (!close_phase(PHASE_PA, ta, tb)) return res;
        // Bob usa su vista del mensaje (si el atacante lo altero, la
        // autenticacion de esta fase ya habria fallado).
        uint32_t m_rx = 0;
        for (size_t i = 0; i < 4 && i < wire.size(); ++i) m_rx = (m_rx << 8) | wire[i];
        const uint64_t nseed_rx = rd64(wire, 4);
        Bits seed_rx;
        for (uint64_t i = 0; i < nseed_rx && 12 + (i >> 3) < wire.size(); ++i)
            seed_rx.push_back((wire[12 + (i >> 3)] >> (i & 7)) & 1);
        fa = toeplitz_hash_seeded(alice_sifted, m, seed);
        fb = toeplitz_hash_seeded(kb, m_rx, seed_rx);
        if (fa.size() != m || fb.size() != m) { res.abort_reason = "semilla de Toeplitz invalida"; return res; }
    }

    res.pa_length = m;
    res.auth_key_bits = auth_a.key_bits_consumed();
    res.net_key_bits = (int64_t)m - (int64_t)res.auth_key_bits;
    res.ok = true;

    // Reposicion de la reserva con la clave nueva; el resto se entrega.
    size_t refill = cfg.refill_pools ? std::min<size_t>(fa.size(), res.auth_key_bits) : 0;
    if (refill) {
        alice_pool.add(Bits(fa.begin(), fa.begin() + (std::ptrdiff_t)refill));
        bob_pool.add(Bits(fb.begin(), fb.begin() + (std::ptrdiff_t)refill));
    }
    res.alice_key.assign(fa.begin() + (std::ptrdiff_t)refill, fa.end());
    res.bob_key.assign(fb.begin() + (std::ptrdiff_t)refill, fb.end());
    return res;
}

} // namespace aurora
