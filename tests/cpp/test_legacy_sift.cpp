// =============================================================================
// Tests sobre el kernel ANTERIOR de hls_sift/ (SYNC + SIFT).
// Los KNOWN_BUG documentan fallos reales: deben FALLAR (XFAIL). Si alguno pasa
// (XPASS) es que el comportamiento ha cambiado y hay que revisar el test.
// Necesita los batches reales (variable AURORA_BATCHES, por defecto ./batches).
// =============================================================================
#include <cmath>
#include <string>
#include <vector>

#include "../../hls_sift/srcs/quantum_kernels.h"
#include "legacy_common.h"
#include "test_framework.h"

using namespace aurora;

struct LegacyRun {
    int16_t lag = 0;
    uint32_t sync_count = 0;
    uint32_t sifted = 0;
    std::vector<uint8_t> key;
};

// Reproduce exactamente el uso de hls_sift/srcs/main.cpp.
static LegacyRun run_legacy(const Detections &a, const Detections &b, uint32_t na, uint32_t nb) {
    LegacyRun r;
    std::vector<uint8_t> key(BATCH_LIMIT);
    CorrResult res{};
    uint32_t cnt = 0;
    compute_correlation_hls(a.tags.data(), a.states.data(), b.tags.data(), b.states.data(), na, nb, 350, 0,
                            MODE_SYNC, key.data(), &cnt, res);
    r.lag = res.best_lag;
    r.sync_count = res.max_count;
    compute_correlation_hls(a.tags.data(), a.states.data(), b.tags.data(), b.states.data(), na, nb, 350, r.lag,
                            MODE_SIFT, key.data(), &cnt, res);
    r.sifted = cnt;
    r.key.assign(key.begin(), key.begin() + cnt);
    return r;
}

static uint32_t clamp4096(size_t n) { return (uint32_t)std::min<size_t>(n, BATCH_LIMIT); }

KNOWN_BUG(legacy_sift_covers_full_batch) {
    // BUG: truncar Alice y Bob a 4096 eventos cubre ~3.3 ms de Alice y ~5.9 ms
    // de Bob (intervalos distintos) de un batch de 25 ms => se pierde ~90% de la clave.
    for (int i = 1; i <= 5; ++i) {
        Detections a, b;
        if (!load_real_batch(i, a, b)) { CHECK_MSG(false, "no se encuentra batch" + std::to_string(i)); return; }
        LegacyRun l = run_legacy(a, b, clamp4096(a.size()), clamp4096(b.size()));
        LagEstimate e = estimate_lag(a, b);
        SiftOutput s = sift_batch(a, b, (int64_t)std::llround(e.lag), 350);
        CHECK_MSG(l.sifted >= 0.9 * s.alice_key.size(),
                  "batch" + std::to_string(i) + ": legacy " + std::to_string(l.sifted) + " bits vs " +
                      std::to_string(s.alice_key.size()) + " disponibles");
    }
}

KNOWN_BUG(legacy_sync_finds_coincidences) {
    // BUG: en MODE_SYNC 'a_ptr' solo avanza al encontrar coincidencia y la
    // busqueda mira 64 posiciones => se queda atascado y cuenta casi 0 pares.
    for (int i = 1; i <= 3; ++i) {
        Detections a, b;
        if (!load_real_batch(i, a, b)) { CHECK(false); return; }
        uint32_t na = clamp4096(a.size());
        uint32_t nb = 0;   // Bobs dentro del intervalo de los 4096 Alice
        while (nb < b.size() && nb < BATCH_LIMIT && b.tags[nb] < a.tags[na - 1]) ++nb;
        LegacyRun l = run_legacy(a, b, na, nb);
        Detections a2{std::vector<uint64_t>(a.tags.begin(), a.tags.begin() + na),
                      std::vector<uint8_t>(a.states.begin(), a.states.begin() + na)};
        Detections b2{std::vector<uint64_t>(b.tags.begin(), b.tags.begin() + nb),
                      std::vector<uint8_t>(b.states.begin(), b.states.begin() + nb)};
        SiftOutput s = sift_batch(a2, b2, 0, 350);
        CHECK_MSG(l.sync_count >= 0.5 * s.n_coincidences,
                  "batch" + std::to_string(i) + ": SYNC cuenta " + std::to_string(l.sync_count) + " pares, hay " +
                      std::to_string(s.n_coincidences));
    }
}

KNOWN_BUG(legacy_sync_lag_is_accurate) {
    // Consecuencia del anterior: el lag elegido es ruido.
    for (int i = 1; i <= 5; ++i) {
        Detections a, b;
        if (!load_real_batch(i, a, b)) { CHECK(false); return; }
        LegacyRun l = run_legacy(a, b, clamp4096(a.size()), clamp4096(b.size()));
        LagEstimate e = estimate_lag(a, b);
        CHECK_MSG(std::fabs(l.lag - e.lag) <= 60.0,
                  "batch" + std::to_string(i) + ": lag legacy " + std::to_string(l.lag) + " ps vs " +
                      std::to_string(e.lag) + " ps (sigma pico " + std::to_string(e.sigma) + ")");
    }
}

KNOWN_BUG(legacy_sift_no_unsigned_underflow) {
    // BUG: 'target - window' con uint64_t da la vuelta cerca de t=0.
    uint64_t at[3] = {100, 5000, 9000}, bt[3] = {150, 5050, 9050};
    uint8_t am[3] = {0, 1, 2}, bm[3] = {0, 1, 2}, key[8];
    uint32_t cnt = 0;
    CorrResult r{};
    compute_correlation_hls(at, am, bt, bm, 3, 3, 350, -200, MODE_SIFT, key, &cnt, r);
    CHECK_MSG(cnt == 3, "sifted " + std::to_string(cnt) + " de 3");
}

TEST(legacy_sift_equals_new_sift_with_same_lag_and_span) {
    // Linea base: con el MISMO lag y entradas alineadas en tiempo, el SIFT
    // anterior y el nuevo deben producir la misma clave de Alice.
    for (int i = 1; i <= 5; ++i) {
        Detections a, b;
        if (!load_real_batch(i, a, b)) { CHECK(false); return; }
        uint32_t na = clamp4096(a.size()), nb = 0;
        while (nb < b.size() && nb < BATCH_LIMIT && b.tags[nb] + 2000 < a.tags[na - 1]) ++nb;
        std::vector<uint8_t> key(BATCH_LIMIT);
        uint32_t cnt = 0;
        CorrResult r{};
        compute_correlation_hls(a.tags.data(), a.states.data(), b.tags.data(), b.states.data(), na, nb, 350, 20,
                                MODE_SIFT, key.data(), &cnt, r);
        Detections a2{std::vector<uint64_t>(a.tags.begin(), a.tags.begin() + na),
                      std::vector<uint8_t>(a.states.begin(), a.states.begin() + na)};
        Detections b2{std::vector<uint64_t>(b.tags.begin(), b.tags.begin() + nb),
                      std::vector<uint8_t>(b.states.begin(), b.states.begin() + nb)};
        SiftOutput s = sift_batch(a2, b2, 20, 350);
        CHECK_MSG(std::vector<uint8_t>(key.begin(), key.begin() + cnt) == s.alice_key,
                  "batch" + std::to_string(i) + ": " + std::to_string(cnt) + " vs " + std::to_string(s.alice_key.size()));
    }
}

int main() { return tf::run_all("LEGACY hls_sift (fallos documentados)"); }
