// =============================================================================
// AURORA - Tests del driver del TRNG contra un modelo del periferico
// =============================================================================
// El modelo reproduce el comportamiento de rtl/trng/trng_axi.v a nivel de
// registros (la equivalencia con el RTL la comprueba tests/trng/). Permite
// simular averias del hardware que en la placa no se pueden provocar.
// =============================================================================
#include <deque>
#include <random>
#include <string>

#include "../../sw/aurora_random.h"
#include "../../sw/aurora_trng_driver.h"
#include "mock_trng.h"
#include "test_framework.h"

using namespace aurora;



TEST(driver_init_runs_self_test_and_starts) {
    MockTrng hw;
    TrngDriver drv(hw);
    TrngStatus s = drv.init();
    CHECK_MSG(s == TrngStatus::OK, trng_status_str(s));
    CHECK(hw.read(trng_reg::RCT_CUT) == rct_cutoff(0.5) && hw.read(trng_reg::APT_CUT) == apt_cutoff(0.5));
    CHECK(hw.ctrl_writes >= 5);   // clear + 3 fallos + arranque
}

TEST(driver_reads_random_bits) {
    MockTrng hw;
    TrngDriver drv(hw);
    CHECK(drv.init() == TrngStatus::OK);
    Bits raw, rnd;
    CHECK(drv.read_raw(raw, 5000) == TrngStatus::OK && raw.size() == 5000);
    SimulatedRandomSource gen(5);
    Bits seed;
    gen.random_bits(extractor_seed_length(0.5), seed);
    CHECK(drv.read_random(rnd, 4096, Bits(100, 0)) == TrngStatus::BAD_SEED);
    CHECK(drv.read_random(rnd, 4096, seed) == TrngStatus::OK && rnd.size() == 4096);
    size_t ones = 0;
    for (auto v : rnd) ones += v;
    CHECK_MSG(ones > 1800 && ones < 2300, std::to_string(ones));
}

TEST(driver_rejects_wrong_device) {
    MockTrng hw;
    hw.wrong_id = true;
    TrngDriver drv(hw);
    CHECK(drv.init() == TrngStatus::NOT_FOUND);
}

TEST(driver_detects_broken_health_tests) {
    // Si los tests de salud del hardware no funcionan, el autodiagnostico falla.
    MockTrng hw;
    hw.health_broken = true;
    TrngDriver drv(hw);
    TrngConfig cfg;
    cfg.timeout_us = 20000;
    CHECK(drv.init(cfg) == TrngStatus::SELFTEST_FAILED);
    Bits b;
    CHECK(drv.read_raw(b, 32) != TrngStatus::OK && b.empty());
}

TEST(driver_stops_delivering_bits_when_source_fails) {
    MockTrng hw;
    TrngDriver drv(hw);
    CHECK(drv.init() == TrngStatus::OK);
    hw.die_after_samples = 0;   // a partir de ahora la fuente esta bloqueada
    // Se vacia lo que ya estuviera en la FIFO y despues debe llegar la alarma.
    Bits b;
    TrngStatus s = drv.read_raw(b, 1'000'000);
    CHECK_MSG(s == TrngStatus::ALARM, trng_status_str(s));
    CHECK(b.empty());
    CHECK(drv.read_raw(b, 32) == TrngStatus::ALARM);   // hasta reinicializar
}

TEST(trng_random_source_feeds_the_protocol) {
    MockTrng hw;
    TrngDriver drv(hw);
    CHECK(drv.init() == TrngStatus::OK);
    SimulatedRandomSource gen(6);
    Bits seed;
    gen.random_bits(extractor_seed_length(0.5), seed);
    TrngRandomSource src(drv, seed);
    CHECK(!src.simulated());
    Bits a, b;
    CHECK(src.random_bits(70000, a) && a.size() == 70000);   // tamano tipico de una semilla Toeplitz
    CHECK(src.random_bits(64, b) && b.size() == 64);
    hw.die_after_samples = 0;
    Bits c;
    bool ok = true;
    for (int i = 0; i < 10 && ok; ++i) ok = src.random_bits(4096, c);   // vacia la FIFO y llega la alarma
    CHECK(!ok && src.last_status() == TrngStatus::ALARM);
}

int main() { return tf::run_all("AURORA driver del TRNG"); }
