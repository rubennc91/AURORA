// =============================================================================
// Tests sobre los kernels ANTERIORES de hls_hashing/ (shuffle, paridad, PA).
// Los KNOWN_BUG documentan fallos reales y deben FALLAR (XFAIL).
// =============================================================================
#include <random>
#include <set>
#include <string>
#include <vector>

#include "../../hls_hashing/srcs/quantum_kernels.h"
#include "test_framework.h"

static uint32_t gf2_rank(std::vector<std::vector<uint8_t>> m) {
    uint32_t rank = 0;
    const size_t rows = m.size(), cols = m.empty() ? 0 : m[0].size();
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

KNOWN_BUG(legacy_pa_matrix_has_full_rank) {
    // BUG: 'lfsr >> (j % 64)' repite columnas cada 64 bits => la "clave final"
    // de 500 bits depende de solo ~32 combinaciones lineales de la entrada.
    const uint32_t n = 1000, m = 500;
    std::vector<uint8_t> in(n), out(m);
    std::vector<std::vector<uint8_t>> mat(m, std::vector<uint8_t>(n));
    for (uint32_t j = 0; j < n; ++j) {
        std::fill(in.begin(), in.end(), 0);
        in[j] = 1;
        compute_privacy_amplification_hls(in.data(), out.data(), n, m, 99999);
        for (uint32_t i = 0; i < m; ++i) mat[i][j] = out[i];
    }
    uint32_t r = gf2_rank(mat);
    CHECK_MSG(r == m, "rango GF(2) = " + std::to_string(r) + " de " + std::to_string(m));
}

KNOWN_BUG(legacy_pa_output_depends_on_all_input_bits) {
    // Consecuencia: dos entradas distintas (difieren en los bits j y j+64)
    // producen exactamente la misma clave final.
    const uint32_t n = 1000, m = 500;
    std::vector<uint8_t> x(n, 0), y(n, 0), ox(m), oy(m);
    y[10] = 1;
    y[74] = 1;
    compute_privacy_amplification_hls(x.data(), ox.data(), n, m, 99999);
    compute_privacy_amplification_hls(y.data(), oy.data(), n, m, 99999);
    CHECK_MSG(ox != oy, "colision trivial: entradas distintas -> misma clave");
}

KNOWN_BUG(legacy_shuffle_is_permutation) {
    // BUG: (i*p + seed) % n no es biyeccion cuando n es multiplo de 53 y de 17.
    std::vector<uint8_t> in(BATCH_LIMIT), out(BATCH_LIMIT);
    for (uint32_t n = 1; n <= BATCH_LIMIT; ++n) {
        for (uint32_t i = 0; i < n; ++i) in[i] = (uint8_t)(i & 1);
        // Probamos la biyeccion con indices: marcamos cada posicion origen.
        std::set<uint32_t> src;
        uint32_t step = (n % 53 == 0) ? 17 : 53;
        for (uint32_t i = 0; i < n; ++i) src.insert((i * step + 12345) % n);
        CHECK_MSG(src.size() == n, "n=" + std::to_string(n) + ": solo " + std::to_string(src.size()) + " distintos");
    }
    // Ademas comprobamos el kernel real en el caso que falla.
    const uint32_t n = 901;
    for (uint32_t i = 0; i < n; ++i) in[i] = (uint8_t)(i < 450);
    compute_shuffle_hls(in.data(), out.data(), n, 12345);
    uint32_t ones_in = 450, ones_out = 0;
    for (uint32_t i = 0; i < n; ++i) ones_out += out[i];
    CHECK_MSG(ones_out == ones_in, "el shuffle cambia el peso de la clave: " + std::to_string(ones_out));
}

TEST(legacy_block_parity_is_correct) {
    // La paridad por bloques si es correcta (incluido el ultimo bloque parcial).
    std::mt19937 rng(1);
    for (uint32_t n : {1u, 7u, 8u, 9u, 52u, 1000u}) {
        for (uint32_t bs : {1u, 3u, 8u, 64u}) {
            std::vector<uint8_t> key(n), par(BATCH_LIMIT);
            for (auto &v : key) v = rng() & 1;
            compute_block_parity_hls(key.data(), n, bs, par.data());
            bool ok = true;
            for (uint32_t b = 0; b * bs < n; ++b) {
                uint8_t p = 0;
                for (uint32_t j = b * bs; j < std::min(n, (b + 1) * bs); ++j) p ^= key[j];
                ok &= par[b] == p;
            }
            CHECK_MSG(ok, "n=" + std::to_string(n) + " bs=" + std::to_string(bs));
        }
    }
}

int main() { return tf::run_all("LEGACY hls_hashing (fallos documentados)"); }
