// =============================================================================
// AURORA - Aplicacion bare-metal de prueba en la placa (Zynq UltraScale+, A53)
// =============================================================================
// 1. TRNG: captura en bruto (modo bypass) para medir la entropia en el PC.
// 2. TRNG: arranque con autodiagnostico y bits extraidos.
// 3. Kernels sync + sift en la FPGA sobre batches reales embebidos, comparados
//    bit a bit con los resultados calculados en el PC.
// 4. Kernel Toeplitz comparado con un modelo independiente.
// 5. Cadena completa (Cascade + verificacion + PA) autenticada, con las
//    semillas publicas sacadas del TRNG real.
//
// La salida va por la UART. Las lineas entre TRNG_RAW_BEGIN y TRNG_RAW_END son
// la captura del TRNG (tools/uart_capture.py la extrae).
// =============================================================================
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "aurora_auth.h"
#include "aurora_compat.h"
#include "aurora_board.h"
#include "aurora_data.h"
#include "aurora_host.h"
#include "aurora_random.h"
#include "aurora_trng.h"
#include "aurora_trng_driver.h"

#include "sleep.h"
#include "xil_io.h"
#include "xtime_l.h"

using namespace aurora;

#ifndef AURORA_TRNG_CAPTURE_WORDS
#define AURORA_TRNG_CAPTURE_WORDS 32768   // 1 Mbit
#endif

