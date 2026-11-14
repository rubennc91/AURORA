// =============================================================================
// AURORA - Plataforma simulada para probar la aplicacion bare-metal en el PC
// =============================================================================
// Emula los perifericos del block design a nivel de registros:
//   * TRNG: tests/cpp/mock_trng.h (modelo de rtl/trng/trng_axi.v).
//   * Kernels HLS: al escribir ap_start se leen los argumentos de los
//     registros (con los offsets de shim/xaurora_*_hw.h) y se ejecuta el modelo
//     C++ del kernel (compilado con el prefijo sw_). ap_done se borra al leer,
//     como en el hardware.
// Comprueba asi que hw_backend.cpp programa bien los registros y que la
// aplicacion completa funciona, antes de llevarla a la placa.
// =============================================================================
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "../../hw/aurora_kernels.h"
#include "../../sw/baremetal/aurora_board.h"
#include "../cpp/mock_trng.h"
#include "shim/xaurora_sift_hw.h"
#include "shim/xaurora_sync_hw.h"
#include "shim/xaurora_toeplitz_hw.h"
#include "shim/xaurora_ldpc_dec_hw.h"

// Modelos C++ de los kernels (hw/*.cpp compilados con -Daurora_X=sw_aurora_X).
void sw_aurora_sync(const uint64_t *, uint32_t, const uint64_t *, uint32_t, int64_t, uint32_t, uint32_t,
                    uint32_t *, SyncResult *);
void sw_aurora_sift(const uint64_t *, const uint8_t *, uint32_t, const uint64_t *, const uint8_t *, uint32_t,
                    int64_t, uint32_t, uint64_t *, uint64_t *, SiftResult *);
void sw_aurora_toeplitz(const uint64_t *, uint32_t, const uint64_t *, uint64_t *, uint32_t);

unsigned long mock_cache_ops = 0;

