#ifndef QUANTUM_KERNELS_H
#define QUANTUM_KERNELS_H

#include <cstdint>
#include <ap_int.h>

// Constantes globales
#define BATCH_LIMIT 4096
#define MODE_SYNC 0
#define MODE_SIFT 1
// AUMENTAMOS EL LÍMITE para no perder la sincronización en ráfagas
#define ALICE_LIMIT 1024

// Estructura de resultados
struct CorrResult {
    uint32_t max_count;
    int16_t best_lag;
};

// Declaración de la función Top-Level
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
);

#endif
