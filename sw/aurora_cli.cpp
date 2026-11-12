// =============================================================================
// AURORA - Herramienta de linea de comandos (PC / PS)
//
//   aurora_cli batch    <prefijo> [half_window_ps]
//       Procesa un batch y emite una linea JSON (usada por los tests Python).
//   aurora_cli pipeline <dir_batches> <primero> <ultimo> [half_window_ps]
//       Cadena completa: sync -> sifting -> Cascade -> verificacion -> PA,
//       con el canal clasico autenticado (Wegman-Carter).
//   aurora_cli polyhash <clave_hex32> [mensaje_hex]
//       Hash polinomico GF(2^128) usado por la autenticacion.
// =============================================================================
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "aurora_auth.h"
#include "aurora_host.h"

using namespace aurora;

static int cmd_batch(const std::string &prefix, uint32_t w) {
    Detections a, b;
    std::string err;
    if (!load_batch(prefix, a, b, &err) || !validate_detections(a, &err) || !validate_detections(b, &err)) {
        std::printf("{\"ok\":false,\"error\":\"%s\"}\n", err.c_str());
        return 2;
    }
    LagEstimate lag = estimate_lag(a, b);
    int64_t lag_ps = (int64_t)std::llround(lag.lag);
    SiftOutput s = sift_batch(a, b, lag_ps, w);
    uint32_t errors = 0;
    for (size_t k = 0; k < s.alice_key.size(); ++k) errors += s.alice_key[k] != s.bob_key[k];
    std::printf("{\"ok\":%s,\"n_alice\":%zu,\"n_bob\":%zu,\"coarse_lag\":%" PRId64 ",\"lag\":%.3f,"
                "\"lag_ps\":%" PRId64 ",\"sigma\":%.3f,\"signal\":%.1f,\"snr\":%.2f,\"half_window\":%u,"
                "\"coincidences\":%u,\"sifted\":%zu,\"errors\":%u,\"chunks\":%u,"
                "\"alice_key\":\"%s\",\"bob_key\":\"%s\"}\n",
                lag.ok ? "true" : "false", a.size(), b.size(), lag.coarse_lag, lag.lag, lag_ps, lag.sigma,
                lag.signal, lag.snr, w, s.n_coincidences, s.alice_key.size(), errors, s.n_chunks,
                bits_to_hex(s.alice_key).c_str(), bits_to_hex(s.bob_key).c_str());
    return lag.ok ? 0 : 1;
}

