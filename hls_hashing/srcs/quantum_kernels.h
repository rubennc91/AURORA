#ifndef QUANTUM_KERNELS_H
#define QUANTUM_KERNELS_H

#include <cstdint>
#include <ap_int.h>

#define BATCH_LIMIT 4096
#define ALICE_LIMIT 1024 // Límite ampliado para no perder sincronización

#define MODE_SYNC 0
#define MODE_SIFT 1

struct CorrResult {
    uint32_t max_count;
    int16_t best_lag;
};

// 1. CORRELACIÓN Y CRIBADO
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

// 2. NUEVO: PERMUTACIÓN (SHUFFLE)
// Reordena la clave de forma determinista basada en una semilla
void compute_shuffle_hls(
    const uint8_t *input_key,
    uint8_t       *output_key,
    uint32_t      key_length,
    uint32_t      seed
);

// 3. PARIDAD DE BLOQUES
void compute_block_parity_hls(
    const uint8_t *input_key,
    uint32_t      key_length,
    uint32_t      block_size,
    uint8_t       *parity_out
);

// 4: AMPLIFICACIÓN DE PRIVACIDAD (Toeplitz Hashing)
// Comprime la clave corregida (input) en una clave final (output) más corta y segura.
void compute_privacy_amplification_hls(
    const uint8_t *corrected_key, // Entrada (Clave con fugas de info)
    uint8_t       *final_key,     // Salida (Clave final secreta)
    uint32_t      input_len,      // Longitud entrada (ej: 1000)
    uint32_t      output_len,     // Longitud salida (ej: 500)
    uint64_t      seed            // Semilla pública para generar la matriz
);

#endif
