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
// 6. Reconciliacion LDPC: decodificador en la PL frente al mismo algoritmo en
//    el PS (comparacion bit a bit y de tiempos), frente a sum-product en coma
//    flotante, y cadena autenticada completa con LDPC en la PL.
//
// Modo calefactor (buzon, "HEAT"): calienta la PL con los osciladores del
// calefactor hasta la temperatura pedida y captura el TRNG en bruto con el
// calefactor encendido y justo despues de apagarlo (heat_test()).
//
// Modo test de reinicios (buzon board::MAILBOX escrito por XSCT): solo se
// ejecuta restart_test() y cada arranque del TRNG se imprime como una linea
// "R <i> <hex>" (tools/restart_test.py construye la matriz de SP 800-90B).
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
#include "aurora_ldpc.h"
#include "aurora_random.h"
#include "aurora_trng.h"
#include "aurora_trng_driver.h"

#include "sleep.h"
#include "xil_cache.h"
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

// Temperatura (grados C) de un SYSMON (formula de UG580 / xsysmonpsu.h para la
// referencia interna). Devuelve -999 si el sensor no responde.
double sysmon_temp(uintptr_t addr) {
    const uint32_t raw = Xil_In32(addr) & 0xFFFFu;
    if (raw == 0) return -999.0;
    const double t = raw * 509.3140064 / 65536.0 - 280.23087870;
    return (t < -60.0 || t > 150.0) ? -999.0 : t;
}

void print_temp(const char *where) {
    char ps[16], pl[16];
    const double tps = sysmon_temp(board::AMS_PS_TEMP), tpl = sysmon_temp(board::AMS_PL_TEMP);
    if (tps > -999.0) ::snprintf(ps, sizeof ps, "%.1f", tps); else ::snprintf(ps, sizeof ps, "n/d");
    if (tpl > -999.0) ::snprintf(pl, sizeof pl, "%.1f", tpl); else ::snprintf(pl, sizeof pl, "n/d");
    std::printf("TEMP %s ps=%s pl=%s\n", where, ps, pl);
}

Bits to_bits(const uint8_t *p, size_t n) { return Bits(p, p + n); }

// ----------------------------------------------------- test de reinicios -----
struct RestartConfig {
    bool active = false;
    uint32_t n = 1000, skip = 0, off_us = 1000;
};

struct HeatConfig {
    bool active = false;
    double target = 70.0, limit = 85.0;
    uint32_t hold_s = 60, step_ms = 2000;
};

HeatConfig g_heat;   // lo rellena read_mailbox() si el buzon pide el modo calefactor

RestartConfig read_mailbox() {
    RestartConfig c;
    Xil_DCacheInvalidateRange((INTPTR)board::MAILBOX, 64);
    const uint32_t magic = Xil_In32(board::MAILBOX);
    if (magic == board::MAILBOX_MAGIC_HEAT) {
        g_heat.active = true;
        g_heat.target = Xil_In32(board::MAILBOX + 4) / 10.0;
        g_heat.hold_s = Xil_In32(board::MAILBOX + 8);
        g_heat.limit = Xil_In32(board::MAILBOX + 12) / 10.0;
        const uint32_t step = Xil_In32(board::MAILBOX + 16);
        g_heat.step_ms = step ? step : 2000;
        Xil_Out32(board::MAILBOX, 0);
        Xil_DCacheFlushRange((INTPTR)board::MAILBOX, 64);
        return c;
    }
    if (magic != board::MAILBOX_MAGIC_RESTART) return c;
    c.active = true;
    c.n = Xil_In32(board::MAILBOX + 4);
    c.skip = Xil_In32(board::MAILBOX + 8);
    c.off_us = Xil_In32(board::MAILBOX + 12);
    // Se borra para que un arranque posterior sin buzon haga la prueba normal.
    Xil_Out32(board::MAILBOX, 0);
    Xil_DCacheFlushRange((INTPTR)board::MAILBOX, 64);
    return c;
}

constexpr uint32_t RESTART_SAMPLES = 1000;   // SP 800-90B: 1000 muestras por reinicio