namespace {
using namespace aurora;

std::map<uintptr_t, uint32_t> regs;   // registros de los kernels
MockTrng trng;
std::map<uintptr_t, bool> done_flag;

// Calefactor con un modelo termico de primer orden (tiempo real): la PL tiende
// a 30 C + 1,5 C por banco encendido con constante de tiempo de 1 s.
struct MockHeater {
    uint32_t mask = 0, tripped = 0, wdog = 100000000u;
    double T = 30.0;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now(), kick = last;
    void update() {
        auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        if (mask && std::chrono::duration<double>(now - kick).count() > wdog / 1e8) { mask = 0; tripped = 1; }
        const double t_inf = 30.0 + 1.5 * __builtin_popcount(mask);
        T += (t_inf - T) * (1.0 - std::exp(-dt / 1.0));
    }
    uint32_t read(uint32_t off) {
        update();
        switch (off) {
            case board::heater_reg::CTRL: return mask;
            case board::heater_reg::STATUS: return tripped;
            case board::heater_reg::WDOG: return wdog;
            case board::heater_reg::ID: return board::heater_reg::ID_VALUE;
            case board::heater_reg::CONFIG: return (512u << 16) | (32u << 8) | 3u;
            case board::heater_reg::ACTIVITY: return mask & (uint32_t)std::rand();
            default: return 0;
        }
    }
    void write(uint32_t off, uint32_t v) {
        update();
        if (off == board::heater_reg::CTRL) { mask = v; tripped = 0; kick = std::chrono::steady_clock::now(); }
        else if (off == board::heater_reg::WDOG) wdog = v;
    }
} heater;
bool sim_heat = false;

// AURORA_SIM_RESTART="n skip off_us" simula que XSCT ha escrito el buzon.
struct MailboxInit {
    MailboxInit() {
        // Sensores de temperatura: 45 C en el PS; el del PL sin responder (0).
        regs[board::AMS_PS_TEMP] = 0xA37A;
        double ht = 0, hl = 0;
        unsigned hh = 0, hs = 0;
        const char *he = std::getenv("AURORA_SIM_HEAT");   // "objetivo estable_s limite paso_ms"
        if (he && std::sscanf(he, "%lf %u %lf %u", &ht, &hh, &hl, &hs) == 4) {
            sim_heat = true;
            regs[board::MAILBOX] = board::MAILBOX_MAGIC_HEAT;
            regs[board::MAILBOX + 4] = (uint32_t)(ht * 10);
            regs[board::MAILBOX + 8] = hh;
            regs[board::MAILBOX + 12] = (uint32_t)(hl * 10);
            regs[board::MAILBOX + 16] = hs;
        }
        const char *e = std::getenv("AURORA_SIM_RESTART");
        unsigned n = 0, skip = 0, off = 0;
        if (e && std::sscanf(e, "%u %u %u", &n, &skip, &off) == 3) {
            regs[board::MAILBOX] = board::MAILBOX_MAGIC_RESTART;
            regs[board::MAILBOX + 4] = n;
            regs[board::MAILBOX + 8] = skip;
            regs[board::MAILBOX + 12] = off;
        }
    }
} mailbox_init;

uint32_t r32(uintptr_t a) { return regs[a]; }
uint64_t r64(uintptr_t a) { return (uint64_t)regs[a] | ((uint64_t)regs[a + 4] << 32); }
template <typename T> T *ptr(uintptr_t a) { return reinterpret_cast<T *>((uintptr_t)r64(a)); }

void run_sync() {
    const uintptr_t B = board::SYNC_BASE;
    SyncResult r{};
    sw_aurora_sync(ptr<const uint64_t>(B + XAURORA_SYNC_CONTROL_ADDR_ALICE_TAGS_DATA),
                   r32(B + XAURORA_SYNC_CONTROL_ADDR_N_ALICE_DATA),
                   ptr<const uint64_t>(B + XAURORA_SYNC_CONTROL_ADDR_BOB_TAGS_DATA),
                   r32(B + XAURORA_SYNC_CONTROL_ADDR_N_BOB_DATA),
                   (int64_t)r64(B + XAURORA_SYNC_CONTROL_ADDR_LAG_MIN_DATA),
                   r32(B + XAURORA_SYNC_CONTROL_ADDR_BIN_SHIFT_DATA), r32(B + XAURORA_SYNC_CONTROL_ADDR_N_BINS_DATA),
                   ptr<uint32_t>(B + XAURORA_SYNC_CONTROL_ADDR_HISTOGRAM_DATA), &r);
    const uintptr_t R = B + XAURORA_SYNC_CONTROL_ADDR_RESULT_DATA;
    regs[R] = r.peak_bin; regs[R + 4] = r.peak_count; regs[R + 8] = r.total_pairs; regs[R + 12] = r.complete;
}

void run_sift() {
    const uintptr_t B = board::SIFT_BASE;
    SiftResult r{};
    sw_aurora_sift(ptr<const uint64_t>(B + XAURORA_SIFT_CONTROL_ADDR_ALICE_TAGS_DATA),
                   ptr<const uint8_t>(B + XAURORA_SIFT_CONTROL_ADDR_ALICE_STATES_DATA),
                   r32(B + XAURORA_SIFT_CONTROL_ADDR_N_ALICE_DATA),
                   ptr<const uint64_t>(B + XAURORA_SIFT_CONTROL_ADDR_BOB_TAGS_DATA),
                   ptr<const uint8_t>(B + XAURORA_SIFT_CONTROL_ADDR_BOB_STATES_DATA),
                   r32(B + XAURORA_SIFT_CONTROL_ADDR_N_BOB_DATA), (int64_t)r64(B + XAURORA_SIFT_CONTROL_ADDR_LAG_DATA),
                   r32(B + XAURORA_SIFT_CONTROL_ADDR_HALF_WINDOW_DATA),
                   ptr<uint64_t>(B + XAURORA_SIFT_CONTROL_ADDR_ALICE_KEY_DATA),
                   ptr<uint64_t>(B + XAURORA_SIFT_CONTROL_ADDR_BOB_KEY_DATA), &r);
    const uintptr_t R = B + XAURORA_SIFT_CONTROL_ADDR_RESULT_DATA;
    regs[R] = r.n_coincidences; regs[R + 4] = r.n_sifted; regs[R + 8] = r.next_alice_idx;
}

void run_toeplitz() {
    const uintptr_t B = board::TOEPLITZ_BASE;
    sw_aurora_toeplitz(ptr<const uint64_t>(B + XAURORA_TOEPLITZ_CONTROL_ADDR_KEY_IN_DATA),
                       r32(B + XAURORA_TOEPLITZ_CONTROL_ADDR_N_IN_DATA),
                       ptr<const uint64_t>(B + XAURORA_TOEPLITZ_CONTROL_ADDR_SEED_DATA),
                       ptr<uint64_t>(B + XAURORA_TOEPLITZ_CONTROL_ADDR_KEY_OUT_DATA),
                       r32(B + XAURORA_TOEPLITZ_CONTROL_ADDR_N_OUT_DATA));
}

void run_ldpc() {
    // El modelo es el de sw/baremetal/ldpc_sw_model.cpp (mismo fuente que el kernel).
    const uintptr_t B = board::LDPC_BASE;
    sw_aurora_ldpc_dec(ptr<const uint32_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_CHK_PTR_DATA),
                       ptr<const uint16_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_CHK_VAR_DATA),
                       r32(B + XAURORA_LDPC_DEC_CONTROL_ADDR_N_DATA), r32(B + XAURORA_LDPC_DEC_CONTROL_ADDR_M_DATA),
                       r32(B + XAURORA_LDPC_DEC_CONTROL_ADDR_LOAD_CODE_DATA),
                       ptr<const int8_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_LLR_DATA),
                       ptr<const uint8_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_SYNDROME_DATA),
                       r32(B + XAURORA_LDPC_DEC_CONTROL_ADDR_MAX_ITER_DATA),
                       ptr<uint8_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_X_OUT_DATA),
                       ptr<uint32_t>(B + XAURORA_LDPC_DEC_CONTROL_ADDR_STATUS_DATA));
}

