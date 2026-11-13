// =============================================================================
// AURORA - Decodificador LDPC por sindrome (kernel HLS para la PL)
// =============================================================================
// Min-sum normalizado (alfa = 0.6875) por capas, en coma fija:
//   LLR de entrada int8 (escala AURORA_LDPC_LLR_SCALE por unidad natural),
//   mensajes comprobacion->variable int8, posteriores int16 saturados.
// Por cada comprobacion c (fila de H):
//   pasada 1: t_e = L[v] - R[e]; signo total, minimo y segundo minimo de |t|
//   pasada 2: R[e] = (-1)^(s_c + signos de los demas) * alfa * min_{otros}|t|;
//             L[v] = t_e + R[e]
// Tras cada iteracion se comprueba el sindrome de la decision dura (exito =
// coincide con el de Alice).
//
// El codigo (H en CSR) se guarda en memoria interna y solo se recarga si
// load_code != 0 (las tramas consecutivas suelen usar el mismo codigo).
// Este mismo fichero es el modelo de software (PC y PS): la comparacion HW/SW
// es exacta bit a bit.
//
// Interfaz por m_axi (gmem0: entradas, gmem1: salidas) y s_axilite:
//   chk_ptr[m+1], chk_var[E] (uint16), llr[n] (int8), syndrome[m] (0/1)
//   x_out[n] (0/1), status[0] = exito, status[1] = iteraciones
// =============================================================================
#include "aurora_kernels.h"

// Normalizacion del min-sum: alfa = 0.6875 = 1/2 + 1/4 - 1/16 (solo sumas y
// desplazamientos). Medido con Q = 1.72 % y f = 1.45: FER 4 % (0.6875),
// 12 % (0.75), 14 % (0.8125), 39 % (0.875), 40 % (0.625).
#ifndef AURORA_LDPC_NORM
#define AURORA_LDPC_NORM(x) (((x) >> 1) + ((x) >> 2) - ((x) >> 4))
#endif

static inline int16_t sat16(int32_t x) {
#pragma HLS INLINE
    return (int16_t)(x > 32767 ? 32767 : (x < -32767 ? -32767 : x));
}

