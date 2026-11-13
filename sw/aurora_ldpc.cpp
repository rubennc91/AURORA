// =============================================================================
// AURORA - Reconciliacion LDPC: implementacion
// =============================================================================
#include "aurora_ldpc.h"
#include "aurora_compat.h"
#include "../hw/aurora_kernels.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace aurora {

// ------------------------------------------------------------ construccion --
const std::vector<double> &ldpc_family_rates() {
    static const std::vector<double> r = {0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95};
    return r;
}

LdpcDegrees ldpc_default_degrees(double rate) {
    // Ajustado por simulacion (n = 16384, BSC, sum-product, FER <= ~15 %):
    // f minimo ~1.20 en R = 0.85 y ~1.15 en R = 0.75 (casi regular: ~1.7).
    LdpcDegrees d;
    d.d_high = rate >= 0.9 ? 10 : 12;
    d.frac_high = rate >= 0.9 ? 0.30 : 0.25;
    return d;
}

LdpcCode ldpc_build(uint32_t n, uint32_t m, uint64_t seed, const LdpcDegrees &deg) {
    LdpcCode c;
    c.n = n;
    c.m = m;
    if (m < 2 || m >= n) return c;
    SplitMix64 rng(seed);

    // Adyacencia variable -> comprobaciones y comprobacion -> comprobaciones
    // vecinas (bitset m x m) para evitar ciclos de longitud 4.
    std::vector<std::vector<uint32_t>> vchk(n);
    std::vector<uint32_t> cdeg(m, 0);
    std::vector<uint64_t> nb((size_t)m * ((m + 63) / 64), 0);
    const size_t row = (m + 63) / 64;
    auto is_nb = [&](uint32_t a, uint32_t b) { return (nb[(size_t)a * row + b / 64] >> (b % 64)) & 1u; };
    auto set_nb = [&](uint32_t a, uint32_t b) {
        nb[(size_t)a * row + b / 64] |= 1ull << (b % 64);
        nb[(size_t)b * row + a / 64] |= 1ull << (a % 64);
    };
    auto connect = [&](uint32_t v, uint32_t ch) {
        for (uint32_t o : vchk[v]) set_nb(o, ch);
        vchk[v].push_back(ch);
        ++cdeg[ch];
    };

    // Nodos de grado alto y de grado 3: al principio (los de grado alto antes,
    // como en PEG). Los m-1 nodos de grado 2 forman la escalera al final.
    const uint32_t n2 = m - 1;
    const uint32_t n_rest = n - n2;
    uint32_t n_high = (uint32_t)compat::llround(deg.frac_high * n);
    if (n_high > n_rest) n_high = n_rest;

    // Escalera: variable n_rest + i conecta las comprobaciones i e i+1.
    for (uint32_t i = 0; i < n2; ++i) {
        connect(n_rest + i, i);
        connect(n_rest + i, i + 1);
    }

    for (uint32_t v = 0; v < n_rest; ++v) {
        const uint32_t d = std::min<uint32_t>(v < n_high ? deg.d_high : 3, m);
        for (uint32_t j = 0; j < d; ++j) {
            // Entre comprobaciones al azar, la de menor grado que no cree un
            // ciclo de 4 ni repita conexion (400 intentos); si ninguna vale, se
            // relaja la condicion del ciclo (codigos muy pequenos).
            int64_t best = -1;
            for (int relax = 0; relax < 2 && best < 0; ++relax) {
                uint32_t best_deg = UINT32_MAX;
                for (int t = 0; t < (relax ? 48 : 400); ++t) {
                    const uint32_t ch = (uint32_t)rng.bounded(m);
                    bool okc = std::find(vchk[v].begin(), vchk[v].end(), ch) == vchk[v].end();
                    if (okc && relax == 0)
                        for (uint32_t o : vchk[v])
                            if (is_nb(o, ch)) { okc = false; break; }
                    if (okc && cdeg[ch] < best_deg) { best_deg = cdeg[ch]; best = ch; }
                    if (t >= 47 && best >= 0) break;   // 48 muestras bastan casi siempre
                }
            }
            if (best >= 0) connect(v, (uint32_t)best);
        }
    }

    // CSR por comprobaciones.
    c.chk_ptr.assign(m + 1, 0);
    for (uint32_t v = 0; v < n; ++v)
        for (uint32_t ch : vchk[v]) ++c.chk_ptr[ch + 1];
    for (uint32_t i = 0; i < m; ++i) c.chk_ptr[i + 1] += c.chk_ptr[i];
    c.chk_var.assign(c.chk_ptr[m], 0);
    std::vector<uint32_t> fill(c.chk_ptr.begin(), c.chk_ptr.end() - 1);
    for (uint32_t v = 0; v < n; ++v)
        for (uint32_t ch : vchk[v]) c.chk_var[fill[ch]++] = v;
    c.chk_var16.assign(c.chk_var.begin(), c.chk_var.end());
    static uint64_t next_id = 0;
    c.id = ++next_id;
    c.var_ptr.assign(n + 1, 0);
    for (uint32_t v = 0; v < n; ++v) c.var_ptr[v + 1] = c.var_ptr[v] + (uint32_t)vchk[v].size();
    c.var_chk.reserve(c.var_ptr[n]);
    for (uint32_t v = 0; v < n; ++v) c.var_chk.insert(c.var_chk.end(), vchk[v].begin(), vchk[v].end());
    return c;
}