namespace {

int g_fail = 0;

void check(bool ok, const char *what) {
    std::printf("  [%s] %s\n", ok ? " OK " : "FALLO", what);
    if (!ok) ++g_fail;
}

double now_ms() {
    XTime t;
    XTime_GetTime(&t);
    return (double)t * 1000.0 / (double)COUNTS_PER_SECOND;
}

class BoardRegs : public TrngRegs {
public:
    explicit BoardRegs(uintptr_t base) : base_(base) {}
    uint32_t read(uint32_t off) override { return Xil_In32(base_ + off); }
    void write(uint32_t off, uint32_t v) override { Xil_Out32(base_ + off, v); }
    void wait_us(uint32_t us) override { usleep(us); }
private:
    uintptr_t base_;
};

Bits to_bits(const uint8_t *p, size_t n) { return Bits(p, p + n); }

// ----------------------------------------------------------------- TRNG -----
void trng_raw_capture(TrngRegs &r) {
    using namespace trng_reg;
    std::printf("\n== 1. TRNG: captura en bruto (bypass, %d palabras) ==\n", AURORA_TRNG_CAPTURE_WORDS);
    if (r.read(ID) != ID_VALUE) { check(false, "registro ID del TRNG"); return; }
    r.write(CTRL, CTRL_CLEAR);
    r.write(DECIM, 64);
    r.write(CTRL, CTRL_ENABLE | CTRL_CLEAR | CTRL_BYPASS);
    // Primero a memoria (rapido, sin huecos) y despues por la UART.
    std::vector<uint32_t> buf;
    buf.reserve(AURORA_TRNG_CAPTURE_WORDS);
    double t0 = now_ms();
    while (buf.size() < AURORA_TRNG_CAPTURE_WORDS) {
        if (!(r.read(STATUS) & ST_EMPTY)) buf.push_back(r.read(DATA));
        if (now_ms() - t0 > 10000) break;
    }
    const uint32_t st = r.read(STATUS), dropped = r.read(DROPPED), samples = r.read(SAMPLES);
    r.write(CTRL, CTRL_CLEAR);
    std::printf("  %zu palabras en %.1f ms; muestras %u, perdidas %u, alarmas RCT=%u APT=%u\n", buf.size(),
                now_ms() - t0, samples, dropped, (st & ST_RCT) ? 1u : 0u, (st & ST_APT) ? 1u : 0u);
    check(buf.size() == AURORA_TRNG_CAPTURE_WORDS, "captura completa");
    check(dropped == 0, "sin palabras perdidas (captura contigua)");
    std::printf("TRNG_RAW_BEGIN %zu\n", buf.size());
    for (uint32_t w : buf) std::printf("%08" PRIx32 "\n", w);
    std::printf("TRNG_RAW_END\n");
}

bool trng_start(TrngDriver &drv) {
    std::printf("\n== 2. TRNG: autodiagnostico y arranque ==\n");
    TrngStatus s = drv.init();
    std::printf("  estado: %s\n", trng_status_str(s));
    check(s == TrngStatus::OK, "autodiagnostico (3 fallos inyectados detectados) y arranque");
    if (s != TrngStatus::OK) return false;
    Bits seed = to_bits(bm_extractor_seed, bm_extractor_seed_len);
    Bits rnd;
    double t0 = now_ms();
    s = drv.read_random(rnd, 100000, seed);
    double dt = now_ms() - t0;
    size_t ones = 0;
    for (auto b : rnd) ones += b;
    double z = std::fabs((double)ones - rnd.size() / 2.0) / std::sqrt(rnd.size() / 4.0);
    std::printf("  100000 bits extraidos en %.1f ms (%.0f kbit/s); unos=%zu (z=%.2f)\n", dt,
                dt > 0 ? 100000.0 / dt : 0.0, ones, z);
    check(s == TrngStatus::OK && rnd.size() == 100000, "bits extraidos");
    check(z < 4.0, "salida equilibrada (test de frecuencia, |z| < 4)");
    return s == TrngStatus::OK;
}

// -------------------------------------------------------------- kernels -----
void kernels_vs_pc() {
    std::printf("\n== 3. Kernels sync + sift (FPGA) frente a los resultados del PC ==\n");
    for (uint32_t i = 0; i < bm_n_batches; ++i) {
        const BmBatch &b = bm_batches[i];
        Detections A, B;
        A.tags.assign(b.alice_tags, b.alice_tags + b.n_alice);
        A.states.assign(b.alice_states, b.alice_states + b.n_alice);
        B.tags.assign(b.bob_tags, b.bob_tags + b.n_bob);
        B.states.assign(b.bob_states, b.bob_states + b.n_bob);

        hw_kernel_timeout = false;
        double t0 = now_ms();
        LagEstimate e = estimate_lag(A, B);
        double t1 = now_ms();
        const int64_t lag = (int64_t)compat::llround(e.lag);
        SiftOutput s = sift_batch(A, B, lag, 350);
        double t2 = now_ms();

        std::printf("  %s: lag %" PRId64 " ps (PC %" PRId64 "), coincidencias %u (PC %u), cribados %zu (PC %u); "
                    "sync %.2f ms, sift %.2f ms\n",
                    b.name, lag, b.exp_lag_ps, s.n_coincidences, b.exp_coincidences, s.alice_key.size(),
                    b.exp_sifted, t1 - t0, t2 - t1);
        std::string what = std::string(b.name) + ": ";
        check(!hw_kernel_timeout, (what + "los kernels terminan").c_str());
        check(e.ok && lag == b.exp_lag_ps, (what + "lag identico al del PC").c_str());
        check(s.n_coincidences == b.exp_coincidences, (what + "coincidencias identicas").c_str());
        check(bits_to_hex(s.alice_key) == b.exp_alice_key && bits_to_hex(s.bob_key) == b.exp_bob_key,
              (what + "claves de Alice y Bob identicas bit a bit").c_str());
    }
}

void toeplitz_vs_reference() {
    std::printf("\n== 4. Kernel Toeplitz (FPGA) frente al modelo de referencia ==\n");
    Bits x = to_bits(bm_toep_in, bm_toep_n_in), seed = to_bits(bm_toep_seed, bm_toep_n_in + bm_toep_n_out - 1);
    hw_kernel_timeout = false;
    double t0 = now_ms();
    Bits h = toeplitz_hash_seeded(x, bm_toep_n_out, seed);
    double dt = now_ms() - t0;
    std::printf("  %u -> %u bits en %.2f ms\n", bm_toep_n_in, bm_toep_n_out, dt);
    check(!hw_kernel_timeout, "el kernel termina");
    check(bits_to_hex(h) == bm_toep_expected, "salida identica a la definicion de Toeplitz");
}

void full_pipeline(RandomSource &rng) {
    std::printf("\n== 5. Cadena completa autenticada con semillas del %s ==\n", rng.name());
    std::printf("  entrada: claves cribadas de todos los batches (PC), %u bits\n", bm_sifted_len);
    Bits ka = to_bits(bm_sifted_alice, bm_sifted_len), kb = to_bits(bm_sifted_bob, bm_sifted_len);
    Bits psk = to_bits(bm_psk, bm_psk_len);
    KeyPool pa(psk), pb(psk);
    AuthenticatedConfig cfg;
    double t0 = now_ms();
    AuthenticatedResult r = run_authenticated_postprocessing(ka, kb, pa, pb, cfg, rng);
    double dt = now_ms() - t0;
    std::printf("  cribados %u, corregidos %u, filtrados %" PRIu64 ", PA %u bits, autenticacion %" PRIu64
                " bits, semillas %" PRIu64 " bits, neto %zu bits, %.1f ms\n",
                r.sifted, r.corrected, r.leaked_ec, r.pa_length, r.auth_key_bits, r.random_bits_used,
                r.alice_key.size(), dt);
    if (!r.ok) std::printf("  motivo: %s\n", r.abort_reason.c_str());
    check(r.ok, "autenticacion y verificacion correctas");
    check(!r.alice_key.empty() && r.alice_key == r.bob_key, "claves finales iguales en Alice y Bob");
    check(r.alice_key.size() > bm_sifted_len / 2, "clave neta > 50% de la cribada (en el PC: ~70%)");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("\n============================================================\n");
    std::printf(" AURORA - prueba en placa (bare-metal)\n");
    std::printf("============================================================\n");

    BoardRegs regs(board::TRNG_BASE);
    trng_raw_capture(regs);

    TrngDriver drv(regs);
    const bool trng_ok = trng_start(drv);

    kernels_vs_pc();
    toeplitz_vs_reference();

    if (trng_ok) {
        TrngRandomSource src(drv, to_bits(bm_extractor_seed, bm_extractor_seed_len));
        full_pipeline(src);
    } else {
        std::printf("\n== 5. Cadena completa: OMITIDA (el TRNG no arranco) ==\n");
        ++g_fail;
    }

    std::printf("\nRESULTADO: %s (%d fallos)\n", g_fail == 0 ? "TODO OK" : "HAY FALLOS", g_fail);
    std::printf("AURORA_END\n");
    return 0;
}
