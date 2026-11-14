// Datos embebidos en la aplicacion bare-metal (los genera
// tools/make_baremetal_data.py en build/baremetal_src/aurora_data.cpp), con los
// resultados ESPERADOS calculados en el PC con la cadena ya validada.
#ifndef AURORA_BM_DATA_H
#define AURORA_BM_DATA_H

#include <cstdint>

struct BmBatch {
    const char *name;
    uint32_t n_alice;
    const uint64_t *alice_tags;
    const uint8_t *alice_states;
    uint32_t n_bob;
    const uint64_t *bob_tags;
    const uint8_t *bob_states;
    int64_t exp_lag_ps;          // lag redondeado (ps)
    uint32_t exp_coincidences;
    uint32_t exp_sifted;
    const char *exp_alice_key;   // hex, formato bits_to_hex
    const char *exp_bob_key;
};

extern const BmBatch bm_batches[];
extern const uint32_t bm_n_batches;

// Compensacion de retardos por detector (ps, estados H V D A), calibrada en el
// PC con todos los batches (aurora_cli calibrate-delays). Si bm_delays_valid,
// la aplicacion la aplica a Alice y Bob antes de sincronizar y los resultados
// esperados de bm_batches y las claves cribadas ya la incluyen; ventana de
// coincidencia bm_half_window_ps (+-350 ps con compensacion, +-400 sin ella).
extern const bool bm_delays_valid;
extern const double bm_delays_alice[4];
extern const double bm_delays_bob[4];
extern const uint32_t bm_half_window_ps;

// Caso de Toeplitz con su resultado calculado con un modelo Python independiente.
extern const uint32_t bm_toep_n_in, bm_toep_n_out;
extern const uint8_t bm_toep_in[];      // bits (0/1)
extern const uint8_t bm_toep_seed[];    // n_in + n_out - 1 bits
extern const char *bm_toep_expected;    // hex, formato bits_to_hex

// Claves cribadas de TODOS los batches calculadas en el PC (entrada de la
// prueba de la cadena completa, igual que 'make pipeline').
extern const uint32_t bm_sifted_len;
extern const uint8_t bm_sifted_alice[];
extern const uint8_t bm_sifted_bob[];

// Semilla publica del extractor del TRNG (generada en la instalacion) y clave
// precompartida de autenticacion (SOLO PRUEBAS: va embebida en el binario).
extern const uint32_t bm_extractor_seed_len;
extern const uint8_t bm_extractor_seed[];
extern const uint32_t bm_psk_len;
extern const uint8_t bm_psk[];

#endif
