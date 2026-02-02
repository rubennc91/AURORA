#include "quantum_kernels.h"
#include <cstdint>

// ===========================================================
// KERNEL 1: CORRELACIÓN (MODO SEGURO / SAFE MODE)
// ===========================================================
void compute_correlation_hls(
    const uint64_t alice_tags[BATCH_LIMIT],
    const uint8_t  alice_meas[BATCH_LIMIT],
    const uint64_t bob_tags[BATCH_LIMIT],
    const uint8_t  bob_meas[BATCH_LIMIT],
    uint32_t size_a,
    uint32_t size_b,
    uint64_t window,
    int16_t  fixed_lag,
    uint8_t  mode,
    uint8_t  *sifted_key_out,
    uint32_t *sifted_count,
    CorrResult &result
) {
    #pragma HLS INTERFACE m_axi port=alice_tags offset=slave bundle=gmem0 depth=4096
    #pragma HLS INTERFACE m_axi port=alice_meas offset=slave bundle=gmem0 depth=4096
    #pragma HLS INTERFACE m_axi port=bob_tags   offset=slave bundle=gmem1 depth=4096
    #pragma HLS INTERFACE m_axi port=bob_meas   offset=slave bundle=gmem1 depth=4096
    #pragma HLS INTERFACE m_axi port=sifted_key_out offset=slave bundle=gmem2 depth=4096
    #pragma HLS INTERFACE m_axi port=sifted_count   offset=slave bundle=gmem2
    #pragma HLS INTERFACE s_axilite port=return

    uint32_t local_max_count = 0;
    int16_t local_best_lag = 0;

    // --- MODO 0: SYNC ---
    if (mode == MODE_SYNC) {

        LAG_LOOP: for (int16_t lag = -500; lag <= 500; lag++) {
            #pragma HLS LOOP_TRIPCOUNT min=1000 max=1001

            uint32_t current_corr = 0;
            uint32_t a_ptr = 0;

            BOB_LOOP_SYNC: for (uint32_t b_idx = 0; b_idx < BATCH_LIMIT; b_idx++) {
                #pragma HLS LOOP_TRIPCOUNT min=4096 max=4096
                // SIN PIPELINE AQUÍ. Dejamos que Bob sea secuencial.

                if (b_idx >= size_b) break;

                uint64_t b_tag = bob_tags[b_idx];
                uint64_t target = b_tag + lag;
                uint64_t min_range = (target > window) ? (target - window) : 0;
                uint64_t max_range = target + window;

                // PIPELINE AQUÍ: El buscador interno irá a 1 dato por ciclo.
                // Quitamos el UNROLL para reducir el tamaño del diseño.
                SEARCH_ALICE_SYNC: for (uint32_t i = 0; i < 64; i++) {
                    #pragma HLS PIPELINE II=1

                    uint32_t idx = a_ptr + i;
                    if (idx < size_a) {
                        uint64_t a_tag = alice_tags[idx];
                        if (a_tag >= min_range && a_tag <= max_range) {
                            current_corr++;
                            a_ptr = idx;
                        }
                    }
                }
            }
            if (current_corr > local_max_count) {
                local_max_count = current_corr;
                local_best_lag = lag;
            }
        }
        result.max_count = local_max_count;
        result.best_lag = local_best_lag;

    }
    // --- MODO 1: SIFT ---
    else if (mode == MODE_SIFT) {

        uint32_t found_bits = 0;
        uint32_t a_ptr = 0;

        BOB_LOOP_SIFT: for (uint32_t b_idx = 0; b_idx < BATCH_LIMIT; b_idx++) {
            #pragma HLS LOOP_TRIPCOUNT min=4096 max=4096
            // SIN PIPELINE AQUÍ.

            if (b_idx >= size_b) break;

            uint64_t target = bob_tags[b_idx] + fixed_lag;
            uint64_t min_range = target - window;
            uint64_t max_range = target + window;
            uint8_t b_meas = bob_meas[b_idx];
            bool b_diag = (b_meas >= 2);

            // PIPELINE AQUÍ (Bucle interno)
            SEARCH_ALICE_SIFT: for (uint32_t i = 0; i < ALICE_LIMIT; i++) {
                #pragma HLS PIPELINE II=1

                uint32_t idx = a_ptr + i;
                if (idx < size_a) {
                    uint64_t a_tag = alice_tags[idx];
                    if (a_tag > max_range + 2000) break;

                    if (a_tag >= min_range && a_tag <= max_range) {
                        uint8_t a_meas = alice_meas[idx];
                        bool a_diag = (a_meas >= 2);
                        if (a_diag == b_diag) {
                            sifted_key_out[found_bits] = (a_meas % 2);
                            found_bits++;
                        }
                        a_ptr = idx + 1;
                        break;
                    }
                }
            }
        }
        *sifted_count = found_bits;
    }
}
