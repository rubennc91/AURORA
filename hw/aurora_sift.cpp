// =============================================================================
// AURORA - Kernel de coincidencias + sifting BB84
// =============================================================================
#include "aurora_kernels.h"

void aurora_sift(
    const uint64_t *alice_tags, const uint8_t *alice_states, uint32_t n_alice,
    const uint64_t *bob_tags,   const uint8_t *bob_states,   uint32_t n_bob,
    int64_t  lag,
    uint32_t half_window,
    uint64_t *alice_key,
    uint64_t *bob_key,
    SiftResult *result
) {
#pragma HLS INTERFACE m_axi port=alice_tags   offset=slave bundle=gmem0 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=alice_states offset=slave bundle=gmem1 depth=65536
#pragma HLS INTERFACE m_axi port=bob_tags     offset=slave bundle=gmem2 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=bob_states   offset=slave bundle=gmem3 depth=65536
#pragma HLS INTERFACE m_axi port=alice_key    offset=slave bundle=gmem4 depth=1024
#pragma HLS INTERFACE m_axi port=bob_key      offset=slave bundle=gmem5 depth=1024
#pragma HLS INTERFACE s_axilite port=n_alice
#pragma HLS INTERFACE s_axilite port=n_bob
#pragma HLS INTERFACE s_axilite port=lag
#pragma HLS INTERFACE s_axilite port=half_window
#pragma HLS INTERFACE s_axilite port=result
#pragma HLS INTERFACE s_axilite port=return

    if (n_alice > AURORA_MAX_TAGS) n_alice = AURORA_MAX_TAGS;
    if (n_bob > AURORA_MAX_TAGS) n_bob = AURORA_MAX_TAGS;

    const int64_t w = (int64_t)half_window;

    uint32_t ai = 0, b = 0;
    uint32_t n_coinc = 0, n_sifted = 0;
    uint64_t wa = 0, wb = 0;       // palabras de clave en construccion
    uint32_t word_idx = 0;

    // Un unico bucle aplanado: en cada iteracion o bien se descarta un tag de
    // Alice demasiado antiguo, o bien se resuelve el evento de Bob actual.
    // Iteraciones <= n_alice + n_bob.
MAIN_LOOP:
    for (uint32_t it = 0; it < 2u * AURORA_MAX_TAGS; it++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=131072
        if (b >= n_bob) break;

        uint64_t tb = bob_tags[b];
        bool have_a = (ai < n_alice);
        // d = t_a - t_b - lag, con signo (tags < 2^62).
        int64_t d = have_a ? (int64_t)(alice_tags[ai] - tb) - lag : 0;

        if (have_a && d < -w) {
            ai++;                                  // Alice demasiado pronto
        } else {
            if (have_a && d <= w) {                // coincidencia
                uint8_t sa = alice_states[ai];
                uint8_t sb = bob_states[b];
                n_coinc++;
                ai++;                              // cada tag de Alice se usa una vez
                if (((sa >> 1) & 1) == ((sb >> 1) & 1)) {   // misma base
                    uint32_t pos = n_sifted & 63;
                    wa |= (uint64_t)(sa & 1) << pos;
                    wb |= (uint64_t)(sb & 1) << pos;
                    n_sifted++;
                    if (pos == 63) {
                        alice_key[word_idx] = wa;
                        bob_key[word_idx] = wb;
                        word_idx++;
                        wa = 0; wb = 0;
                    }
                }
            }
            b++;
        }
    }
    if ((n_sifted & 63) != 0) {
        alice_key[word_idx] = wa;
        bob_key[word_idx] = wb;
    }

    result->n_coincidences = n_coinc;
    result->n_sifted = n_sifted;
    result->next_alice_idx = ai;
}
