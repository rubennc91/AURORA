// =============================================================================
// AURORA - Software del PS: implementacion
// =============================================================================
#include "aurora_host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>

namespace aurora {

// ------------------------------------------------------------------ E/S -----
bool load_detections(const std::string &path, Detections &out, std::string *err) {
    out.tags.clear();
    out.states.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "no se puede abrir " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] != '(') { ++i; continue; }
        ++i;
        uint64_t t = 0;
        bool any = false;
        while (i < n && s[i] >= '0' && s[i] <= '9') { t = t * 10 + (uint64_t)(s[i] - '0'); ++i; any = true; }
        if (!any || i >= n || s[i] != ',') {
            if (err) *err = "formato invalido en " + path + " (offset " + std::to_string(i) + ")";
            return false;
        }
        ++i;
        uint64_t st = 0;
        any = false;
        while (i < n && s[i] >= '0' && s[i] <= '9') { st = st * 10 + (uint64_t)(s[i] - '0'); ++i; any = true; }
        if (!any || i >= n || s[i] != ')') {
            if (err) *err = "formato invalido en " + path + " (offset " + std::to_string(i) + ")";
            return false;
        }
        ++i;
        out.tags.push_back(t);
        out.states.push_back((uint8_t)st);
    }
    return true;
}

bool load_batch(const std::string &prefix, Detections &alice, Detections &bob, std::string *err) {
    return load_detections(prefix + "_alice.detections", alice, err) &&
           load_detections(prefix + "_bob.detections", bob, err);
}

bool validate_detections(const Detections &d, std::string *err) {
    if (d.tags.size() != d.states.size()) { if (err) *err = "tags/estados de distinto tamano"; return false; }
    for (size_t i = 0; i < d.size(); ++i) {
        if (d.states[i] > 3) { if (err) *err = "estado invalido en " + std::to_string(i); return false; }
        if (d.tags[i] >= (1ULL << 62)) { if (err) *err = "tag >= 2^62 en " + std::to_string(i); return false; }
        if (i && d.tags[i] < d.tags[i - 1]) { if (err) *err = "tags no ordenados en " + std::to_string(i); return false; }
    }
    return true;
}

// ----------------------------------------------------------------- bits -----
Bits unpack_bits(const uint64_t *words, uint32_t n_bits) {
    Bits b(n_bits);
    for (uint32_t k = 0; k < n_bits; ++k) b[k] = (words[k >> 6] >> (k & 63)) & 1;
    return b;
}

std::vector<uint64_t> pack_bits(const Bits &bits) {
    std::vector<uint64_t> w((bits.size() + 63) / 64 + 1, 0);
    for (size_t k = 0; k < bits.size(); ++k)
        if (bits[k]) w[k >> 6] |= 1ULL << (k & 63);
    return w;
}

std::string bits_to_hex(const Bits &bits) {
    static const char *hex = "0123456789abcdef";
    std::string s;
    for (size_t k = 0; k < bits.size(); k += 4) {
        unsigned v = 0;
        for (size_t j = 0; j < 4 && k + j < bits.size(); ++j) v |= (unsigned)bits[k + j] << j;
        s.push_back(hex[v]);
    }
    return s;
}

// ------------------------------------------------------- sincronizacion -----
// Selecciona prefijos de Alice y Bob que cubren el MISMO intervalo temporal y
// caben en el kernel (el fallo principal de la version anterior era truncar
// ambos a 4096 eventos, que corresponden a intervalos distintos).
static void aligned_prefix(const Detections &a, const Detections &b, uint32_t &na, uint32_t &nb) {
    na = (uint32_t)std::min<size_t>(a.size(), AURORA_MAX_TAGS);
    nb = (uint32_t)std::min<size_t>(b.size(), AURORA_MAX_TAGS);
    if (na == 0 || nb == 0) return;
    uint64_t t_end = std::min(a.tags[na - 1], b.tags[nb - 1]);
    na = (uint32_t)(std::upper_bound(a.tags.begin(), a.tags.begin() + na, t_end) - a.tags.begin());
    nb = (uint32_t)(std::upper_bound(b.tags.begin(), b.tags.begin() + nb, t_end) - b.tags.begin());
}

