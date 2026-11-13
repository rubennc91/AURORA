// =============================================================================
// AURORA - Backend HARDWARE de los kernels para la aplicacion bare-metal
// =============================================================================
// Implementa aurora_sync / aurora_sift / aurora_toeplitz / aurora_ldpc_dec con la MISMA firma que
// los modelos C++ de hw/, pero programando los kernels HLS de la PL. Asi todo
// el software del PS (estimate_lag, sift_batch, toeplitz_hash, Cascade,
// autenticacion) se ejecuta en la placa sin cambios usando la FPGA.
//
// Offsets de registros: cabeceras *_hw.h que genera Vitis HLS (las incluye el
// BSP de Vitis a partir del .xsa). Puertos m_axi a la DDR por S_AXI_HP0_FPD,
// que NO es coherente: se limpia la cache antes de lanzar el kernel y se
// invalida despues.
// =============================================================================
#include <cstddef>
#include <cstdint>

#include "aurora_board.h"
#include "aurora_kernels.h"

#include "xil_cache.h"
#include "xil_io.h"
#include "xaurora_sift_hw.h"
#include "xaurora_sync_hw.h"
#include "xaurora_toeplitz_hw.h"
#include "xaurora_ldpc_dec_hw.h"

namespace aurora {
bool hw_kernel_timeout = false;
}

namespace {

using aurora::board::KERNEL_TIMEOUT_POLLS;

inline void wr32(uintptr_t base, uint32_t off, uint32_t v) { Xil_Out32(base + off, v); }
inline uint32_t rd32(uintptr_t base, uint32_t off) { return Xil_In32(base + off); }
inline void wr64(uintptr_t base, uint32_t off, uint64_t v) {
    wr32(base, off, (uint32_t)v);
    wr32(base, off + 4, (uint32_t)(v >> 32));
}
inline uint64_t addr(const void *p) { return (uint64_t)(uintptr_t)p; }

// Limpia (escribe a DDR) e invalida: antes de lanzar el kernel, para entradas
// y salidas (asi ninguna linea sucia sobrescribe despues lo que escriba la PL).
inline void flush(const void *p, size_t n) {
    if (p && n) Xil_DCacheFlushRange((INTPTR)(uintptr_t)p, (INTPTR)n);
}
// Tras el kernel: descarta la cache de las salidas para leer lo que escribio la PL.
inline void invalidate(const void *p, size_t n) {
    if (p && n) Xil_DCacheInvalidateRange((INTPTR)(uintptr_t)p, (INTPTR)n);
}

// ap_ctrl: bit0 ap_start, bit1 ap_done (se borra al leer), bit2 ap_idle.
bool start_and_wait(uintptr_t base, uint32_t ctrl) {
    wr32(base, ctrl, 1u);
    for (uint64_t i = 0; i < KERNEL_TIMEOUT_POLLS; ++i)
        if (rd32(base, ctrl) & 0x2u) return true;
    aurora::hw_kernel_timeout = true;
    return false;
}

} // namespace

// ------------------------------------------------------------------ sync ----
void aurora_sync(const uint64_t *alice_tags, uint32_t n_alice, const uint64_t *bob_tags, uint32_t n_bob,
                 int64_t lag_min, uint32_t bin_shift, uint32_t n_bins, uint32_t *histogram, SyncResult *result) {
    const uintptr_t B = aurora::board::SYNC_BASE;
    const uint32_t na = n_alice > AURORA_MAX_TAGS ? AURORA_MAX_TAGS : n_alice;
    const uint32_t nb = n_bob > AURORA_MAX_TAGS ? AURORA_MAX_TAGS : n_bob;
    const uint32_t nh = n_bins > AURORA_SYNC_MAX_BINS ? AURORA_SYNC_MAX_BINS : n_bins;
    flush(alice_tags, (size_t)na * 8);
    flush(bob_tags, (size_t)nb * 8);
    flush(histogram, (size_t)nh * 4);

    wr64(B, XAURORA_SYNC_CONTROL_ADDR_ALICE_TAGS_DATA, addr(alice_tags));
    wr32(B, XAURORA_SYNC_CONTROL_ADDR_N_ALICE_DATA, n_alice);
    wr64(B, XAURORA_SYNC_CONTROL_ADDR_BOB_TAGS_DATA, addr(bob_tags));
    wr32(B, XAURORA_SYNC_CONTROL_ADDR_N_BOB_DATA, n_bob);
    wr64(B, XAURORA_SYNC_CONTROL_ADDR_LAG_MIN_DATA, (uint64_t)lag_min);
    wr32(B, XAURORA_SYNC_CONTROL_ADDR_BIN_SHIFT_DATA, bin_shift);
    wr32(B, XAURORA_SYNC_CONTROL_ADDR_N_BINS_DATA, n_bins);
    wr64(B, XAURORA_SYNC_CONTROL_ADDR_HISTOGRAM_DATA, addr(histogram));

    *result = SyncResult{};
    if (!start_and_wait(B, XAURORA_SYNC_CONTROL_ADDR_AP_CTRL)) return;
    invalidate(histogram, (size_t)nh * 4);

    // Estructura de salida por s_axilite. Vitis HLS 2022.2 la empaqueta en un
    // registro ancho (campos en orden, el primero en los bits bajos); versiones
    // que no agregan generan un registro por campo.
#if defined(XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA)
    result->peak_bin = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA + 0);
    result->peak_count = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA + 4);
    result->total_pairs = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA + 8);
    result->complete = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA + 12);
