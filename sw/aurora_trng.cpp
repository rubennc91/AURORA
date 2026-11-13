// =============================================================================
// AURORA - TRNG (PS): implementacion
// =============================================================================
#include "aurora_trng.h"
#include "aurora_compat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace aurora {

uint32_t rct_cutoff(double h, double alpha_log2) {
    // SP 800-90B 4.4.1: C = 1 + ceil(-log2(alpha) / H)
    return 1u + (uint32_t)std::ceil(-alpha_log2 / h - 1e-12);
}

// log de la probabilidad binomial P(X = k), X ~ Bin(n, p)
static double log_binom_pmf(uint32_t n, uint32_t k, double p) {
    return compat::lgamma(n + 1.0) - compat::lgamma(k + 1.0) - compat::lgamma(n - k + 1.0) + k * std::log(p) +
           (n - k) * compat::log1p(-p);
}

uint32_t apt_cutoff(double h, double alpha_log2, uint32_t window) {
    // SP 800-90B 4.4.2: C = 1 + CRITBINOM(W, 2^-H, 1 - alpha), donde
    // CRITBINOM(n, p, a) es el menor k con P(X <= k) >= a.
    // Se evalua la cola superior para no perder precision.
    const double p = std::pow(2.0, -h);
    const double alpha = std::pow(2.0, alpha_log2);
    // tail(k) = P(X > k) = sum_{j=k+1..W} pmf(j)
    std::vector<double> pmf(window + 1);
    for (uint32_t j = 0; j <= window; ++j) pmf[j] = std::exp(log_binom_pmf(window, j, p));
    double tail = 0.0;
    std::vector<double> upper(window + 2, 0.0);   // upper[k] = P(X > k)
    for (int64_t k = (int64_t)window; k >= 0; --k) {
        upper[(size_t)k] = tail;
        tail += pmf[(size_t)k];
    }
    for (uint32_t k = 0; k <= window; ++k)
        if (upper[k] <= alpha) return 1u + k;
    return window;
}

HealthResult run_health_tests(const Bits &raw, const HealthConfig &cfg_in) {
    HealthConfig cfg = cfg_in;
    if (cfg.apt_cutoff == 0) cfg.apt_cutoff = apt_cutoff(0.5);
    HealthResult r;
    uint8_t rct_a = 0, apt_a = 0;
    uint32_t rct_b = 0, apt_b = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const uint8_t x = raw[i];
        // RCT
        if (rct_b != 0 && x == rct_a) {
            ++rct_b;
            if (rct_b >= cfg.rct_cutoff && r.rct_fail_idx < 0) r.rct_fail_idx = (int64_t)i;
        } else {
            rct_a = x;
            rct_b = 1;
        }
        // APT
        if (i % APT_WINDOW == 0) {
            apt_a = x;
            apt_b = 1;
        } else if (x == apt_a) {
            ++apt_b;
            if (apt_b >= cfg.apt_cutoff && r.apt_fail_idx < 0) r.apt_fail_idx = (int64_t)i;
        }
        // Arranque: se considera superado tras 1024 muestras sin alarma previa.
        if (i + 1 == APT_WINDOW) r.startup_ok = r.healthy();
    }
    return r;
}

EntropyEstimate estimate_min_entropy(const Bits &raw) {
    EntropyEstimate e;
    const size_t L = raw.size();
    if (L < 2) return e;

    // --- Most Common Value (6.3.1) ---
    size_t ones = 0;
    for (auto b : raw) ones += b;
    double p_hat = (double)std::max(ones, L - ones) / L;
    double p_u = std::min(1.0, p_hat + 2.576 * std::sqrt(p_hat * (1 - p_hat) / (L - 1)));
    e.mcv = -compat::log2(p_u);

    // --- Markov (6.3.3), datos binarios ---
    double c[2][2] = {{0, 0}, {0, 0}};
    for (size_t i = 0; i + 1 < L; ++i) c[raw[i]][raw[i + 1]] += 1;
    const double P0 = (double)(L - ones) / L, P1 = (double)ones / L;
    auto tr = [&](int a, int b) {
        double s = c[a][0] + c[a][1];
        return s > 0 ? c[a][b] / s : 0.0;
    };
    const double P00 = tr(0, 0), P01 = tr(0, 1), P10 = tr(1, 0), P11 = tr(1, 1);
    auto lg = [](double x) { return x > 0 ? compat::log2(x) : -INFINITY; };
    // log2 de la probabilidad de las 6 secuencias candidatas de 128 bits.
    const double cand[6] = {
        lg(P0) + 127 * lg(P00),
        lg(P0) + 64 * lg(P01) + 63 * lg(P10),
        lg(P0) + lg(P01) + 126 * lg(P11),
        lg(P1) + lg(P10) + 126 * lg(P00),
        lg(P1) + 64 * lg(P10) + 63 * lg(P01),
        lg(P1) + 127 * lg(P11),
    };
    double best = -INFINITY;
    for (double v : cand) best = std::max(best, v);
    e.markov = std::min(1.0, -best / 128.0);

    e.min = std::min(e.mcv, e.markov);
    return e;
}

