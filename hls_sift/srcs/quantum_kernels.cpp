#include "quantum_kernels.h"
#include <iostream>

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
    #pragma HLS INTERFACE m_axi port=alice_tags offset=slave bundle=gmem0
    #pragma HLS INTERFACE m_axi port=alice_meas offset=slave bundle=gmem0
    #pragma HLS INTERFACE m_axi port=bob_tags   offset=slave bundle=gmem1
    #pragma HLS INTERFACE m_axi port=bob_meas   offset=slave bundle=gmem1
    #pragma HLS INTERFACE m_axi port=sifted_key_out offset=slave bundle=gmem2
    #pragma HLS INTERFACE m_axi port=sifted_count   offset=slave bundle=gmem2

    #pragma HLS INTERFACE s_axilite port=size_a
    #pragma HLS INTERFACE s_axilite port=size_b
    #pragma HLS INTERFACE s_axilite port=window
    #pragma HLS INTERFACE s_axilite port=fixed_lag
    #pragma HLS INTERFACE s_axilite port=mode
    #pragma HLS INTERFACE s_axilite port=return

    result.max_count = 0;
    result.best_lag = 0;

    // --- MODO 0: SYNC ---
    if (mode == MODE_SYNC) {
        LAG_LOOP: for (int16_t lag = -500; lag <= 500; lag++) {
            uint32_t current_corr = 0;
            uint32_t a_ptr = 0;
            BOB_LOOP_SYNC: for (uint32_t b_idx = 0; b_idx < BATCH_LIMIT; b_idx++) {
                #pragma HLS LOOP_TRIPCOUNT min=1 max=4096
                if (b_idx >= size_b) break;
                uint64_t target = bob_tags[b_idx] + lag;
                uint64_t min_range = target - window;
                uint64_t max_range = target + window;

                SEARCH_ALICE_SYNC: for (uint32_t i = 0; i < 64; i++) { // Aquí mantenemos 64 y Unroll para velocidad en Sync
                    #pragma HLS UNROLL factor=8
                    uint32_t idx = a_ptr + i;
                    if (idx < size_a) {
                        if (alice_tags[idx] >= min_range && alice_tags[idx] <= max_range) {
                            current_corr++;
                            a_ptr = idx + 1;
                            break;
                        }
                    }
                }
            }
            if (current_corr > result.max_count) {
                result.max_count = current_corr;
                result.best_lag = lag;
            }
        }

    // --- MODO 1: SIFT ---
    } else if (mode == MODE_SIFT) {

        uint32_t found_bits = 0;
        uint32_t a_ptr = 0;

        // ESTRATEGIA: Para búsqueda profunda (512), quitamos el pipeline externo
        // y pipelineamos el bucle de búsqueda interno.
        BOB_LOOP_SIFT: for (uint32_t b_idx = 0; b_idx < BATCH_LIMIT; b_idx++) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096

            if (b_idx >= size_b) break;

            uint64_t target = bob_tags[b_idx] + fixed_lag;
            uint64_t min_range = target - window;
            uint64_t max_range = target + window;

            uint8_t b_meas = bob_meas[b_idx];
            bool b_basis_diagonal = (b_meas >= 2);

            SEARCH_ALICE_SIFT: for (uint32_t i = 0; i < ALICE_LIMIT; i++) {
                #pragma HLS PIPELINE II=1 // Búsqueda rápida secuencial

                uint32_t idx = a_ptr + i;
                if (idx < size_a) {
                    uint64_t a_tag = alice_tags[idx];

                    // Si nos pasamos de rango, paramos antes (optimización)
                    if (a_tag > max_range + 2000) break;

                    if (a_tag >= min_range && a_tag <= max_range) {
                        uint8_t a_meas = alice_meas[idx];
                        bool a_basis_diagonal = (a_meas >= 2);

                        if (a_basis_diagonal == b_basis_diagonal) {
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