static int cmd_pipeline(const std::string &dir, int first, int last, uint32_t w) {
    Bits ka, kb;
    uint64_t coinc = 0;
    int used = 0;
    std::printf("batch | lag(ps) | sigma | snr   | coinc | sifted | QBER(%%)\n");
    for (int i = first; i <= last; ++i) {
        Detections a, b;
        std::string prefix = dir + "/batch" + std::to_string(i);
        if (!load_batch(prefix, a, b)) continue;
        LagEstimate lag = estimate_lag(a, b);
        // Cada batch tiene su propio desfase (~77 ps rms en las capturas reales):
        // si la sincronizacion falla, el batch se descarta (no se reutiliza un lag).
        if (!lag.ok) { std::printf("%5d | sync FALLIDO (snr %.1f), se descarta\n", i, lag.snr); continue; }
        SiftOutput s = sift_batch(a, b, (int64_t)std::llround(lag.lag), w);
        uint32_t e = 0;
        for (size_t k = 0; k < s.alice_key.size(); ++k) e += s.alice_key[k] != s.bob_key[k];
        std::printf("%5d | %7.1f | %5.1f | %5.1f | %5u | %6zu | %5.2f%s\n", i, lag.lag, lag.sigma, lag.snr,
                    s.n_coincidences, s.alice_key.size(), s.alice_key.empty() ? 0.0 : 100.0 * e / s.alice_key.size(), "");
        ka.insert(ka.end(), s.alice_key.begin(), s.alice_key.end());
        kb.insert(kb.end(), s.bob_key.begin(), s.bob_key.end());
        coinc += s.n_coincidences;
        ++used;
    }
    if (ka.empty()) { std::printf("Sin clave.\n"); return 1; }

    uint32_t true_err = 0;
    for (size_t k = 0; k < ka.size(); ++k) true_err += ka[k] != kb[k];

    // Clave precompartida SIMULADA para autenticar el canal clasico. En el
    // sistema real se instala de forma segura en ambos equipos y luego se
    // repone con clave QKD.
    SplitMix64 psk_rng(0x9E5C7A11D00DULL);
    Bits psk(4096);
    for (auto &v : psk) v = (uint8_t)(psk_rng.next() & 1);
    KeyPool pool_a(psk), pool_b(psk);
    AuthenticatedConfig cfg;
    AuthenticatedResult r = run_authenticated_postprocessing(ka, kb, pool_a, pool_b, cfg);
    double qber = r.sifted ? (double)r.corrected / r.sifted : 0.0;

    std::printf("\n=== RESUMEN (%d batches) ===\n", used);
    std::printf("coincidencias        : %" PRIu64 "\n", coinc);
    std::printf("clave cribada        : %zu bits\n", ka.size());
    std::printf("errores reales       : %u (QBER %.3f%%)\n", true_err, 100.0 * true_err / ka.size());
    std::printf("Cascade              : %u corregidos, %" PRIu64 " bits filtrados (f=%.3f)\n", r.corrected,
                r.leaked_ec, qber > 0 ? r.leaked_ec / (ka.size() * binary_entropy(qber)) : 0.0);
    std::printf("autenticacion        : %s\n", r.ok ? "OK (4 fases, ambos sentidos)" : r.abort_reason.c_str());
    std::printf("clave final (PA)     : %u bits (%.1f%% de la cribada)\n", r.pa_length,
                100.0 * r.pa_length / ka.size());
    std::printf("clave de autenticacion: %" PRIu64 " bits (repuestos en la reserva)\n", r.auth_key_bits);
    std::printf("clave neta entregada : %zu bits\n", r.alice_key.size());
    std::printf("claves finales iguales: %s\n", (r.ok && !r.alice_key.empty() && r.alice_key == r.bob_key) ? "SI" : "NO");
    return (r.ok && !r.alice_key.empty() && r.alice_key == r.bob_key) ? 0 : 1;
}

// Hash polinomico GF(2^128) (para contrastar con implementaciones independientes).
static int cmd_polyhash(const std::string &khex, const std::string &mhex) {
    U128 k;
    if (!u128_from_hex(khex, k) || mhex.size() % 2) return 2;
    std::vector<uint8_t> m(mhex.size() / 2);
    for (size_t i = 0; i < m.size(); ++i) m[i] = (uint8_t)std::stoul(mhex.substr(2 * i, 2), nullptr, 16);
    std::printf("%s\n", u128_hex(poly_hash(k, m.data(), m.size())).c_str());
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && std::string(argv[1]) == "batch") {
        uint32_t w = argc >= 4 ? (uint32_t)std::atoi(argv[3]) : 350;
        return cmd_batch(argv[2], w);
    }
    if (argc >= 5 && std::string(argv[1]) == "pipeline") {
        uint32_t w = argc >= 6 ? (uint32_t)std::atoi(argv[5]) : 350;
        return cmd_pipeline(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), w);
    }
    if (argc >= 3 && std::string(argv[1]) == "polyhash") {
        return cmd_polyhash(argv[2], argc >= 4 ? argv[3] : "");
    }
    std::fprintf(stderr,
                 "uso:\n  %s batch <prefijo> [half_window_ps]\n"
                 "  %s pipeline <dir> <primero> <ultimo> [half_window_ps]\n"
                 "  %s polyhash <clave_hex32> [mensaje_hex]\n",
                 argv[0], argv[0], argv[0]);
    return 64;
}
