#include "../include/quantum_kernels.h"

void compute_correlation_hls(
    const uint64_t alice_tags[BATCH_LIMIT],
    const uint64_t bob_tags[BATCH_LIMIT],
    uint32_t size_a,
    uint32_t size_b,
    uint64_t window,
    CorrResult &result
) {
    // Interfaces AXI4 para comunicación
    #pragma HLS INTERFACE m_axi port=alice_tags offset=slave bundle=gmem0
    #pragma HLS INTERFACE m_axi port=bob_tags offset=slave bundle=gmem1
    #pragma HLS INTERFACE s_axilite port=size_a
    #pragma HLS INTERFACE s_axilite port=size_b
    #pragma HLS INTERFACE s_axilite port=window
    #pragma HLS INTERFACE s_axilite port=result
    #pragma HLS INTERFACE s_axilite port=return

	#pragma HLS ARRAY_PARTITION variable=alice_tags cyclic factor=16 dim=1

    result.max_count = 0;
    result.best_lag = 0;

    // Bucle Principal: Recorre los posibles desfases temporales (Lags)
    LAG_LOOP: for (int16_t lag = -500; lag <= 500; lag++) {
		// #pragma HLS PIPELINE II=4096
        uint32_t current_corr = 0;
        uint32_t a_ptr = 0;

        // Bucle de Bob: Procesa cada etiqueta de tiempo
        BOB_LOOP: for (uint32_t b_idx = 0; b_idx < BATCH_LIMIT; b_idx++) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096
			// #pragma HLS PIPELINE II=64

            if (b_idx >= size_b) break;

            uint64_t target = bob_tags[b_idx] + lag;
            uint64_t min_range = target - window;
            uint64_t max_range = target + window;

            // Bucle de Búsqueda en Alice (Ventana Deslizante)
            SEARCH_ALICE: for (uint32_t i = 0; i < ALICE_LIMIT; i++) {
                #pragma HLS UNROLL // factor=8

                uint32_t idx = a_ptr + i;
                if (idx < size_a) {
                    uint64_t a_tag = alice_tags[idx];

                    // Lógica de coincidencia
                    if (a_tag >= min_range && a_tag <= max_range) {
                        current_corr++;
                        // Actualizamos el puntero para no volver a buscar atrás
                        a_ptr = idx + 1;
                        break; // Salimos al encontrar la primera coincidencia
                    }
                }
            }
        }

        // Guardamos el mejor resultado encontrado
        if (current_corr > result.max_count) {
            result.max_count = current_corr;
            result.best_lag = lag;
        }
    }
}
