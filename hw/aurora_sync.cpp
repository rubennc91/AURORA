// =============================================================================
// AURORA - Kernel de sincronizacion: histograma de diferencias (streaming, II=1)
// =============================================================================
// Histograma de d = t_a - t_b para todos los pares con
//   lag_min <= d < lag_min + (n_bins << bin_shift).
//
// Estructura DATAFLOW:
//   read_alice --(x = t_a - lag_min)--+
//                                     +--> sync_core (histograma en BRAM)
//   read_bob  --(t_b, t_b + rango)----+
//
// Con x = t_a - lag_min, un par esta en rango si  t_b <= x < t_b + rango, y su
// bin es (x - t_b) >> bin_shift. El nucleo mantiene en REGISTROS una ventana
// ordenada con los tags de Alice que pueden emparejarse con el Bob actual
// (desplazamiento al descartar), asi que no hay lecturas de memoria en el
// camino dependiente entre iteraciones. El histograma usa reenvio de las dos
// ultimas escrituras (patron estandar para II=1 con lectura-modificacion-
// escritura en BRAM).
//
// Si en algun momento hay mas de SYNC_WINDOW tags de Alice dentro del rango de
// un mismo Bob, el resultado seria incompleto: se indica con complete = 0 (el
// PS puede repetir con un rango mas estrecho). Con las tasas actuales (~1,4 MHz
// en Alice y un rango grueso de ~1 us) la ventana media tiene ~1,5 tags.
// =============================================================================
#include "aurora_kernels.h"
#include "hls_compat.h"

#define SYNC_WINDOW 32

namespace {

struct BobRange {
    int64_t lo;   // t_b
    int64_t hi;   // t_b + rango
};

void read_alice(const uint64_t *tags, uint32_t n, int64_t lag_min, hls::stream<int64_t> &out) {
    if (n > AURORA_MAX_TAGS) n = AURORA_MAX_TAGS;
READ_ALICE:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (i >= n) break;
        out.write((int64_t)tags[i] - lag_min);
    }
}

void read_bob(const uint64_t *tags, uint32_t n, uint32_t bin_shift, uint32_t n_bins,
              hls::stream<BobRange> &out) {
    if (n > AURORA_MAX_TAGS) n = AURORA_MAX_TAGS;
    if (n_bins > AURORA_SYNC_MAX_BINS) n_bins = AURORA_SYNC_MAX_BINS;
    const int64_t span = (int64_t)n_bins << bin_shift;   // rango total del histograma
READ_BOB:
    for (uint32_t i = 0; i < AURORA_MAX_TAGS; i++) {
#pragma HLS PIPELINE II=1
        if (i >= n) break;
        BobRange r;
        r.lo = (int64_t)tags[i];
        r.hi = (int64_t)tags[i] + span;
        out.write(r);
    }
}

enum Phase { FETCH_BOB = 0, DROP = 1, ADMIT = 2, SCAN = 3 };

