// =============================================================================
// AURORA - Kernels HLS (PL) para post-procesado QKD BB84
// =============================================================================
// Convenciones comunes a todos los kernels:
//   * Time-tags en picosegundos, uint64_t, ordenados de forma no decreciente y
//     < 2^62 (para poder restar en int64_t sin desbordamiento).
//   * lag (ps) = t_alice - t_bob para un par correlado.
//   * Estados: 0=H, 1=V (base rectilinea), 2=D, 3=A (base diagonal).
//     base = (estado >= 2), bit = (estado & 1).
//   * Claves empaquetadas en palabras de 64 bits, LSB primero:
//       bit k  ->  word[k >> 6], posicion (k & 63).
//
// Los kernels son C++ sintetizable (sin memoria dinamica ni STL). Toda la
// logica interactiva (Cascade, estimacion de parametros, troceado en lotes)
// vive en el PS (ver sw/aurora_host.h).
// =============================================================================
#ifndef AURORA_KERNELS_H
#define AURORA_KERNELS_H

#include <stdint.h>

// Capacidad maxima de un lote por kernel. Un batch real (~25 ms) tiene
// ~35k tags de Alice y ~17k de Bob. Lotes mayores se trocean en el host.
#define AURORA_MAX_TAGS      65536
#define AURORA_MAX_KEY_WORDS (AURORA_MAX_TAGS / 64)

// Histograma de sincronizacion.
#define AURORA_SYNC_MAX_BINS 1024

// Amplificacion de privacidad (Toeplitz): limites del bloque de entrada/salida.
#define AURORA_PA_MAX_IN_BITS   (1 << 17)                // 131072 bits
#define AURORA_PA_MAX_IN_WORDS  (AURORA_PA_MAX_IN_BITS / 64)
#define AURORA_PA_MAX_SEED_WORDS (2 * AURORA_PA_MAX_IN_WORDS)

// -----------------------------------------------------------------------------
// KERNEL 1: SINCRONIZACION
// Histograma de diferencias  d = t_alice - t_bob  para todos los pares con
//   lag_min <= d < lag_min + (n_bins << bin_shift).
// Algoritmo de dos punteros: O(na + nb + pares en rango) ciclos, frente a
// O(n_lags * nb * ventana) de la version anterior.
// El PS calcula el lag final (centroide con resta de fondo) sobre el histograma.
// -----------------------------------------------------------------------------
struct SyncResult {
    uint32_t peak_bin;      // bin con mas cuentas
    uint32_t peak_count;    // cuentas en ese bin
    uint32_t total_pairs;   // pares que cayeron en el rango del histograma
    uint32_t complete;      // 1 si se procesaron todos los eventos de Bob
};

void aurora_sync(
    const uint64_t *alice_tags, uint32_t n_alice,
    const uint64_t *bob_tags,   uint32_t n_bob,
    int64_t  lag_min,            // ps, inicio del rango (normalmente negativo)
    uint32_t bin_shift,          // ancho de bin = 2^bin_shift ps (evita divisores en HW)
    uint32_t n_bins,             // <= AURORA_SYNC_MAX_BINS
    uint32_t *histogram,         // salida: n_bins cuentas
    SyncResult *result
);

// -----------------------------------------------------------------------------
// KERNEL 2: COINCIDENCIAS + SIFTING
// Para cada evento de Bob (en orden) busca el primer tag de Alice no usado con
// |t_a - t_b - lag| <= half_window. Cada tag de Alice se usa como mucho una vez.
// Si las bases coinciden, se anade un bit a la clave de Alice y a la de Bob.
// Toda la aritmetica de tiempos es con signo: no hay desbordamientos cerca de t=0.
// -----------------------------------------------------------------------------
struct SiftResult {
    uint32_t n_coincidences;    // pares temporales encontrados
    uint32_t n_sifted;          // bits con base coincidente (longitud de clave)
    uint32_t next_alice_idx;    // primer indice de Alice no consumido (para trocear)
};

void aurora_sift(
    const uint64_t *alice_tags, const uint8_t *alice_states, uint32_t n_alice,
    const uint64_t *bob_tags,   const uint8_t *bob_states,   uint32_t n_bob,
    int64_t  lag,                // ps
    uint32_t half_window,        // ps
    uint64_t *alice_key,         // salida empaquetada (AURORA_MAX_KEY_WORDS)
    uint64_t *bob_key,           // salida empaquetada (AURORA_MAX_KEY_WORDS)
    SiftResult *result
);

// -----------------------------------------------------------------------------
// KERNEL 3: AMPLIFICACION DE PRIVACIDAD (hash de Toeplitz)
//   out[i] = XOR_j  T[i][j] & in[j],   T[i][j] = seed[i - j + n_in - 1]
// La semilla tiene n_in + n_out - 1 bits y debe ser aleatoria y compartida
// (no secreta). T es una familia 2-universal => valida para PA (Leftover Hash).
// Requiere n_out <= n_in.
// -----------------------------------------------------------------------------
void aurora_toeplitz(
    const uint64_t *key_in,  uint32_t n_in,
    const uint64_t *seed,                 // n_in + n_out - 1 bits empaquetados
    uint64_t       *key_out, uint32_t n_out
);

// -----------------------------------------------------------------------------
// aurora_ldpc_dec: decodificador LDPC por sindrome (min-sum normalizado por
// capas, coma fija). Ver hw/aurora_ldpc_dec.cpp.
// -----------------------------------------------------------------------------
#define AURORA_LDPC_MAX_N    16384
#define AURORA_LDPC_MAX_M    8192
#define AURORA_LDPC_MAX_E    98304
#define AURORA_LDPC_MAX_DEG  128     // grado maximo de comprobacion
#define AURORA_LDPC_MAX_ITER 255
#define AURORA_LDPC_LLR_SCALE 4      // LLR int8 = round(4 * LLR natural), saturado a +-127

void aurora_ldpc_dec(
    const uint32_t *chk_ptr, const uint16_t *chk_var, uint32_t n, uint32_t m, uint32_t load_code,
    const int8_t *llr, const uint8_t *syndrome, uint32_t max_iter,
    uint8_t *x_out, uint32_t *status          // status[0] = exito, status[1] = iteraciones
);

#endif // AURORA_KERNELS_H