// SP 800-90B 3.1.4: se para la fuente (enable = 0 apaga los osciladores en
// anillo), se reinicia y se guardan las primeras muestras en bruto. 'skip'
// descarta las primeras muestras tras el arranque (en produccion el TRNG no
// entrega nada hasta superar el test de arranque de 1024 muestras).
void restart_test(TrngRegs &r, const RestartConfig &c) {
    using namespace trng_reg;
    std::printf("\n== Test de reinicios del TRNG: %u reinicios x %u muestras, descarte %u, apagado %u us ==\n", c.n,
                RESTART_SAMPLES, c.skip, c.off_us);
    if (r.read(ID) != ID_VALUE) { check(false, "registro ID del TRNG"); return; }
    if (c.n == 0 || c.n > 100000 || c.skip > 1000000) { check(false, "parametros del buzon validos"); return; }
    const uint32_t total = c.skip + RESTART_SAMPLES;
    const uint32_t words = (total + 31) / 32;
    std::vector<uint32_t> buf(words);
    std::string hex(RESTART_SAMPLES / 4, '0');
    uint32_t bad = 0;
    r.write(CTRL, CTRL_CLEAR);
    r.write(DECIM, 64);
    print_temp("inicio");
    std::printf("RESTART_BEGIN %u %u %u %u\n", c.n, RESTART_SAMPLES, c.skip, c.off_us);
    const double t0 = now_ms();
    for (uint32_t i = 0; i < c.n; ++i) {
        r.write(CTRL, CTRL_CLEAR);              // osciladores parados, estado a cero
        usleep(c.off_us);
        r.write(CTRL, CTRL_ENABLE | CTRL_BYPASS);
        uint32_t got = 0;
        const double ts = now_ms();
        while (got < words && now_ms() - ts < 1000.0)
            if (!(r.read(STATUS) & ST_EMPTY)) buf[got++] = r.read(DATA);
        const uint32_t dropped = r.read(DROPPED);
        r.write(CTRL, CTRL_CLEAR);
        if (got < words || dropped != 0) {
            ++bad;
            std::printf("RESTART_ERROR %u palabras %u/%u perdidas %u\n", i, got, words, dropped);
            continue;
        }
        // Muestras [skip, skip + 1000) -> 250 digitos hex (bit mas antiguo primero).
        static const char digits[] = "0123456789abcdef";
        for (uint32_t k = 0; k < RESTART_SAMPLES / 4; ++k) {
            uint32_t nib = 0;
            for (uint32_t b = 0; b < 4; ++b) {
                const uint32_t idx = c.skip + 4 * k + b;
                nib = (nib << 1) | ((buf[idx / 32] >> (31 - idx % 32)) & 1u);
            }
            hex[k] = digits[nib];
        }
        std::printf("R %u %s\n", i, hex.c_str());
    }
    std::printf("RESTART_END\n");
    print_temp("fin");
    std::printf("  %u reinicios en %.1f s, %u con error\n", c.n, (now_ms() - t0) / 1000.0, bad);
    check(bad == 0, "todos los reinicios capturados sin perdidas");
}

// ------------------------------------------------------------ calefactor -----
// Temperatura de la PL (la de la fuente del TRNG); si no responde, la del PS.
double chip_temp() {
    const double t = sysmon_temp(board::AMS_PL_TEMP);
    return t > -999.0 ? t : sysmon_temp(board::AMS_PS_TEMP);
}

// Captura en bruto a memoria (bypass, sin huecos). Devuelve las palabras perdidas.
uint32_t raw_capture_to(TrngRegs &r, std::vector<uint32_t> &buf, uint32_t words) {
    using namespace trng_reg;
    buf.clear();
    buf.reserve(words);
    r.write(CTRL, CTRL_CLEAR);
    r.write(DECIM, 64);
    r.write(CTRL, CTRL_ENABLE | CTRL_CLEAR | CTRL_BYPASS);
    const double t0 = now_ms();
    while (buf.size() < words && now_ms() - t0 < 10000)
        if (!(r.read(STATUS) & ST_EMPTY)) buf.push_back(r.read(DATA));
    const uint32_t dropped = r.read(DROPPED);
    r.write(CTRL, CTRL_CLEAR);
    return dropped;
}

void print_capture(const char *label, const std::vector<uint32_t> &buf) {
    std::printf("TRNG_RAW_BEGIN %zu %s\n", buf.size(), label);
    for (uint32_t w : buf) std::printf("%08" PRIx32 "\n", w);
    std::printf("TRNG_RAW_END\n");
}