static void centroid(const std::vector<uint32_t> &h, int64_t lag_min, uint32_t shift, const SyncConfig &cfg,
                     double center, LagEstimate &out) {
    const double w = (double)(1u << shift);
    double bg_sum = 0;
    int bg_n = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        double c = lag_min + (i + 0.5) * w;
        if (std::fabs(c - center) > cfg.background_guard_ps) { bg_sum += h[i]; ++bg_n; }
    }
    double bg = bg_n ? bg_sum / bg_n : 0.0;   // cuentas de fondo por bin
    double s0 = 0, s1 = 0, s2 = 0, raw = 0;
    int sig_n = 0;
    for (size_t i = 0; i < h.size(); ++i) {
        double c = lag_min + (i + 0.5) * w;
        if (std::fabs(c - center) <= cfg.signal_half_width_ps) {
            double v = (double)h[i] - bg;
            s0 += v; s1 += v * c; s2 += v * c * c; raw += h[i]; ++sig_n;
        }
    }
    out.signal = s0;
    out.background_per_ps = bg / w;
    out.snr = (raw > 0) ? s0 / std::sqrt(raw) : 0.0;
    if (s0 > 0) {
        out.lag = s1 / s0;
        double var = s2 / s0 - out.lag * out.lag;
        out.sigma = var > 0 ? std::sqrt(var) : 0.0;
    }
    (void)sig_n;
}

LagEstimate estimate_lag(const Detections &alice, const Detections &bob, const SyncConfig &cfg) {
    LagEstimate est;
    uint32_t na, nb;
    aligned_prefix(alice, bob, na, nb);
    est.n_alice_used = na;
    est.n_bob_used = nb;
    if (na == 0 || nb == 0) return est;

    std::vector<uint32_t> hist(AURORA_SYNC_MAX_BINS);
    SyncResult r{};

    // Paso 1: grueso.
    aurora_sync(alice.tags.data(), na, bob.tags.data(), nb, cfg.coarse_lag_min, cfg.coarse_shift,
                cfg.coarse_bins, hist.data(), &r);
    if (r.peak_count == 0) return est;
    const int64_t cw = (int64_t)1 << cfg.coarse_shift;
    est.coarse_lag = cfg.coarse_lag_min + (int64_t)r.peak_bin * cw + cw / 2;

    // Paso 2: fino, centrado en el grueso.
    const int64_t fw = (int64_t)1 << cfg.fine_shift;
    const int64_t fine_min = est.coarse_lag - (int64_t)cfg.fine_bins / 2 * fw;
    aurora_sync(alice.tags.data(), na, bob.tags.data(), nb, fine_min, cfg.fine_shift, cfg.fine_bins,
                hist.data(), &r);
    hist.resize(cfg.fine_bins);

    double center = fine_min + (r.peak_bin + 0.5) * fw;
    for (int iter = 0; iter < 3; ++iter) {
        centroid(hist, fine_min, cfg.fine_shift, cfg, center, est);
        center = est.lag;
    }
    const double range_lo = (double)cfg.coarse_lag_min;
    const double range_hi = (double)cfg.coarse_lag_min + (double)cfg.coarse_bins * cw;
    est.ok = r.complete && est.signal > 0 && est.snr >= cfg.min_snr && est.lag > range_lo + cw && est.lag < range_hi - cw;
    return est;
}