uint32_t extractor_output_length(size_t n, double h_min, double eps_log2) {
    double m = (double)n * h_min - 2.0 * (-eps_log2);
    if (m <= 0) return 0;
    return (uint32_t)std::min<double>(std::floor(m), (double)n);
}

size_t extractor_seed_length(double h_min, double eps_log2) {
    uint32_t m = extractor_output_length(TRNG_EXTRACT_BLOCK, h_min, eps_log2);
    return m ? (size_t)TRNG_EXTRACT_BLOCK + m - 1 : 0;
}

Bits extract_random(const Bits &raw, double h_min, const Bits &public_seed, double eps_log2) {
    Bits out;
    if (public_seed.size() < extractor_seed_length(h_min, eps_log2)) return out;
    for (size_t off = 0; off < raw.size(); off += TRNG_EXTRACT_BLOCK) {
        size_t n = std::min<size_t>(TRNG_EXTRACT_BLOCK, raw.size() - off);
        uint32_t m = extractor_output_length(n, h_min, eps_log2);
        if (m == 0) continue;
        Bits block(raw.begin() + (std::ptrdiff_t)off, raw.begin() + (std::ptrdiff_t)(off + n));
        Bits seed(public_seed.begin(), public_seed.begin() + (std::ptrdiff_t)toeplitz_seed_length(n, m));
        Bits part = toeplitz_hash_seeded(block, m, seed);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

size_t raw_bits_needed(size_t n, double h_min, double eps_log2) {
    const uint32_t m_full = extractor_output_length(TRNG_EXTRACT_BLOCK, h_min, eps_log2);
    if (m_full == 0) return 0;
    size_t full = n / m_full, rem = n - full * m_full;
    size_t raw = full * TRNG_EXTRACT_BLOCK;
    if (rem) {
        size_t r = (size_t)std::ceil((rem - eps_log2 * 2.0) / h_min) + 1;
        while (r < TRNG_EXTRACT_BLOCK && extractor_output_length(r, h_min, eps_log2) < rem) ++r;
        raw += std::min<size_t>(r, TRNG_EXTRACT_BLOCK);
    }
    return raw;
}

bool save_bits_hex(const std::string &path, const Bits &bits, std::string *err) {
    std::ofstream f(path);
    if (!f) { if (err) *err = "no se puede escribir " + path; return false; }
    f << "# bits " << bits.size() << "\n";
    for (size_t k = 0; k < bits.size(); k += 32) {
        uint32_t w = 0;
        for (size_t b = 0; b < 32; ++b) w = (w << 1) | (k + b < bits.size() ? bits[k + b] : 0);
        char buf[16];
        ::snprintf(buf, sizeof buf, "%08x\n", w);
        f << buf;
    }
    return (bool)f;
}

bool load_bits_hex(const std::string &path, Bits &bits, std::string *err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "no se puede abrir " + path; return false; }
    std::string first;
    std::getline(f, first);
    size_t n = 0;
    if (::sscanf(first.c_str(), "# bits %zu", &n) != 1) {
        if (err) *err = "falta la cabecera '# bits N' en " + path;
        return false;
    }
    f.close();
    if (!load_trng_words(path, bits, err)) return false;
    if (bits.size() < n) { if (err) *err = "fichero truncado: " + path; return false; }
    bits.resize(n);
    return true;
}

bool load_trng_words(const std::string &path, Bits &out, std::string *err) {
    out.clear();
    std::ifstream f(path);
    if (!f) {
        if (err) *err = "no se puede abrir " + path;
        return false;
    }
    std::string line;
    size_t lineno = 0;
    while (std::getline(f, line)) {
        ++lineno;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("0x", 0) == 0) line = line.substr(2);
        char *end = nullptr;
        const uint32_t w = (uint32_t)::strtoul(line.c_str(), &end, 16);
        if (line.empty() || end == nullptr || *end != '\0') {
            if (err) *err = "linea " + compat::to_string(lineno) + " invalida en " + path;
            return false;
        }
        for (int b = 31; b >= 0; --b) out.push_back((w >> b) & 1);
    }
    return true;
}

} // namespace aurora