std::vector<uint32_t> ldpc_untainted_puncturing(const LdpcCode &c, const std::vector<uint32_t> &order, uint32_t max_p) {
    std::vector<uint32_t> out;
    std::vector<uint8_t> tainted(c.m, 0);
    for (uint32_t v : order) {
        if (out.size() >= max_p) break;
        bool ok = true;
        for (uint32_t e = c.var_ptr[v]; e < c.var_ptr[v + 1] && ok; ++e) ok = !tainted[c.var_chk[e]];
        if (!ok) continue;
        out.push_back(v);
        // Se marcan las comprobaciones vecinas Y las de sus otras variables: asi
        // ninguna comprobacion queda con dos perforadas a distancia 2.
        for (uint32_t e = c.var_ptr[v]; e < c.var_ptr[v + 1]; ++e) tainted[c.var_chk[e]] = 1;
    }
    return out;
}

std::shared_ptr<const LdpcCode> ldpc_code(double rate, uint32_t n) {
    static std::map<std::pair<uint32_t, uint32_t>, std::shared_ptr<const LdpcCode>> cache;
    const uint32_t m = (uint32_t)compat::llround((1.0 - rate) * n);
    auto key = std::make_pair(n, m);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const uint64_t seed = 0x4C4450434155524Full ^ ((uint64_t)n << 32) ^ m;   // "LDPCAURO"
    auto c = std::make_shared<const LdpcCode>(ldpc_build(n, m, seed, ldpc_default_degrees(rate)));
    cache[key] = c;
    return c;
}

Bits ldpc_syndrome(const LdpcCode &c, const Bits &x) {
    Bits s(c.m, 0);
    for (uint32_t i = 0; i < c.m; ++i) {
        uint8_t p = 0;
        for (uint32_t e = c.chk_ptr[i]; e < c.chk_ptr[i + 1]; ++e) p ^= x[c.chk_var[e]];
        s[i] = p;
    }
    return s;
}

bool ldpc_girth_at_least_6(const LdpcCode &c) {
    // Dos comprobaciones no pueden compartir mas de una variable.
    std::map<std::pair<uint32_t, uint32_t>, int> seen;
    std::vector<std::vector<uint32_t>> vchk(c.n);
    for (uint32_t i = 0; i < c.m; ++i)
        for (uint32_t e = c.chk_ptr[i]; e < c.chk_ptr[i + 1]; ++e) vchk[c.chk_var[e]].push_back(i);
    for (auto &l : vchk)
        for (size_t a = 0; a < l.size(); ++a)
            for (size_t b = a + 1; b < l.size(); ++b)
                if (++seen[std::make_pair(std::min(l[a], l[b]), std::max(l[a], l[b]))] > 1) return false;
    return true;
}

// ----------------------------------------------------------- decodificador --
static inline float phi(float x) {
    // phi(x) = -log(tanh(x/2)) = log((e^x + 1) / (e^x - 1)); involutiva.
    if (x < 1e-6f) x = 1e-6f;
    if (x > 30.0f) return 0.0f;
    const double e = ::exp((double)x);
    return (float)::log((e + 1.0) / (e - 1.0));
}