// ---------------------------------------------------------------- sifting ---
SiftOutput sift_batch(const Detections &alice, const Detections &bob, int64_t lag, uint32_t half_window,
                      uint32_t max_chunk) {
    SiftOutput out;
    if (max_chunk == 0 || max_chunk > AURORA_MAX_TAGS) max_chunk = AURORA_MAX_TAGS;
    const size_t NA = alice.size(), NB = bob.size();
    std::vector<uint64_t> ka(AURORA_MAX_KEY_WORDS + 1), kb(AURORA_MAX_KEY_WORDS + 1);

    size_t a0 = 0, b0 = 0;
    while (b0 < NB) {
        size_t b1 = std::min(NB, b0 + max_chunk);
        size_t a1 = std::min(NA, a0 + max_chunk);
        if (a1 < NA) {
            // Solo procesamos Bobs cuya ventana completa cae antes de alice[a1]:
            //   t_b + lag + w < t_a[a1]
            const int64_t lim = (int64_t)alice.tags[a1] - lag - (int64_t)half_window;
            size_t bl = std::lower_bound(bob.tags.begin() + b0, bob.tags.begin() + b1, lim,
                                         [](uint64_t t, int64_t v) { return (int64_t)t < v; }) - bob.tags.begin();
            if (bl == b0) {
                // Ningun Bob cabe: descartamos los Alice anteriores a la ventana
                // de b0 (el kernel tambien los descartaria) y reintentamos.
                const int64_t lo_t = (int64_t)bob.tags[b0] + lag - (int64_t)half_window;
                size_t an = std::lower_bound(alice.tags.begin() + a0, alice.tags.end(), lo_t,
                                             [](uint64_t t, int64_t v) { return (int64_t)t < v; }) - alice.tags.begin();
                if (an > a0) { a0 = an; continue; }
                // Caso degenerado: mas de max_chunk tags de Alice dentro de una
                // sola ventana. Procesamos Bob de uno en uno.
                bl = b0 + 1;
            }
            b1 = bl;
        }
        SiftResult r{};
        aurora_sift(alice.tags.data() + a0, alice.states.data() + a0, (uint32_t)(a1 - a0),
                    bob.tags.data() + b0, bob.states.data() + b0, (uint32_t)(b1 - b0),
                    lag, half_window, ka.data(), kb.data(), &r);
        Bits pa = unpack_bits(ka.data(), r.n_sifted), pb = unpack_bits(kb.data(), r.n_sifted);
        out.alice_key.insert(out.alice_key.end(), pa.begin(), pa.end());
        out.bob_key.insert(out.bob_key.end(), pb.begin(), pb.end());
        out.n_coincidences += r.n_coincidences;
        out.n_chunks++;
        a0 += r.next_alice_idx;
        b0 = b1;
    }
    return out;
}

// ---------------------------------------------------------------- Cascade ---
uint64_t SplitMix64::bounded(uint64_t n) {
    if (n <= 1) return 0;
    const uint64_t limit = UINT64_MAX - (UINT64_MAX % n);
    uint64_t x;
    do { x = next(); } while (x >= limit);
    return x % n;
}

std::vector<uint32_t> random_permutation(uint32_t n, SplitMix64 &rng) {
    std::vector<uint32_t> p(n);
    for (uint32_t i = 0; i < n; ++i) p[i] = i;
    for (uint32_t i = n; i > 1; --i) std::swap(p[i - 1], p[rng.bounded(i)]);
    return p;
}

namespace {
struct Pass {
    uint32_t k;
    std::vector<uint32_t> perm;      // posicion -> indice
    std::vector<uint32_t> inv;       // indice -> posicion
    std::vector<uint8_t> alice_par;  // paridad de Alice por bloque (revelada)
};

uint8_t parity_range(const Bits &key, const Pass &p, uint32_t lo, uint32_t hi) {
    uint8_t x = 0;
    for (uint32_t i = lo; i < hi; ++i) x ^= key[p.perm[i]];
    return x;
}
} // namespace

