// =============================================================================
// AURORA - Kernel de sincronizacion (histograma de diferencias temporales)
// =============================================================================
#include "aurora_kernels.h"

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

    if (n_bins > AURORA_SYNC_MAX_BINS) n_bins = AURORA_SYNC_MAX_BINS;
    if (n_alice > AURORA_MAX_TAGS) n_alice = AURORA_MAX_TAGS;
    if (n_bob > AURORA_MAX_TAGS) n_bob = AURORA_MAX_TAGS;

    uint32_t hist[AURORA_SYNC_MAX_BINS];
#pragma HLS BIND_STORAGE variable=hist type=ram_t2p impl=bram

CLEAR_HIST:
    for (uint32_t i = 0; i < AURORA_SYNC_MAX_BINS; i++) {
#pragma HLS PIPELINE II=1
        hist[i] = 0;
    }

    const int64_t lag_max = lag_min + ((int64_t)n_bins << bin_shift);

    // Maquina de estados aplanada (un unico bucle => pipeline II=1):
    //   ADVANCE: mueve 'lo' hasta el primer tag de Alice con d >= lag_min
    //   SCAN   : recorre desde 'lo' los tags con d < lag_max y los acumula
    uint32_t b = 0, lo = 0, k = 0;
    bool scanning = false;
    uint64_t tb = (n_bob > 0) ? bob_tags[0] : 0;
    uint32_t total = 0;

    // Acumulador con reenvio para evitar dependencias RAW sobre 'hist'.
    uint32_t prev_bin = 0xFFFFFFFFu;
    uint32_t acc = 0;

MAIN_LOOP:
    for (uint32_t it = 0; it < 8u * AURORA_MAX_TAGS; it++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=hist type=inter false
#pragma HLS LOOP_TRIPCOUNT min=1 max=200000
        if (b >= n_bob) break;

        if (!scanning) {
            if (lo < n_alice && (int64_t)(alice_tags[lo] - tb) < lag_min) {
                lo++;
            } else {
                k = lo;
                scanning = true;
            }
        } else {
            int64_t d = (k < n_alice) ? (int64_t)(alice_tags[k] - tb) : lag_max;
            if (d < lag_max) {
                uint32_t bin = (uint32_t)((uint64_t)(d - lag_min) >> bin_shift);
                if (bin == prev_bin) {
                    acc++;
                } else {
                    if (prev_bin != 0xFFFFFFFFu) hist[prev_bin] = acc;
                    acc = hist[bin] + 1;
                    prev_bin = bin;
                }
                total++;
                k++;
            } else {
                b++;
                scanning = false;
                if (b < n_bob) tb = bob_tags[b];
            }
        }
    }
    if (prev_bin != 0xFFFFFFFFu) hist[prev_bin] = acc;

    // Copia del histograma + busqueda del pico.
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
    result->complete = (b >= n_bob) ? 1u : 0u;
}