// Coma fija: cuantifica las LLR y llama al decodificador del kernel. Cada
// decodificador (HW o modelo SW) recuerda el ultimo codigo cargado.
static LdpcDecodeResult decode_fixed(const LdpcCode &c, const Bits &syndrome, const std::vector<float> &llr, Bits &x,
                                     uint32_t max_iter, LdpcFixedDecoder dec) {
    LdpcDecodeResult r;
    x.assign(c.n, 0);
    if (!dec) dec = aurora_ldpc_dec;
    if (c.n > AURORA_LDPC_MAX_N || c.m > AURORA_LDPC_MAX_M || c.edges() > AURORA_LDPC_MAX_E) return r;
    static std::map<LdpcFixedDecoder, uint64_t> loaded;
    const uint32_t load = loaded[dec] != c.id ? 1u : 0u;
    std::vector<int8_t> q(c.n);
    for (uint32_t v = 0; v < c.n; ++v) {
        const float s = llr[v] * AURORA_LDPC_LLR_SCALE;
        const float rnd = s >= 0 ? (float)::floor(s + 0.5f) : -(float)::floor(-s + 0.5f);
        q[v] = (int8_t)(rnd > 127 ? 127 : (rnd < -127 ? -127 : rnd));
    }
    uint32_t status[2] = {0, 0};
    if (max_iter > AURORA_LDPC_MAX_ITER) max_iter = AURORA_LDPC_MAX_ITER;
    dec(c.chk_ptr.data(), c.chk_var16.data(), c.n, c.m, load, q.data(), syndrome.data(), max_iter, x.data(), status);
    loaded[dec] = c.id;
    r.ok = status[0] != 0;
    r.iterations = status[1];
    return r;
}

LdpcDecodeResult ldpc_decode(const LdpcCode &c, const Bits &syndrome, const std::vector<float> &llr, Bits &x,
                             uint32_t max_iter, LdpcAlgo algo, float alpha, LdpcFixedDecoder fixed) {
    if (algo == LdpcAlgo::FIXED_MIN_SUM) return decode_fixed(c, syndrome, llr, x, max_iter, fixed);
    LdpcDecodeResult r;
    std::vector<float> L(llr.begin(), llr.end());
    std::vector<float> R(c.edges(), 0.0f), t;
    x.assign(c.n, 0);
    auto hard_ok = [&]() {
        for (uint32_t v = 0; v < c.n; ++v) x[v] = L[v] < 0.0f;
        for (uint32_t i = 0; i < c.m; ++i) {
            uint8_t p = 0;
            for (uint32_t e = c.chk_ptr[i]; e < c.chk_ptr[i + 1]; ++e) p ^= x[c.chk_var[e]];
            if (p != syndrome[i]) return false;
        }
        return true;
    };
    if (hard_ok()) { r.ok = true; return r; }
    for (uint32_t it = 1; it <= max_iter; ++it) {
        for (uint32_t i = 0; i < c.m; ++i) {
            const uint32_t b = c.chk_ptr[i], e_end = c.chk_ptr[i + 1], d = e_end - b;
            t.resize(d);
            bool neg = syndrome[i] != 0;
            for (uint32_t k = 0; k < d; ++k) {
                t[k] = L[c.chk_var[b + k]] - R[b + k];
                if (t[k] < 0.0f) neg = !neg;
            }
            if (algo == LdpcAlgo::SUM_PRODUCT) {
                float S = 0.0f;
                for (uint32_t k = 0; k < d; ++k) S += phi(std::fabs(t[k]));
                for (uint32_t k = 0; k < d; ++k) {
                    float mag = phi(std::max(S - phi(std::fabs(t[k])), 0.0f));
                    bool sg = neg != (t[k] < 0.0f);
                    R[b + k] = sg ? -mag : mag;
                    L[c.chk_var[b + k]] = t[k] + R[b + k];
                }
            } else {
                float m1 = 1e30f, m2 = 1e30f;
                uint32_t i1 = 0;
                for (uint32_t k = 0; k < d; ++k) {
                    const float a = std::fabs(t[k]);
                    if (a < m1) { m2 = m1; m1 = a; i1 = k; }
                    else if (a < m2) m2 = a;
                }
                for (uint32_t k = 0; k < d; ++k) {
                    float mag = alpha * (k == i1 ? m2 : m1);
                    bool sg = neg != (t[k] < 0.0f);
                    R[b + k] = sg ? -mag : mag;
                    L[c.chk_var[b + k]] = t[k] + R[b + k];
                }
            }
        }
        r.iterations = it;
        if (hard_ok()) { r.ok = true; return r; }
    }
    return r;
}

// ----------------------------------------------------------- planificacion --
uint32_t LdpcFramePlan::leak() const {
    const uint32_t m = (uint32_t)compat::llround((1.0 - rate) * (k + p + s));
    return m > p ? m - p : 0;
}