void heat_test(TrngRegs &trng, const HeatConfig &c) {
    using namespace board::heater_reg;
    const uintptr_t H = board::HEATER_BASE;
    std::printf("\n== Calefactor: objetivo %.1f C, estable %u s, limite %.1f C ==\n", c.target, c.hold_s, c.limit);
    if (Xil_In32(H + ID) != ID_VALUE) { check(false, "registro ID del calefactor"); return; }
    const uint32_t cfg = Xil_In32(H + CONFIG);
    const uint32_t n_banks = (cfg >> 8) & 0xFF, per_bank = cfg >> 16;
    std::printf("  %u bancos x %u osciladores\n", n_banks, per_bank);
    if (c.limit > 95.0 || c.target >= c.limit || n_banks == 0 || n_banks > 32) {
        check(false, "parametros del calefactor validos (limite <= 95 C, objetivo < limite)");
        return;
    }
    Xil_Out32(H + WDOG, 100000000u);   // 1 s sin refresco => se apaga solo
    print_temp("inicio");

    auto mask_of = [](uint32_t n) { return n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u); };
    uint32_t n_on = 0;
    bool reached = false, abort = false;
    const double t_start = now_ms();
    double last_step = t_start, last_print = 0, stable_since = -1;
    double t_max = -1e9, all_on_since = -1;
    while (true) {
        const double now = now_ms(), T = chip_temp();
        if (T <= -999.0 || T >= c.limit) {
            Xil_Out32(H + CTRL, 0);
            std::printf("  CORTE: temperatura %.1f C (limite %.1f C): calefactor apagado\n", T, c.limit);
            abort = true;
            break;
        }
        t_max = T > t_max ? T : t_max;
        if (now - last_step >= c.step_ms) {
            if (T < c.target - 1.0 && n_on < n_banks) ++n_on;
            else if (T > c.target + 1.0 && n_on > 0) --n_on;
            last_step = now;
        }
        Xil_Out32(H + CTRL, mask_of(n_on));   // tambien refresca el watchdog
        const bool in_band = T >= c.target - 2.0 && T <= c.target + 2.0;
        stable_since = in_band ? (stable_since < 0 ? now : stable_since) : -1;
        if (now - last_print >= 5000) {
            std::printf("HEAT t=%6.0f s  T=%5.1f C  bancos=%2u/%u  actividad=%08" PRIx32 "\n", (now - t_start) / 1000,
                        T, n_on, n_banks, Xil_In32(H + ACTIVITY));
            last_print = now;
        }
        if (stable_since >= 0 && now - stable_since >= c.hold_s * 1000.0) { reached = true; break; }
        // Sin potencia suficiente: todos los bancos encendidos 5 minutos sin llegar.
        all_on_since = (n_on == n_banks) ? (all_on_since < 0 ? now : all_on_since) : -1;
        if (all_on_since >= 0 && now - all_on_since > 300000 && T < c.target - 2.0) break;
        usleep(200000);
    }
    if (abort) { Xil_Out32(H + CTRL, 0); check(false, "temperatura por debajo del limite"); return; }
    std::printf("  %s: T = %.1f C con %u/%u bancos (maximo %.1f C)\n",
                reached ? "objetivo alcanzado y estable" : "objetivo NO alcanzado (potencia insuficiente)", chip_temp(),
                n_on, n_banks, t_max);

    // Capturas: con el calefactor encendido (el refresco cubre los 0,7 s) y
    // justo despues de apagarlo (la referencia: sin ruido del calefactor).
    std::vector<uint32_t> on, off;
    Xil_Out32(H + CTRL, mask_of(n_on));
    print_temp("calefactor_on");
    const uint32_t d_on = raw_capture_to(trng, on, AURORA_TRNG_CAPTURE_WORDS);
    Xil_Out32(H + CTRL, 0);
    print_temp("calefactor_off");
    const uint32_t d_off = raw_capture_to(trng, off, AURORA_TRNG_CAPTURE_WORDS);
    print_temp("fin_captura");
    const uint32_t tripped = Xil_In32(H + STATUS) & 1u;
    check(Xil_In32(H + CTRL) == 0, "calefactor apagado al terminar");
    check(on.size() == AURORA_TRNG_CAPTURE_WORDS && off.size() == AURORA_TRNG_CAPTURE_WORDS && d_on == 0 && d_off == 0,
          "capturas completas y sin perdidas");
    check(!tripped, "el watchdog no salto durante la prueba");
    print_capture("caliente_off", off);
    print_capture("caliente_on", on);
}