void sync_core(hls::stream<int64_t> &sa, uint32_t na, hls::stream<BobRange> &sb, uint32_t nb,
               uint32_t bin_shift, uint32_t n_bins, uint32_t *histogram, SyncResult *result) {
    if (na > AURORA_MAX_TAGS) na = AURORA_MAX_TAGS;
    if (nb > AURORA_MAX_TAGS) nb = AURORA_MAX_TAGS;
    if (n_bins > AURORA_SYNC_MAX_BINS) n_bins = AURORA_SYNC_MAX_BINS;

    uint32_t hist[AURORA_SYNC_MAX_BINS];
#pragma HLS BIND_STORAGE variable=hist type=ram_t2p impl=bram

CLEAR_HIST:
    for (uint32_t i = 0; i < AURORA_SYNC_MAX_BINS; i++) {
#pragma HLS PIPELINE II=1
        hist[i] = 0;
    }

    // Ventana de Alice en registros, ordenada (win[0] es el mas antiguo).
    int64_t win[SYNC_WINDOW];
#pragma HLS ARRAY_PARTITION variable=win complete
    uint32_t count = 0;

    uint32_t ra = 0, rb = 0;
    bool ha = false;
    int64_t a = 0;
    BobRange cur = {0, 0};
    uint32_t phase = FETCH_BOB, k = 0;
    bool done = false, overflow = false;
    uint32_t total = 0;

    // Reenvio de las dos ultimas escrituras del histograma.
    uint32_t lb1 = 0xFFFFFFFFu, lb2 = 0xFFFFFFFFu, lv1 = 0, lv2 = 0;

MAIN_LOOP:
    for (uint32_t it = 0; it < 16u * AURORA_MAX_TAGS; it++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=hist type=inter false
#pragma HLS LOOP_TRIPCOUNT min=1 max=400000
        if (done) break;
        if (!ha && ra < na) { a = sa.read(); ra++; ha = true; }

        if (phase == FETCH_BOB) {
            if (rb < nb) { cur = sb.read(); rb++; phase = DROP; }
            else done = true;
        } else if (phase == DROP) {
            // Descarta de la ventana los Alice anteriores al rango del Bob actual.
            if (count > 0 && win[0] < cur.lo) {
                for (int i = 0; i < SYNC_WINDOW - 1; i++) {
#pragma HLS UNROLL
                    win[i] = win[i + 1];
                }
                count--;
            } else {
                phase = ADMIT;
            }
        } else if (phase == ADMIT) {
            // Incorpora los Alice que caen antes del final del rango.
            if (ha && a < cur.hi) {
                if (a >= cur.lo) {
                    if (count < SYNC_WINDOW) { win[count] = a; count++; }
                    else overflow = true;
                }
                ha = false;   // demasiado antiguo para este Bob y los siguientes, o ya en la ventana
            } else {
                phase = SCAN;
                k = 0;
            }
        } else {
            // SCAN: un par por ciclo con cada Alice de la ventana.
            if (k < count) {
                uint32_t bin = (uint32_t)((uint64_t)(win[k] - cur.lo) >> bin_shift);
                uint32_t v = hist[bin];
                if (bin == lb1) v = lv1;
                else if (bin == lb2) v = lv2;
                v++;
                hist[bin] = v;
                lb2 = lb1; lv2 = lv1;
                lb1 = bin; lv1 = v;
                total++;
                k++;
            } else {
                phase = FETCH_BOB;
            }
        }
    }
    const bool all_bobs = (rb >= nb) && (phase == FETCH_BOB || done);

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

    uint32_t peak_bin = 0, peak_count = 0;
WRITE_HIST:
    for (uint32_t i = 0; i < AURORA_SYNC_MAX_BINS; i++) {
#pragma HLS PIPELINE II=1
        if (i < n_bins) {
            uint32_t c = hist[i];
            histogram[i] = c;
            if (c > peak_count) { peak_count = c; peak_bin = i; }
        }
    }
    result->peak_bin = peak_bin;
    result->peak_count = peak_count;
    result->total_pairs = total;
    result->complete = (all_bobs && !overflow) ? 1u : 0u;
}

} // namespace

void aurora_sync(
    const uint64_t *alice_tags, uint32_t n_alice,
    const uint64_t *bob_tags,   uint32_t n_bob,
    int64_t  lag_min,
    uint32_t bin_shift,
    uint32_t n_bins,
    uint32_t *histogram,
    SyncResult *result
) {
#pragma HLS INTERFACE m_axi port=alice_tags offset=slave bundle=gmem0 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=bob_tags   offset=slave bundle=gmem1 depth=65536 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=histogram  offset=slave bundle=gmem2 depth=1024
#pragma HLS INTERFACE s_axilite port=n_alice
#pragma HLS INTERFACE s_axilite port=n_bob
#pragma HLS INTERFACE s_axilite port=lag_min
#pragma HLS INTERFACE s_axilite port=bin_shift
#pragma HLS INTERFACE s_axilite port=n_bins
#pragma HLS INTERFACE s_axilite port=result
#pragma HLS INTERFACE s_axilite port=return
#pragma HLS DATAFLOW

    hls::stream<int64_t> sa("alice_stream");
    hls::stream<BobRange> sb("bob_stream");
#pragma HLS STREAM variable=sa depth=64
#pragma HLS STREAM variable=sb depth=64

    read_alice(alice_tags, n_alice, lag_min, sa);
    read_bob(bob_tags, n_bob, bin_shift, n_bins, sb);
    sync_core(sa, n_alice, sb, n_bob, bin_shift, n_bins, histogram, result);
}