LdpcFramePlan ldpc_plan_frame(uint32_t k_avail, double q, double f, uint32_t n) {
    LdpcFramePlan best;
    const double h = binary_entropy(std::min(std::max(q, 1e-4), 0.5));
    const double fh = f * h;
    if (k_avail == 0 || fh >= 1.0) return best;
    const uint32_t p_max = n / 10;   // perforado maximo (mas degrada mucho el codigo)
    uint32_t best_cost = UINT32_MAX;
    for (double R : ldpc_family_rates()) {
        const uint32_t m = (uint32_t)compat::llround((1.0 - R) * n);
        LdpcFramePlan pl;
        pl.rate = R;
        // Trama completa (s = 0): m - p = f (n - p) h
        double p_full = ((double)m - fh * n) / (1.0 - fh);
        if (p_full >= 0 && (uint32_t)std::ceil(p_full) <= p_max && n - (uint32_t)std::ceil(p_full) <= k_avail) {
            pl.p = (uint32_t)std::ceil(p_full);
            pl.k = n - pl.p;
            pl.s = 0;
        } else {
            // Trama con k fijo: k = min(k_avail, maximo que admite el codigo)
            uint32_t k = std::min<uint32_t>(k_avail, (uint32_t)std::floor(m / fh));
            if (k > n) k = n;
            const double L = fh * k;
            if (L > m) continue;
            uint32_t p = (uint32_t)std::floor(m - L);
            if (p > p_max) continue;
            if (k + p > n) {
                if (n < p) continue;
                k = n - p;
            }
            pl.k = k;
            pl.p = p;
            pl.s = n - k - p;
        }
        if (pl.k == 0) continue;
        // Preferencia: el perforado degrada el codigo y el acortado no, asi que
        // se busca el menor p; a igualdad, mas bits de clave por trama.
        const uint32_t cost = pl.p * 4 + (n - pl.k);
        if (best.k == 0 || cost < best_cost) {
            best = pl;
            best_cost = cost;
        }
    }
    return best;
}

// --------------------------------------------------------- reconciliacion ---
namespace {
void put32(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 3; i >= 0; --i) b.push_back((uint8_t)(v >> (8 * i)));
}
uint32_t get32(const std::vector<uint8_t> &b, size_t off) {
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) v = (v << 8) | (off + i < b.size() ? b[off + i] : 0);
    return v;
}
} // namespace