// ----------------------------------------------------------------- TRNG -----
void trng_raw_capture(TrngRegs &r) {
    using namespace trng_reg;
    std::printf("\n== 1. TRNG: captura en bruto (bypass, %d palabras) ==\n", AURORA_TRNG_CAPTURE_WORDS);
    if (r.read(ID) != ID_VALUE) { check(false, "registro ID del TRNG"); return; }
    print_temp("inicio");
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
    print_temp("captura");
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
    if (bm_delays_valid) {
        const double *a = bm_delays_alice, *b = bm_delays_bob;
        std::printf("  compensacion de retardos por detector (ps, H V D A): Alice %+.1f %+.1f %+.1f %+.1f, "
                    "Bob %+.1f %+.1f %+.1f %+.1f; ventana +-%u ps\n",
                    a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], bm_half_window_ps);
    } else {
        std::printf("  sin compensacion de retardos; ventana +-%u ps\n", bm_half_window_ps);
    }
    for (uint32_t i = 0; i < bm_n_batches; ++i) {
        const BmBatch &b = bm_batches[i];
        Detections A, B;
        A.tags.assign(b.alice_tags, b.alice_tags + b.n_alice);
        A.states.assign(b.alice_states, b.alice_states + b.n_alice);
        B.tags.assign(b.bob_tags, b.bob_tags + b.n_bob);
        B.states.assign(b.bob_states, b.bob_states + b.n_bob);

        hw_kernel_timeout = false;
        double t0 = now_ms();
        // Retardo de cada detector restado en los dos lados ANTES de sincronizar
        // (los tiempos que se publicarian ya no dependen del bit).
        if (bm_delays_valid) {
            apply_detector_delays(A, bm_delays_alice);
            apply_detector_delays(B, bm_delays_bob);
        }
        double tc = now_ms();
        LagEstimate e = estimate_lag(A, B);
        double t1 = now_ms();
        const int64_t lag = (int64_t)compat::llround(e.lag);
        SiftOutput s = sift_batch(A, B, lag, bm_half_window_ps);
        double t2 = now_ms();

        std::printf("  %s: lag %" PRId64 " ps (PC %" PRId64 "), coincidencias %u (PC %u), cribados %zu (PC %u); "
                    "compensacion %.2f ms, sync %.2f ms, sift %.2f ms\n",
                    b.name, lag, b.exp_lag_ps, s.n_coincidences, b.exp_coincidences, s.alice_key.size(),
                    b.exp_sifted, tc - t0, t1 - tc, t2 - t1);
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

// --------------------------------------------------------------- LDPC -------
struct LdpcRun {
    LdpcReconcileResult r;
    double ms = 0;
};

LdpcRun run_ldpc(const Bits &ka, const Bits &kb, const LdpcConfig &cfg) {
    // Bits perforados de Alice: aqui un PRNG fijo para que las tres ejecuciones
    // reciban exactamente la misma entrada (en la cadena real salen del TRNG).
    SplitMix64 prng(0x1D9C0FFEEull);
    LdpcRun out;
    const double t0 = now_ms();
    out.r = ldpc_reconcile(ka, kb, cfg, 0xA11CEull, [&](size_t n, Bits &o) {
        o.resize(n);
        for (auto &v : o) v = (uint8_t)(prng.next() & 1);
        return true;
    });
    out.ms = now_ms() - t0;
    return out;
}

void ldpc_hw_vs_sw(RandomSource *rng) {
    std::printf("\n== 6. Reconciliacion LDPC: PL frente a PS ==\n");
    if (!hw_ldpc_in_pl)
        std::printf("  AVISO: el bitstream no incluye aurora_ldpc_dec; la fila 'PL' usa el modelo SW\n");
    Bits ka = to_bits(bm_sifted_alice, bm_sifted_len), kb = to_bits(bm_sifted_bob, bm_sifted_len);
    uint32_t errors = 0;
    for (size_t i = 0; i < ka.size(); ++i) errors += ka[i] != kb[i];
    const double q = (double)errors / ka.size();

    LdpcConfig hw = ldpc_config_fixed();                       // kernel de la PL
    LdpcConfig sw = ldpc_config_fixed(sw_aurora_ldpc_dec);     // mismo algoritmo en el A53
    LdpcConfig fl;                                             // sum-product, coma flotante (A53)
    hw_kernel_timeout = false;
    // Primera pasada: construye (una vez) los codigos que se usan y los carga
    // en el kernel; las medidas de abajo ya no incluyen ese coste.
    double t0 = now_ms();
    run_ldpc(ka, kb, hw);
    run_ldpc(ka, kb, fl);
    std::printf("  primera ejecucion (construccion de codigos incluida): %.0f ms\n", now_ms() - t0);
    LdpcRun a = run_ldpc(ka, kb, hw);
    LdpcRun b = run_ldpc(ka, kb, sw);
    LdpcRun c = run_ldpc(ka, kb, fl);
    auto line = [&](const char *name, const LdpcRun &x) {
        const double f = x.r.alice.empty() ? 0.0 : x.r.leaked_bits / (x.r.alice.size() * binary_entropy(q));
        std::printf("  %-28s %8.1f ms  tramas %u (fallidas %u)  fuga %" PRIu64 "  f=%.3f  iter.max %.0f  claves %s\n",
                    name, x.ms, x.r.frames, x.r.failed, x.r.leaked_bits, f, x.r.max_iter_seen,
                    x.r.alice == x.r.bob ? "iguales" : "DISTINTAS");
    };
    line("PL (min-sum, coma fija)", a);
    line("PS (min-sum, coma fija)", b);
    line("PS (sum-product, flotante)", c);
    if (a.ms > 0) std::printf("  aceleracion PL / PS (mismo algoritmo): x%.1f\n", b.ms / a.ms);
    check(!hw_kernel_timeout, "el kernel LDPC termina");
    check(a.r.alice == a.r.bob && !a.r.alice.empty(), "PL: claves de Alice y Bob iguales");
    check(a.r.bob == b.r.bob && a.r.leaked_bits == b.r.leaked_bits && a.r.failed == b.r.failed &&
              a.r.max_iter_seen == b.r.max_iter_seen,
          "PL y PS (mismo algoritmo) identicos bit a bit");
    check(c.r.alice == c.r.bob, "PS sum-product: claves iguales");

    if (!rng) return;
    Bits psk = to_bits(bm_psk, bm_psk_len);
    KeyPool pa(psk), pb(psk);
    AuthenticatedConfig cfg;
    cfg.reconciliation = Reconciliation::LDPC;
    cfg.ldpc = hw;
    t0 = now_ms();
    AuthenticatedResult r = run_authenticated_postprocessing(ka, kb, pa, pb, cfg, *rng);
    std::printf("  cadena autenticada con LDPC en la PL: PA %u bits, neto %zu bits, %.1f ms%s%s\n", r.pa_length,
                r.alice_key.size(), now_ms() - t0, r.ok ? "" : " - ", r.ok ? "" : r.abort_reason.c_str());
    check(r.ok && !r.alice_key.empty() && r.alice_key == r.bob_key, "cadena autenticada con LDPC en la PL");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("\n============================================================\n");
    std::printf(" AURORA - prueba en placa (bare-metal)\n");
    std::printf("============================================================\n");

    BoardRegs regs(board::TRNG_BASE);
    const RestartConfig rc = read_mailbox();
    if (g_heat.active) {
        heat_test(regs, g_heat);
        Xil_Out32(board::HEATER_BASE + board::heater_reg::CTRL, 0);
        std::printf("\nRESULTADO: %s (%d fallos)\n", g_fail == 0 ? "TODO OK" : "HAY FALLOS", g_fail);
        std::printf("AURORA_END\n");
        return 0;
    }
    if (rc.active) {
        restart_test(regs, rc);
        std::printf("\nRESULTADO: %s (%d fallos)\n", g_fail == 0 ? "TODO OK" : "HAY FALLOS", g_fail);
        std::printf("AURORA_END\n");
        return 0;
    }
    trng_raw_capture(regs);

    TrngDriver drv(regs);
    const bool trng_ok = trng_start(drv);

    kernels_vs_pc();
    toeplitz_vs_reference();

    if (trng_ok) {
        TrngRandomSource src(drv, to_bits(bm_extractor_seed, bm_extractor_seed_len));
        full_pipeline(src);
        ldpc_hw_vs_sw(&src);
    } else {
        std::printf("\n== 5. Cadena completa: OMITIDA (el TRNG no arranco) ==\n");
        ++g_fail;
        ldpc_hw_vs_sw(nullptr);
    }

    std::printf("\nRESULTADO: %s (%d fallos)\n", g_fail == 0 ? "TODO OK" : "HAY FALLOS", g_fail);
    std::printf("AURORA_END\n");
    return 0;
}
