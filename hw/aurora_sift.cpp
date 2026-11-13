// =============================================================================
// AURORA - Kernel de coincidencias + sifting BB84 (streaming, II=1)
// =============================================================================
// Estructura DATAFLOW:
//   read_alice --(t_a - lag, estado)--+
//                                     +--> sift_core --(palabras)--> write_keys
//   read_bob  --(t_b - w, t_b + w)----+
//
// Los lectores recorren la DDR de forma secuencial (rafagas). El nucleo es una
// mezcla de dos flujos ordenados: en cada ciclo compara las cabezas (en
// registros) y consume Alice, Bob o ambos. No hay lecturas de memoria en el
// camino dependiente entre iteraciones (la version anterior leia por m_axi una
// posicion que dependia de la iteracion previa: II~14 y ~17 ns).
//
// Semantica identica a la version anterior (y a los modelos de referencia):
// para cada Bob, el primer Alice no usado con |t_a - t_b - lag| <= w.
// =============================================================================
#include "aurora_kernels.h"
#include "hls_compat.h"

namespace {

struct AliceEv {
    int64_t t;      // t_a - lag
    uint8_t s;
};
struct BobEv {
    int64_t lo;     // t_b - w
    int64_t hi;     // t_b + w
    uint8_t s;
};
struct KeyWord {
    uint64_t a, b;
    uint32_t coinc, sifted, next_alice;   // validos solo en la ultima
    bool last;
};

void read_alice(const uint64_t *tags, const uint8_t *states, uint32_t n, int64_t lag,
                hls::stream<AliceEv> &out) {
    if (n > AURORA_MAX_TAGS) n = AURORA_MAX_TAGS;
READ_ALICE:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (i >= n) break;
        AliceEv e;
        e.t = (int64_t)tags[i] - lag;
        e.s = states[i];
        out.write(e);
    }
}

void read_bob(const uint64_t *tags, const uint8_t *states, uint32_t n, uint32_t half_window,
              hls::stream<BobEv> &out) {
    if (n > AURORA_MAX_TAGS) n = AURORA_MAX_TAGS;
READ_BOB:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (i >= n) break;
        BobEv e;
        e.lo = (int64_t)tags[i] - (int64_t)half_window;
        e.hi = (int64_t)tags[i] + (int64_t)half_window;
        e.s = states[i];
        out.write(e);
    }
}

void sift_core(hls::stream<AliceEv> &sa, uint32_t na, hls::stream<BobEv> &sb, uint32_t nb,
               hls::stream<KeyWord> &out) {
    if (na > AURORA_MAX_TAGS) na = AURORA_MAX_TAGS;
    if (nb > AURORA_MAX_TAGS) nb = AURORA_MAX_TAGS;

    uint32_t ra = 0, rb = 0;          // elementos leidos de cada stream
    bool ha = false, hb = false;      // hay cabeza valida
    AliceEv a = {0, 0};
    BobEv b = {0, 0, 0};
    uint32_t consumed_a = 0, n_coinc = 0, n_sifted = 0;
    uint64_t wa = 0, wb = 0;

    // Cada iteracion consume al menos un elemento: <= na + nb + 1 iteraciones.
MERGE:
    for (uint32_t it = 0; it < 2u * AURORA_MAX_TAGS + 2u; it++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=131074
        if (!ha && ra < na) { a = sa.read(); ra++; ha = true; }
        if (!hb && rb < nb) { b = sb.read(); rb++; hb = true; }
        if (!hb) break;                               // no quedan Bobs

        bool emit = false;
        if (!ha) {
            hb = false;                               // Alice agotada: Bob sin pareja
        } else if (a.t < b.lo) {
            ha = false;                               // Alice demasiado pronto
            consumed_a++;
        } else if (a.t <= b.hi) {                     // coincidencia
            ha = false;
            hb = false;
            consumed_a++;
            n_coinc++;
            if (((a.s >> 1) & 1) == ((b.s >> 1) & 1)) {
                uint32_t pos = n_sifted & 63;
                wa |= (uint64_t)(a.s & 1) << pos;
                wb |= (uint64_t)(b.s & 1) << pos;
                n_sifted++;
                emit = (pos == 63);
            }
        } else {
            hb = false;                               // Bob sin pareja
        }
        if (emit) {
            KeyWord w = {wa, wb, 0, 0, 0, false};
            out.write(w);
            wa = 0;
            wb = 0;
        }
    }

    // Ultima palabra (parcial o vacia) con los contadores.
    KeyWord last = {wa, wb, n_coinc, n_sifted, consumed_a, true};
    out.write(last);

    // Vaciar lo que quede en los streams (si no, los lectores no terminarian).
DRAIN_ALICE:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (ra >= na) break;
        sa.read();
        ra++;
    }
DRAIN_BOB:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (rb >= nb) break;
        sb.read();
        rb++;
    }
}

void write_keys(hls::stream<KeyWord> &in, uint64_t *alice_key, uint64_t *bob_key, SiftResult *result) {
    uint32_t idx = 0;
WRITE_KEYS:
    for (uint32_t i = 0; i <= AURORA_MAX_KEY_WORDS; i++) {
#pragma HLS PIPELINE II=1
        KeyWord w = in.read();
        if (w.last) {
            if ((w.sifted & 63) != 0) {
                alice_key[idx] = w.a;
                bob_key[idx] = w.b;
            }
            result->n_coincidences = w.coinc;
            result->n_sifted = w.sifted;
            result->next_alice_idx = w.next_alice;
            break;
        }
        alice_key[idx] = w.a;
        bob_key[idx] = w.b;
        idx++;
    }
}

} // namespace

void aurora_sift(
    const uint64_t *alice_tags, const uint8_t *alice_states, uint32_t n_alice,
    const uint64_t *bob_tags,   const uint8_t *bob_states,   uint32_t n_bob,
    int64_t  lag,
    uint32_t half_window,
    uint64_t *alice_key,
    uint64_t *bob_key,
    SiftResult *result
) {
#pragma HLS INTERFACE m_axi port=alice_tags   offset=slave bundle=gmem0 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=alice_states offset=slave bundle=gmem1 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=bob_tags     offset=slave bundle=gmem2 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=bob_states   offset=slave bundle=gmem3 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=alice_key    offset=slave bundle=gmem4 depth=1025
#pragma HLS INTERFACE m_axi port=bob_key      offset=slave bundle=gmem5 depth=1025
#pragma HLS INTERFACE s_axilite port=n_alice
#pragma HLS INTERFACE s_axilite port=n_bob
#pragma HLS INTERFACE s_axilite port=lag
#pragma HLS INTERFACE s_axilite port=half_window
#pragma HLS INTERFACE s_axilite port=result
#pragma HLS INTERFACE s_axilite port=return
#pragma HLS DATAFLOW

    hls::stream<AliceEv> sa("alice_stream");
    hls::stream<BobEv> sb("bob_stream");
    hls::stream<KeyWord> sk("key_stream");
#pragma HLS STREAM variable=sa depth=64
#pragma HLS STREAM variable=sb depth=64
#pragma HLS STREAM variable=sk depth=16

    read_alice(alice_tags, alice_states, n_alice, lag, sa);
    read_bob(bob_tags, bob_states, n_bob, half_window, sb);
    sift_core(sa, n_alice, sb, n_bob, sk);
    write_keys(sk, alice_key, bob_key, result);
}
