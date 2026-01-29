#include "quantum_kernels.h"

void compute_correlation_hls(
    const uint64_t alice_tags[BATCH_LIMIT],
    const uint64_t bob_tags[BATCH_LIMIT],
    uint32_t size_a,
    uint32_t size_b,
    uint64_t window,
    CorrResult &result
) {
    // Directivas para la FPGA (Zynq UltraScale+)
    #pragma HLS INTERFACE m_axi port=alice_tags offset=slave bundle=gmem0
    #pragma HLS INTERFACE m_axi port=bob_tags offset=slave bundle=gmem1
    #pragma HLS INTERFACE s_axilite port=size_a
    #pragma HLS INTERFACE s_axilite port=size_b
    #pragma HLS INTERFACE s_axilite port=window
    #pragma HLS INTERFACE s_axilite port=result
    #pragma HLS INTERFACE s_axilite port=return

    result.max_count = 0;
    result.best_lag = 0;

    for (int64_t lag = -500; lag <= 500; lag++) {
        #pragma HLS PIPELINE II=1
        uint32_t current_corr = 0;
        uint32_t a_idx = 0;

        for (uint32_t b_idx = 0; b_idx < size_b; b_idx++) {
            #pragma HLS LOOP_TRIPCOUNT min=1 max=4096
            while (a_idx < size_a && alice_tags[a_idx] < (bob_tags[b_idx] + lag - window)) {
                a_idx++;
            }
            if (a_idx < size_a && alice_tags[a_idx] <= (bob_tags[b_idx] + lag + window)) {
                current_corr++;
            }
        }

        if (current_corr > result.max_count) {
            result.max_count = current_corr;
            result.best_lag = lag;
        }
    }
}
