#ifndef QUANTUM_KERNELS_H
#define QUANTUM_KERNELS_H

#include <stdint.h>

#define BATCH_LIMIT 4096 

struct CorrResult {
    int64_t best_lag;
    uint32_t max_count;
};

void compute_correlation_hls(
    const uint64_t alice_tags[BATCH_LIMIT],
    const uint64_t bob_tags[BATCH_LIMIT],
    uint32_t size_a,
    uint32_t size_b,
    uint64_t window,
    CorrResult &result
);

#endif