#elif defined(XAURORA_SYNC_CONTROL_ADDR_RESULT_PEAK_BIN_DATA)
    result->peak_bin = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_PEAK_BIN_DATA);
    result->peak_count = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_PEAK_COUNT_DATA);
    result->total_pairs = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_TOTAL_PAIRS_DATA);
    result->complete = rd32(B, XAURORA_SYNC_CONTROL_ADDR_RESULT_COMPLETE_DATA);
#else
#error "No se reconoce el registro 'result' en xaurora_sync_hw.h: envia ese fichero para adaptar el backend"
#endif
}

// ------------------------------------------------------------------ sift ----
void aurora_sift(const uint64_t *alice_tags, const uint8_t *alice_states, uint32_t n_alice,
                 const uint64_t *bob_tags, const uint8_t *bob_states, uint32_t n_bob, int64_t lag,
                 uint32_t half_window, uint64_t *alice_key, uint64_t *bob_key, SiftResult *result) {
    const uintptr_t B = aurora::board::SIFT_BASE;
    const uint32_t na = n_alice > AURORA_MAX_TAGS ? AURORA_MAX_TAGS : n_alice;
    const uint32_t nb = n_bob > AURORA_MAX_TAGS ? AURORA_MAX_TAGS : n_bob;
    // Como mucho un bit por Bob: palabras de clave necesarias.
    const size_t key_bytes = (((size_t)nb + 63) / 64 + 1) * 8;
    flush(alice_tags, (size_t)na * 8);
    flush(alice_states, na);
    flush(bob_tags, (size_t)nb * 8);
    flush(bob_states, nb);
    flush(alice_key, key_bytes);
    flush(bob_key, key_bytes);

    wr64(B, XAURORA_SIFT_CONTROL_ADDR_ALICE_TAGS_DATA, addr(alice_tags));
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_ALICE_STATES_DATA, addr(alice_states));
    wr32(B, XAURORA_SIFT_CONTROL_ADDR_N_ALICE_DATA, n_alice);
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_BOB_TAGS_DATA, addr(bob_tags));
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_BOB_STATES_DATA, addr(bob_states));
    wr32(B, XAURORA_SIFT_CONTROL_ADDR_N_BOB_DATA, n_bob);
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_LAG_DATA, (uint64_t)lag);
    wr32(B, XAURORA_SIFT_CONTROL_ADDR_HALF_WINDOW_DATA, half_window);
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_ALICE_KEY_DATA, addr(alice_key));
    wr64(B, XAURORA_SIFT_CONTROL_ADDR_BOB_KEY_DATA, addr(bob_key));

    *result = SiftResult{};
    if (!start_and_wait(B, XAURORA_SIFT_CONTROL_ADDR_AP_CTRL)) return;
    invalidate(alice_key, key_bytes);
    invalidate(bob_key, key_bytes);

#if defined(XAURORA_SIFT_CONTROL_ADDR_RESULT_DATA)
    result->n_coincidences = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_DATA + 0);
    result->n_sifted = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_DATA + 4);
    result->next_alice_idx = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_DATA + 8);