CascadeResult cascade_correct(const Bits &alice, Bits &bob, const CascadeConfig &cfg) {
    CascadeResult res;
    const uint32_t n = (uint32_t)alice.size();
    if (n == 0 || bob.size() != alice.size()) return res;

    double q = std::max(cfg.qber_hint, 1e-4);
    uint32_t k1 = (uint32_t)std::max(4.0, std::round(0.73 / q));
    k1 = std::min(k1, n);
    res.first_block = k1;

    SplitMix64 rng(cfg.seed);
    std::vector<Pass> passes;

    auto block_end = [&](const Pass &p, uint32_t blk) { return std::min(n, (blk + 1) * p.k); };

    // Busqueda binaria en el bloque 'blk' del pase 'pi'; corrige un bit y
    // devuelve su indice original.
    auto binary_fix = [&](uint32_t pi, uint32_t blk) -> uint32_t {
        const Pass &p = passes[pi];
        uint32_t lo = blk * p.k, hi = block_end(p, blk);
        while (hi - lo > 1) {
            uint32_t mid = lo + (hi - lo) / 2;
            uint8_t pa = parity_range(alice, p, lo, mid);
            res.leaked_bits++;
            if (pa != parity_range(bob, p, lo, mid)) hi = mid; else lo = mid;
        }
        uint32_t idx = p.perm[lo];
        bob[idx] ^= 1;
        res.corrected++;
        return idx;
    };

    for (int pi = 0; pi < cfg.passes; ++pi) {
        Pass p;
        p.k = (pi == 0) ? k1 : std::min(n, passes.back().k * 2);
        if (pi == 0) {
            p.perm.resize(n);
            for (uint32_t i = 0; i < n; ++i) p.perm[i] = i;
        } else {
            p.perm = random_permutation(n, rng);
        }
        p.inv.resize(n);
        for (uint32_t i = 0; i < n; ++i) p.inv[p.perm[i]] = i;
        const uint32_t nblk = (n + p.k - 1) / p.k;
        p.alice_par.resize(nblk);
        for (uint32_t b = 0; b < nblk; ++b) p.alice_par[b] = parity_range(alice, p, b * p.k, block_end(p, b));
        res.leaked_bits += nblk;
        passes.push_back(std::move(p));

        std::deque<std::pair<uint32_t, uint32_t>> queue;
        for (uint32_t b = 0; b < nblk; ++b) queue.emplace_back((uint32_t)pi, b);

        while (!queue.empty()) {
            auto [qi, blk] = queue.front();
            queue.pop_front();
            const Pass &qp = passes[qi];
            if (qp.alice_par[blk] == parity_range(bob, qp, blk * qp.k, block_end(qp, blk))) continue;
            uint32_t idx = binary_fix(qi, blk);
            // Efecto cascada: el bit corregido cambia la paridad de su bloque en
            // todos los demas pases ya procesados.
            for (uint32_t oj = 0; oj < passes.size(); ++oj) {
                if (oj == qi) continue;
                queue.emplace_back(oj, passes[oj].inv[idx] / passes[oj].k);
            }
        }
    }
    return res;
}

uint64_t verification_hash(const Bits &key, uint64_t seed) {
    const uint64_t P = (1ULL << 61) - 1;
    auto mulmod = [&](uint64_t a, uint64_t b) {
        unsigned __int128 m = (unsigned __int128)a * b;
        uint64_t lo = (uint64_t)(m & P), hi = (uint64_t)(m >> 61);
        uint64_t r = lo + hi;
        return r >= P ? r - P : r;
    };
    uint64_t r = (seed % (P - 2)) + 2;
    uint64_t h = 0;
    for (size_t k = 0; k < key.size(); k += 32) {
        uint64_t chunk = 0;
        for (size_t j = 0; j < 32 && k + j < key.size(); ++j) chunk |= (uint64_t)key[k + j] << j;
        h = mulmod(h, r) + chunk;
        if (h >= P) h -= P;
    }
    return mulmod(h, r) ^ key.size();
}

// ------------------------------------------------------------ privacidad ----
double binary_entropy(double p) {
    if (p <= 0.0 || p >= 1.0) return 0.0;
    return -p * std::log2(p) - (1 - p) * std::log2(1 - p);
}

uint32_t pa_output_length(uint32_t n, double qber, uint64_t leak_ec, uint64_t leak_verify, double eps_pa) {
    double m = (double)n * (1.0 - binary_entropy(qber)) - (double)leak_ec - (double)leak_verify -
               2.0 * std::log2(1.0 / eps_pa);
    if (m <= 0) return 0;
    return (uint32_t)std::min<double>(std::floor(m), n);
}

Bits toeplitz_hash(const Bits &key, uint32_t n_out, uint64_t seed) {
    const uint32_t n_in = (uint32_t)key.size();
    if (n_out == 0 || n_out > n_in) return Bits();
    SplitMix64 rng(seed);
    std::vector<uint64_t> s((n_in + n_out - 1 + 63) / 64 + 1);
    for (auto &w : s) w = rng.next();
    std::vector<uint64_t> in = pack_bits(key);
    std::vector<uint64_t> out((n_out + 63) / 64 + 1, 0);
    aurora_toeplitz(in.data(), n_in, s.data(), out.data(), n_out);
    return unpack_bits(out.data(), n_out);
}

} // namespace aurora