LdpcReconcileResult ldpc_reconcile(const Bits &alice, const Bits &bob, const LdpcConfig &cfg, uint64_t seed,
                                   const std::function<bool(size_t, Bits &)> &alice_random, LdpcIo *io) {
    LdpcReconcileResult res;
    const uint32_t n = cfg.n;
    SplitMix64 pub(seed);
    // QBER adaptativo: pseudo-cuentas con la estimacion inicial (peso 4096 bits).
    double err_acc = cfg.qber_hint * 4096.0, bits_acc = 4096.0;
    size_t off = 0;
    while (off < alice.size()) {
        const uint32_t avail = (uint32_t)std::min<size_t>(alice.size() - off, n);
        const double q_est = err_acc / bits_acc;
        LdpcFramePlan pl = ldpc_plan_frame(avail, q_est, cfg.f_target, n);
        if (pl.k == 0) {   // resto sin plan posible: se descarta
            res.discarded_bits += avail;
            break;
        }
        // Posiciones: permutacion publica. Perforados: los primeros p que admite
        // el perforado "untainted" en ese orden; acortados y clave: el resto en
        // orden. Si no hay p posiciones perforables, se pasan a acortadas.
        std::vector<uint32_t> perm = random_permutation(n, pub);
        auto layout = [&](const LdpcCode &c, const LdpcFramePlan &pl_in, LdpcFramePlan &pl_out) {
            pl_out = pl_in;
            std::vector<uint32_t> punct_pos = ldpc_untainted_puncturing(c, perm, pl_in.p);
            if (punct_pos.size() < pl_in.p) {
                pl_out.s += pl_in.p - (uint32_t)punct_pos.size();
                pl_out.p = (uint32_t)punct_pos.size();
            }
            std::vector<uint8_t> is_p(n, 0);
            for (uint32_t v : punct_pos) is_p[v] = 1;
            std::vector<uint32_t> pos(punct_pos);
            for (uint32_t v : perm) if (!is_p[v]) pos.push_back(v);
            return pos;   // [0,p) perforados, [p,p+s) acortados, resto clave
        };
        auto code = ldpc_code(pl.rate, n);
        LdpcFramePlan plan_used;
        std::vector<uint32_t> pos = layout(*code, pl, plan_used);
        pl = plan_used;
        Bits xa(n, 0), punct;
        if (pl.p && (!alice_random(pl.p, punct) || punct.size() != pl.p)) {
            res.discarded_bits += (uint32_t)(alice.size() - off);
            break;
        }
        for (uint32_t i = 0; i < pl.p; ++i) xa[pos[i]] = punct[i];
        for (uint32_t i = 0; i < pl.k; ++i) xa[pos[pl.p + pl.s + i]] = alice[off + i];
        const Bits syn = ldpc_syndrome(*code, xa);

        // Mensaje de Alice: plan + sindrome.
        std::vector<uint8_t> msg;
        put32(msg, (uint32_t)compat::llround(pl.rate * 1000));
        put32(msg, pl.k);
        put32(msg, pl.p);
        put32(msg, pl.s);
        std::vector<uint8_t> sb((syn.size() + 7) / 8, 0);
        for (size_t i = 0; i < syn.size(); ++i) sb[i >> 3] |= (uint8_t)(syn[i] << (i & 7));
        msg.insert(msg.end(), sb.begin(), sb.end());
        const std::vector<uint8_t> wire = io && io->alice_to_bob ? io->alice_to_bob(msg) : msg;

        // Bob: usa SOLO lo recibido.
        LdpcFramePlan rx;
        rx.rate = get32(wire, 0) / 1000.0;
        rx.k = get32(wire, 4);
        rx.p = get32(wire, 8);
        rx.s = get32(wire, 12);
        bool bob_ok = false;
        uint32_t errors = 0;
        Bits xb;
        if (rx.k == pl.k && rx.k + rx.p + rx.s == n && off + rx.k <= bob.size()) {
            auto bcode = ldpc_code(rx.rate, n);
            LdpcFramePlan rx_used;
            std::vector<uint32_t> bpos = layout(*bcode, rx, rx_used);
            if (rx_used.p != rx.p) rx.k = 0;   // plan incoherente: Bob no decodifica
            Bits syn_rx(bcode->m, 0);
            for (uint32_t i = 0; i < bcode->m && 16 + (i >> 3) < wire.size(); ++i)
                syn_rx[i] = (wire[16 + (i >> 3)] >> (i & 7)) & 1;
            const double q = std::min(std::max(q_est, 1e-4), 0.25);
            const float l0 = (float)::log((1.0 - q) / q);
            std::vector<float> llr(n, 0.0f);
            for (uint32_t i = rx.p; i < rx.p + rx.s; ++i) llr[bpos[i]] = 30.0f;   // acortados = 0
            for (uint32_t i = 0; i < rx.k; ++i) llr[bpos[rx.p + rx.s + i]] = bob[off + i] ? -l0 : l0;
            LdpcDecodeResult d = ldpc_decode(*bcode, syn_rx, llr, xb, cfg.max_iter, cfg.algo, 0.8f, cfg.fixed_decoder);
            res.max_iter_seen = std::max<double>(res.max_iter_seen, d.iterations);
            bob_ok = d.ok && rx.k > 0;
            if (bob_ok)
                for (uint32_t i = 0; i < rx.k; ++i) errors += xb[bpos[rx.p + rx.s + i]] != bob[off + i];
        }
        std::vector<uint8_t> reply;
        reply.push_back(bob_ok ? 1 : 0);
        put32(reply, errors);
        const std::vector<uint8_t> rwire = io && io->bob_to_alice ? io->bob_to_alice(reply) : reply;
        const bool alice_ok = !rwire.empty() && rwire[0] == 1;
        const uint32_t errors_rx = get32(rwire, 1);

        ++res.frames;
        if (getenv("AURORA_LDPC_DEBUG"))
            ::printf("[ldpc] trama %u: R=%.2f k=%u p=%u s=%u q_est=%.4f ok=%d errores=%u\n", res.frames, pl.rate, pl.k,
                     pl.p, pl.s, q_est, (int)bob_ok, errors);
        if (alice_ok && bob_ok) {
            for (uint32_t i = 0; i < pl.k; ++i) {
                res.alice.push_back(alice[off + i]);
                res.bob.push_back(xb[pos[pl.p + pl.s + i]]);
            }
            res.corrected += errors;
            res.leaked_bits += pl.leak();
            err_acc += errors_rx;
            bits_acc += pl.k;
        } else {
            // Trama descartada por ambos (su sindrome solo habla de bits que no
            // se usan). Se sube la estimacion del QBER para la siguiente.
            ++res.failed;
            res.discarded_bits += pl.k;
            err_acc *= 1.6;
        }
        off += pl.k;
    }
    return res;
}

} // namespace aurora
