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
//   aurora_cli trng-analyze <captura> [--h H] [--extract salida.txt]
//       Tests de salud, min-entropia (SP 800-90B) y extraccion Toeplitz.
//   aurora_cli trng-health <raw.txt> <rct_cutoff> <apt_cutoff>
//       Modelo de referencia de los tests de salud del RTL.
//   aurora_cli gen-random <n_bits | extractor> <salida.txt> [--h H]
//       Instalacion: bits del generador del sistema operativo (semilla publica
//       del extractor del TRNG o clave precompartida de autenticacion).
// =============================================================================
#include <cinttypes>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "aurora_auth.h"
#include "aurora_host.h"
#include "aurora_random.h"
#include "aurora_trng.h"
#include <fstream>

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

static int cmd_pipeline(const std::string &dir, int first, int last, uint32_t w, bool ldpc) {
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
    if (ldpc) cfg.reconciliation = Reconciliation::LDPC;
    // En el PC no hay TRNG: fuente SIMULADA. En la placa: TrngRandomSource.
    SimulatedRandomSource alice_rng(0x5EED0001ULL);
    const auto t0 = std::chrono::steady_clock::now();
    AuthenticatedResult r = run_authenticated_postprocessing(ka, kb, pool_a, pool_b, cfg, alice_rng);
    const double t_post = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double qber = r.reconciled ? (double)r.corrected / r.reconciled : 0.0;

    std::printf("\n=== RESUMEN (%d batches) ===\n", used);
    std::printf("coincidencias        : %" PRIu64 "\n", coinc);
    std::printf("clave cribada        : %zu bits\n", ka.size());
    std::printf("errores reales       : %u (QBER %.3f%%)\n", true_err, 100.0 * true_err / ka.size());
    std::printf("%-21s: %u corregidos, %" PRIu64 " bits filtrados (f=%.3f)", ldpc ? "LDPC" : "Cascade", r.corrected,
                r.leaked_ec, qber > 0 ? r.leaked_ec / (r.reconciled * binary_entropy(qber)) : 0.0);
    if (ldpc) std::printf(", %u tramas, %u descartadas, %u bits conservados", r.ec_frames, r.ec_failed, r.reconciled);
    std::printf("\n");
    std::printf("autenticacion        : %s\n", r.ok ? "OK (4 fases, ambos sentidos)" : r.abort_reason.c_str());
    std::printf("semillas publicas    : %" PRIu64 " bits de la fuente %s\n", r.random_bits_used, alice_rng.name());
    std::printf("clave final (PA)     : %u bits (%.1f%% de la cribada)\n", r.pa_length,
                100.0 * r.pa_length / ka.size());
    std::printf("clave de autenticacion: %" PRIu64 " bits (repuestos en la reserva)\n", r.auth_key_bits);
    std::printf("clave neta entregada : %zu bits\n", r.alice_key.size());
    std::printf("tiempo post-procesado: %.3f s (PC)\n", t_post);
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

// Bits brutos en formato del testbench: "<indice> <bit>" por linea. Se toma la
// ultima secuencia que empieza en el indice 0 (tras el ultimo clear).
static bool load_raw_indexed(const std::string &path, Bits &out) {
    std::ifstream f(path);
    if (!f) return false;
    out.clear();
    uint64_t idx;
    int bit;
    while (f >> idx >> bit) {
        if (idx == 0) out.clear();
        if (idx != out.size()) return false;
        out.push_back((uint8_t)(bit & 1));
    }
    return true;
}

static int cmd_trng_health(const std::string &path, uint32_t rct, uint32_t apt) {
    Bits raw;
    if (!load_raw_indexed(path, raw)) { std::fprintf(stderr, "no se puede leer %s\n", path.c_str()); return 2; }
    HealthConfig hc;
    hc.rct_cutoff = rct;
    hc.apt_cutoff = apt;
    HealthResult r = run_health_tests(raw, hc);
    std::printf("{\"samples\":%zu,\"rct_idx\":%lld,\"apt_idx\":%lld,\"startup_ok\":%s}\n", raw.size(),
                (long long)r.rct_fail_idx, (long long)r.apt_fail_idx, r.startup_ok ? "true" : "false");
    return 0;
}

// Analisis de una captura (palabras hex de 32 bits): salud, entropia y extraccion.
static int cmd_trng_analyze(int argc, char **argv) {
    std::string in, out_path;
    double h_assumed = 0.5;
    uint64_t seed = 0x7A3C5E9F1B2D4C6EULL;   // solo analisis: semilla del extractor generada con un PRNG
    std::string seed_file;                   // produccion: semilla publica generada en la instalacion
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--extract" && i + 1 < argc) out_path = argv[++i];
        else if (a == "--h" && i + 1 < argc) h_assumed = std::atof(argv[++i]);
        else if (a == "--seed" && i + 1 < argc) seed = std::strtoull(argv[++i], nullptr, 0);
        else if (a == "--seed-file" && i + 1 < argc) seed_file = argv[++i];
        else if (a == "--raw-indexed") in = std::string("@") + (i + 1 < argc ? argv[++i] : "");
        else in = a;
    }
    Bits raw;
    std::string err;
    if (!in.empty() && in[0] == '@') {
        if (!load_raw_indexed(in.substr(1), raw)) { std::fprintf(stderr, "no se puede leer %s\n", in.c_str() + 1); return 2; }
    } else if (!load_trng_words(in, raw, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    HealthConfig hc;
    hc.rct_cutoff = rct_cutoff(h_assumed);
    hc.apt_cutoff = apt_cutoff(h_assumed);
    HealthResult hr = run_health_tests(raw, hc);
    EntropyEstimate e = estimate_min_entropy(raw);
    // Se usa la entropia asumida solo si la estimacion no la contradice.
    double h_use = std::min(h_assumed, e.min);
    Bits ext_seed;
    if (!seed_file.empty()) {
        if (!load_bits_hex(seed_file, ext_seed, &err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    } else {
        SimulatedRandomSource(seed).random_bits(extractor_seed_length(h_use), ext_seed);
    }
    Bits ext = extract_random(raw, h_use, ext_seed);
    if (!raw.empty() && ext.empty() && ext_seed.size() < extractor_seed_length(h_use)) {
        std::fprintf(stderr, "semilla del extractor demasiado corta: %zu bits, se necesitan %zu\n", ext_seed.size(),
                     extractor_seed_length(h_use));
        return 2;
    }
    if (!out_path.empty()) {
        std::ofstream f(out_path);
        for (size_t k = 0; k + 32 <= ext.size(); k += 32) {
            uint32_t w = 0;
            for (int b = 0; b < 32; ++b) w = (w << 1) | ext[k + (size_t)b];
            char buf[16];
            std::snprintf(buf, sizeof buf, "%08x\n", w);
            f << buf;
        }
    }
    std::printf("{\"bits\":%zu,\"h_assumed\":%.3f,\"mcv\":%.4f,\"markov\":%.4f,\"h_min\":%.4f,"
                "\"rct_cutoff\":%u,\"apt_cutoff\":%u,\"rct_idx\":%lld,\"apt_idx\":%lld,"
                "\"h_used\":%.4f,\"extracted\":%zu}\n",
                raw.size(), h_assumed, e.mcv, e.markov, e.min, hc.rct_cutoff, hc.apt_cutoff,
                (long long)hr.rct_fail_idx, (long long)hr.apt_fail_idx, h_use, ext.size());
    return hr.healthy() ? 0 : 1;
}

// Instalacion: bits aleatorios del sistema operativo (semilla publica del
// extractor del TRNG, clave precompartida de autenticacion).
static int cmd_gen_random(const std::string &what, const std::string &out_path, double h) {
    size_t n = (what == "extractor") ? extractor_seed_length(h) : (size_t)std::strtoull(what.c_str(), nullptr, 10);
    if (n == 0) { std::fprintf(stderr, "numero de bits invalido\n"); return 2; }
    Bits b;
    std::string err;
    if (!os_random_bits(n, b, &err) || !save_bits_hex(out_path, b, &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    std::printf("%zu bits aleatorios (sistema operativo) guardados en %s\n", n, out_path.c_str());
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && std::string(argv[1]) == "batch") {
        uint32_t w = argc >= 4 ? (uint32_t)std::atoi(argv[3]) : 350;
        return cmd_batch(argv[2], w);
    }
    if (argc >= 5 && std::string(argv[1]) == "pipeline") {
        uint32_t w = 350;
        bool ldpc = false;
        for (int i = 5; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--ec" && i + 1 < argc) ldpc = std::string(argv[++i]) == "ldpc";
            else w = (uint32_t)std::atoi(argv[i]);
        }
        return cmd_pipeline(argv[2], std::atoi(argv[3]), std::atoi(argv[4]), w, ldpc);
    }
    if (argc >= 5 && std::string(argv[1]) == "trng-health") {
        return cmd_trng_health(argv[2], (uint32_t)std::atoi(argv[3]), (uint32_t)std::atoi(argv[4]));
    }
    if (argc >= 3 && std::string(argv[1]) == "trng-analyze") return cmd_trng_analyze(argc, argv);
    if (argc >= 4 && std::string(argv[1]) == "gen-random") {
        double h = 0.5;
        for (int i = 4; i + 1 < argc; ++i) if (std::string(argv[i]) == "--h") h = std::atof(argv[i + 1]);
        return cmd_gen_random(argv[2], argv[3], h);
    }
    if (argc >= 3 && std::string(argv[1]) == "polyhash") {
        return cmd_polyhash(argv[2], argc >= 4 ? argv[3] : "");
    }
    std::fprintf(stderr,
                 "uso:\n  %s batch <prefijo> [half_window_ps]\n"
                 "  %s pipeline <dir> <primero> <ultimo> [half_window_ps] [--ec cascade|ldpc]\n"
                 "  %s polyhash <clave_hex32> [mensaje_hex]\n"
                 "  %s trng-analyze <captura_hex> [--h H] [--seed S | --seed-file F] [--extract salida.txt]\n"
                 "  %s trng-analyze --raw-indexed <raw.txt> [--h H] [--extract salida.txt]\n"
                 "  %s trng-health <raw.txt> <rct_cutoff> <apt_cutoff>\n"
                 "  %s gen-random <n_bits | extractor> <salida.txt> [--h H]\n",
                 argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
    return 64;
}