#elif defined(XAURORA_SIFT_CONTROL_ADDR_RESULT_N_COINCIDENCES_DATA)
    result->n_coincidences = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_N_COINCIDENCES_DATA);
    result->n_sifted = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_N_SIFTED_DATA);
    result->next_alice_idx = rd32(B, XAURORA_SIFT_CONTROL_ADDR_RESULT_NEXT_ALICE_IDX_DATA);
#else
#error "No se reconoce el registro 'result' en xaurora_sift_hw.h: envia ese fichero para adaptar el backend"
#endif
}

// -------------------------------------------------------------- toeplitz ----
void aurora_toeplitz(const uint64_t *key_in, uint32_t n_in, const uint64_t *seed, uint64_t *key_out,
                     uint32_t n_out) {
    const uintptr_t B = aurora::board::TOEPLITZ_BASE;
    const size_t in_bytes = ((size_t)n_in + 63) / 64 * 8;
    const size_t seed_bytes = ((size_t)n_in + n_out - 1 + 63) / 64 * 8;
    const size_t out_bytes = ((size_t)n_out + 63) / 64 * 8;
    flush(key_in, in_bytes);
    flush(seed, seed_bytes);
    flush(key_out, out_bytes);

    wr64(B, XAURORA_TOEPLITZ_CONTROL_ADDR_KEY_IN_DATA, addr(key_in));
    wr32(B, XAURORA_TOEPLITZ_CONTROL_ADDR_N_IN_DATA, n_in);
    wr64(B, XAURORA_TOEPLITZ_CONTROL_ADDR_SEED_DATA, addr(seed));
    wr64(B, XAURORA_TOEPLITZ_CONTROL_ADDR_KEY_OUT_DATA, addr(key_out));
    wr32(B, XAURORA_TOEPLITZ_CONTROL_ADDR_N_OUT_DATA, n_out);

    if (!start_and_wait(B, XAURORA_TOEPLITZ_CONTROL_ADDR_AP_CTRL)) return;
    invalidate(key_out, out_bytes);
}

// -------------------------------------------------------------- ldpc_dec ----
// El kernel conserva el codigo cargado; solo se limpia la cache del codigo
// cuando se recarga (load_code != 0).
void aurora_ldpc_dec(const uint32_t *chk_ptr, const uint16_t *chk_var, uint32_t n, uint32_t m, uint32_t load_code,
                     const int8_t *llr, const uint8_t *syndrome, uint32_t max_iter, uint8_t *x_out,
                     uint32_t *status) {
    const uintptr_t B = aurora::board::LDPC_BASE;
    status[0] = status[1] = 0;
    if (n > AURORA_LDPC_MAX_N || m > AURORA_LDPC_MAX_M) return;
    if (load_code) {
        flush(chk_ptr, ((size_t)m + 1) * 4);
        const uint32_t edges = chk_ptr[m] > AURORA_LDPC_MAX_E ? AURORA_LDPC_MAX_E : chk_ptr[m];
        flush(chk_var, (size_t)edges * 2);
    }
    flush(llr, n);
    flush(syndrome, m);
    flush(x_out, n);
    flush(status, 8);

    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_CHK_PTR_DATA, addr(chk_ptr));
    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_CHK_VAR_DATA, addr(chk_var));
    wr32(B, XAURORA_LDPC_DEC_CONTROL_ADDR_N_DATA, n);
    wr32(B, XAURORA_LDPC_DEC_CONTROL_ADDR_M_DATA, m);
    wr32(B, XAURORA_LDPC_DEC_CONTROL_ADDR_LOAD_CODE_DATA, load_code);
    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_LLR_DATA, addr(llr));
    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_SYNDROME_DATA, addr(syndrome));
    wr32(B, XAURORA_LDPC_DEC_CONTROL_ADDR_MAX_ITER_DATA, max_iter);
    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_X_OUT_DATA, addr(x_out));
    wr64(B, XAURORA_LDPC_DEC_CONTROL_ADDR_STATUS_DATA, addr(status));

    if (!start_and_wait(B, XAURORA_LDPC_DEC_CONTROL_ADDR_AP_CTRL)) return;
    invalidate(x_out, n);
    invalidate(status, 8);
}
