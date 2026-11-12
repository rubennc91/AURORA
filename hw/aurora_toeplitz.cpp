// =============================================================================
// AURORA - Amplificacion de privacidad: hash de Toeplitz
// =============================================================================
// out = T * in  sobre GF(2), con T[i][j] = seed[i - j + n_in - 1].
// Implementacion por columnas: para cada bit j de entrada a 1, se hace XOR de
// la ventana de n_out bits de la semilla que empieza en (n_in - 1 - j).
// Coste ~ (#unos en la entrada) * ceil(n_out / 64) ciclos con II=1.
// =============================================================================
#include "aurora_kernels.h"

static inline uint64_t seed_window(const uint64_t *seed, uint32_t n_seed_words, uint32_t bit_off) {
#pragma HLS INLINE
    uint32_t w = bit_off >> 6;
    uint32_t s = bit_off & 63;
    uint64_t lo = (w < n_seed_words) ? seed[w] : 0;
    uint64_t hi = (w + 1 < n_seed_words) ? seed[w + 1] : 0;
    return (s == 0) ? lo : ((lo >> s) | (hi << (64 - s)));
}

void aurora_toeplitz(
    const uint64_t *key_in,  uint32_t n_in,
    const uint64_t *seed,
    uint64_t       *key_out, uint32_t n_out
) {
#pragma HLS INTERFACE m_axi port=key_in  offset=slave bundle=gmem0 depth=2048
#pragma HLS INTERFACE m_axi port=seed    offset=slave bundle=gmem1 depth=4096
#pragma HLS INTERFACE m_axi port=key_out offset=slave bundle=gmem2 depth=2048
#pragma HLS INTERFACE s_axilite port=n_in
#pragma HLS INTERFACE s_axilite port=n_out
#pragma HLS INTERFACE s_axilite port=return

    if (n_in == 0 || n_out == 0 || n_out > n_in || n_in > AURORA_PA_MAX_IN_BITS) return;

    const uint32_t n_in_words = (n_in + 63) >> 6;
    const uint32_t n_out_words = (n_out + 63) >> 6;
    const uint32_t n_seed_bits = n_in + n_out - 1;
    const uint32_t n_seed_words = (n_seed_bits + 63) >> 6;

    uint64_t seed_buf[AURORA_PA_MAX_SEED_WORDS];
    uint64_t acc[AURORA_PA_MAX_IN_WORDS];
#pragma HLS BIND_STORAGE variable=seed_buf type=ram_2p impl=uram
#pragma HLS BIND_STORAGE variable=acc type=ram_t2p impl=bram

LOAD_SEED:
    for (uint32_t i = 0; i < AURORA_PA_MAX_SEED_WORDS; i++) {
#pragma HLS PIPELINE II=1
        if (i >= n_seed_words) break;
        uint64_t v = seed[i];
        // Limpia los bits por encima de n_seed_bits (semilla exactamente definida).
        if (i == n_seed_words - 1 && (n_seed_bits & 63) != 0)
            v &= (~0ULL) >> (64 - (n_seed_bits & 63));
        seed_buf[i] = v;
    }

CLEAR_ACC:
    for (uint32_t q = 0; q < AURORA_PA_MAX_IN_WORDS; q++) {
#pragma HLS PIPELINE II=1
        if (q >= n_out_words) break;
        acc[q] = 0;
    }

COLS_WORDS:
    for (uint32_t jw = 0; jw < AURORA_PA_MAX_IN_WORDS; jw++) {
        if (jw >= n_in_words) break;
        uint64_t word = key_in[jw];
    COLS_BITS:
        for (uint32_t jb = 0; jb < 64; jb++) {
            uint32_t j = (jw << 6) + jb;
            if (j >= n_in) break;
            if ((word >> jb) & 1) {
                uint32_t base = n_in - 1 - j;
            ROWS:
                for (uint32_t q = 0; q < AURORA_PA_MAX_IN_WORDS; q++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=acc type=inter false
#pragma HLS LOOP_TRIPCOUNT min=1 max=2048
                    if (q >= n_out_words) break;
                    acc[q] ^= seed_window(seed_buf, n_seed_words, base + (q << 6));
                }
            }
        }
    }

WRITE_OUT:
    for (uint32_t q = 0; q < AURORA_PA_MAX_IN_WORDS; q++) {
#pragma HLS PIPELINE II=1
        if (q >= n_out_words) break;
        uint64_t v = acc[q];
        if (q == n_out_words - 1 && (n_out & 63) != 0)
            v &= (~0ULL) >> (64 - (n_out & 63));
        key_out[q] = v;
    }
}
