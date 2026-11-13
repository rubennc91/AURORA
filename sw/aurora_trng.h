// =============================================================================
// AURORA - TRNG: modelo de referencia, estimacion de entropia y extraccion (PS)
// =============================================================================
// Complementa al RTL de rtl/trng/:
//   * Modelo software EXACTO de los tests de salud del hardware (RCT y APT de
//     NIST SP 800-90B, seccion 4.4) para contrastar la simulacion y la placa.
//   * Calculo de los umbrales a partir de la min-entropia asumida.
//   * Estimadores de min-entropia (subconjunto de SP 800-90B, seccion 6.3,
//     para datos binarios): Most Common Value y Markov.
//   * Extractor Toeplitz (Leftover Hash Lemma) para obtener bits casi
//     uniformes a partir de los bits brutos.
//
// AVISO: la entropia de un TRNG de osciladores en anillo depende del
// dispositivo, la colocacion, la temperatura y la tension. Hay que medirla
// con capturas de la placa (CTRL.gate_bypass) y fijar H_min de forma
// conservadora. El modelo de simulacion del RTL no dice nada sobre la
// entropia real.
// =============================================================================
#ifndef AURORA_TRNG_H
#define AURORA_TRNG_H

#include <cstdint>
#include <string>
#include <vector>

#include "aurora_host.h"

namespace aurora {

// Mapa de registros de rtl/trng/trng_axi.v (offsets en bytes).
namespace trng_reg {
constexpr uint32_t CTRL = 0x00, STATUS = 0x04, DATA = 0x08, DECIM = 0x0C, RCT_CUT = 0x10, APT_CUT = 0x14,
                   SAMPLES = 0x18, DROPPED = 0x1C, ID = 0x20, RCT_IDX = 0x24, APT_IDX = 0x28;
constexpr uint32_t ID_VALUE = 0x54524E47;   // "TRNG"
constexpr uint32_t CTRL_ENABLE = 1u << 0, CTRL_CLEAR = 1u << 1, CTRL_BYPASS = 1u << 8;
constexpr uint32_t ST_STARTUP = 1u << 0, ST_RCT = 1u << 1, ST_APT = 1u << 2, ST_EMPTY = 1u << 3, ST_FULL = 1u << 4;
inline uint32_t ctrl_fault(uint32_t f) { return (f & 3u) << 4; }
} // namespace trng_reg

constexpr uint32_t APT_WINDOW = 1024;

// Umbrales de SP 800-90B para min-entropia h (bits/muestra) y una
// probabilidad de falsa alarma alpha = 2^alpha_log2.
uint32_t rct_cutoff(double h, double alpha_log2 = -20.0);
uint32_t apt_cutoff(double h, double alpha_log2 = -20.0, uint32_t window = APT_WINDOW);

struct HealthConfig {
    uint32_t rct_cutoff = 41;    // h = 0.5, alpha = 2^-20
    uint32_t apt_cutoff = 0;     // 0 => se calcula con apt_cutoff(0.5)
};

struct HealthResult {
    int64_t rct_fail_idx = -1;   // indice de la muestra que disparo la alarma
    int64_t apt_fail_idx = -1;
    bool startup_ok = false;     // primeras 1024 muestras sin alarma
    bool healthy() const { return rct_fail_idx < 0 && apt_fail_idx < 0; }
};

// Modelo exacto de rtl/trng/trng_health.v.
HealthResult run_health_tests(const Bits &raw, const HealthConfig &cfg = HealthConfig());

struct EntropyEstimate {
    double mcv = 0;      // Most Common Value (6.3.1), con cota de confianza al 99%
    double markov = 0;   // Markov (6.3.3)
    double min = 0;      // estimacion final = minimo
};
EntropyEstimate estimate_min_entropy(const Bits &raw);

// Longitud segura de salida del extractor: floor(n*h - 2*log2(1/eps)).
uint32_t extractor_output_length(size_t n, double h_min, double eps_log2 = -64.0);

// Extraccion con Toeplitz por bloques de TRNG_EXTRACT_BLOCK bits brutos (el
// ultimo puede ser parcial). Todos los bloques usan la misma semilla PUBLICA
// y FIJA, que debe ser uniforme e independiente de la fuente: se genera una
// vez en la instalacion (aurora_cli gen-random) y se guarda. Necesita al menos
// extractor_seed_length(h_min) bits (se usa un prefijo). Devuelve vacio si la
// semilla es demasiado corta.
constexpr uint32_t TRNG_EXTRACT_BLOCK = 65536;
size_t extractor_seed_length(double h_min, double eps_log2 = -64.0);
Bits extract_random(const Bits &raw, double h_min, const Bits &public_seed, double eps_log2 = -64.0);
// Bits brutos necesarios para obtener al menos n bits extraidos.
size_t raw_bits_needed(size_t n, double h_min, double eps_log2 = -64.0);

// Ficheros de bits (semilla del extractor, clave precompartida): palabras hex
// de 32 bits por linea, MSB primero, con una cabecera "# bits N".
bool save_bits_hex(const std::string &path, const Bits &bits, std::string *err = nullptr);
bool load_bits_hex(const std::string &path, Bits &bits, std::string *err = nullptr);

// Lectura de capturas: una palabra hexadecimal de 32 bits por linea (formato
// del testbench y del volcado del driver). Bits en orden MSB primero (el
// primer bit muestreado es el bit 31).
bool load_trng_words(const std::string &path, Bits &out, std::string *err = nullptr);

} // namespace aurora

#endif