void aurora_ldpc_dec(const uint32_t *chk_ptr, const uint16_t *chk_var, uint32_t n, uint32_t m, uint32_t load_code,
                     const int8_t *llr, const uint8_t *syndrome, uint32_t max_iter, uint8_t *x_out,
                     uint32_t *status) {
#pragma HLS INTERFACE m_axi port=chk_ptr  offset=slave bundle=gmem0 depth=8193
#pragma HLS INTERFACE m_axi port=chk_var  offset=slave bundle=gmem0 depth=98304
#pragma HLS INTERFACE m_axi port=llr      offset=slave bundle=gmem0 depth=16384
#pragma HLS INTERFACE m_axi port=syndrome offset=slave bundle=gmem0 depth=8192
#pragma HLS INTERFACE m_axi port=x_out    offset=slave bundle=gmem1 depth=16384
#pragma HLS INTERFACE m_axi port=status   offset=slave bundle=gmem1 depth=2
#pragma HLS INTERFACE s_axilite port=n
#pragma HLS INTERFACE s_axilite port=m
#pragma HLS INTERFACE s_axilite port=load_code
#pragma HLS INTERFACE s_axilite port=max_iter
#pragma HLS INTERFACE s_axilite port=return

    // Codigo: se conserva entre llamadas.
    static uint32_t cp[AURORA_LDPC_MAX_M + 1];
    static uint16_t cv[AURORA_LDPC_MAX_E];
#pragma HLS BIND_STORAGE variable=cv type=ram_2p impl=uram
    static uint32_t n_edges = 0, loaded_m = 0;

    int8_t R[AURORA_LDPC_MAX_E];
    int16_t L[AURORA_LDPC_MAX_N];
    uint8_t syn[AURORA_LDPC_MAX_M];
    int16_t tb[AURORA_LDPC_MAX_DEG];
    uint16_t vb[AURORA_LDPC_MAX_DEG];

    status[0] = 0;
    status[1] = 0;
    if (n == 0 || m == 0 || n > AURORA_LDPC_MAX_N || m > AURORA_LDPC_MAX_M) return;

    if (load_code) {
    LOAD_PTR:
        for (uint32_t i = 0; i <= AURORA_LDPC_MAX_M; i++) {
#pragma HLS PIPELINE II=1
            if (i > m) break;
            cp[i] = chk_ptr[i];
        }
        n_edges = cp[m] > AURORA_LDPC_MAX_E ? AURORA_LDPC_MAX_E : cp[m];
        loaded_m = m;
    LOAD_VAR:
        for (uint32_t e = 0; e < AURORA_LDPC_MAX_E; e++) {
#pragma HLS PIPELINE II=1
            if (e >= n_edges) break;
            cv[e] = chk_var[e];
        }
    }
    if (loaded_m != m || cp[m] != n_edges || n_edges == 0) return;   // codigo no cargado o incoherente

LOAD_LLR:
    for (uint32_t v = 0; v < AURORA_LDPC_MAX_N; v++) {
#pragma HLS PIPELINE II=1
        if (v >= n) break;
        L[v] = llr[v];
    }
LOAD_SYN:
    for (uint32_t c = 0; c < AURORA_LDPC_MAX_M; c++) {
#pragma HLS PIPELINE II=1
        if (c >= m) break;
        syn[c] = syndrome[c] & 1;
    }
CLEAR_R:
    for (uint32_t e = 0; e < AURORA_LDPC_MAX_E; e++) {
#pragma HLS PIPELINE II=1
        if (e >= n_edges) break;
        R[e] = 0;
    }

    uint32_t ok = 0, it = 0;
ITER:
    for (uint32_t iter = 0; iter <= AURORA_LDPC_MAX_ITER; iter++) {
        // ---- comprobacion del sindrome de la decision dura ----
        uint32_t bad = 0;
    CHECK_ROWS:
        for (uint32_t c = 0; c < AURORA_LDPC_MAX_M; c++) {
            if (c >= m) break;
            const uint32_t b = cp[c], d = cp[c + 1] - cp[c];
            uint8_t p = syn[c];
        CHECK_EDGES:
            for (uint32_t k = 0; k < AURORA_LDPC_MAX_DEG; k++) {
#pragma HLS PIPELINE II=1
                if (k >= d) break;
                p ^= (L[cv[b + k]] < 0) ? 1 : 0;
            }
            bad |= p;
        }
        if (!bad) { ok = 1; it = iter; break; }
        if (iter >= max_iter) { it = iter; break; }

        // ---- una iteracion por capas ----
    ROWS:
        for (uint32_t c = 0; c < AURORA_LDPC_MAX_M; c++) {
            if (c >= m) break;
            const uint32_t b = cp[c], d = cp[c + 1] - cp[c];
            uint8_t neg = syn[c];
            int16_t min1 = 127, min2 = 127;
            uint32_t imin = 0;
        PASS1:
            for (uint32_t k = 0; k < AURORA_LDPC_MAX_DEG; k++) {
#pragma HLS PIPELINE II=1
                if (k >= d) break;
                const uint16_t v = cv[b + k];
                const int16_t t = sat16((int32_t)L[v] - (int32_t)R[b + k]);
                tb[k] = t;
                vb[k] = v;
                neg ^= (t < 0) ? 1 : 0;
                int16_t a = t < 0 ? (int16_t)-t : t;
                if (a > 127) a = 127;
                if (a < min1) { min2 = min1; min1 = a; imin = k; }
                else if (a < min2) min2 = a;
            }
            const int8_t m1 = (int8_t)AURORA_LDPC_NORM(min1), m2 = (int8_t)AURORA_LDPC_NORM(min2);
        PASS2:
            for (uint32_t k = 0; k < AURORA_LDPC_MAX_DEG; k++) {
#pragma HLS PIPELINE II=1
                if (k >= d) break;
                const int16_t t = tb[k];
                const int8_t mag = (k == imin) ? m2 : m1;
                const uint8_t s = neg ^ ((t < 0) ? 1 : 0);
                const int8_t r = s ? (int8_t)-mag : mag;
                R[b + k] = r;
                L[vb[k]] = sat16((int32_t)t + (int32_t)r);
            }
        }
    }

WRITE_X:
    for (uint32_t v = 0; v < AURORA_LDPC_MAX_N; v++) {
#pragma HLS PIPELINE II=1
        if (v >= n) break;
        x_out[v] = (L[v] < 0) ? 1 : 0;
    }
    status[0] = ok;
    status[1] = it;
}