bool in_trng(uintptr_t a) { return a >= board::TRNG_BASE && a < board::TRNG_BASE + 0x1000; }
bool is_ctrl(uintptr_t a) {
    return a == board::SYNC_BASE || a == board::SIFT_BASE || a == board::TOEPLITZ_BASE || a == board::LDPC_BASE;
}
} // namespace

bool in_heater(uintptr_t a) { return a >= board::HEATER_BASE && a < board::HEATER_BASE + 0x1000; }

uint32_t mock_read32(uintptr_t a) {
    if (in_heater(a)) return heater.read((uint32_t)(a - board::HEATER_BASE));
    if (a == board::AMS_PL_TEMP && sim_heat) {
        heater.update();
        return (uint32_t)((heater.T + 280.23087870) * 65536.0 / 509.3140064);
    }
    if (in_trng(a)) {
        const uint32_t off = (uint32_t)(a - board::TRNG_BASE);
        if (off == trng_reg::STATUS) trng.wait_us(32);   // el tiempo avanza mientras se sondea
        return trng.read(off);
    }
    if (is_ctrl(a)) {
        bool d = done_flag[a];
        done_flag[a] = false;          // ap_done se borra al leer
        return d ? 0x6u : 0x4u;        // ap_done | ap_idle
    }
    return regs[a];
}

void mock_write32(uintptr_t a, uint32_t v) {
    if (in_heater(a)) { heater.write((uint32_t)(a - board::HEATER_BASE), v); return; }
    if (in_trng(a)) { trng.write((uint32_t)(a - board::TRNG_BASE), v); return; }
    if (is_ctrl(a) && (v & 1u)) {
        if (a == board::SYNC_BASE) run_sync();
        else if (a == board::SIFT_BASE) run_sift();
        else if (a == board::LDPC_BASE) run_ldpc();
        else run_toeplitz();
        done_flag[a] = true;
        return;
    }
    regs[a] = v;
}

void mock_usleep(unsigned us) { trng.wait_us(us); }
