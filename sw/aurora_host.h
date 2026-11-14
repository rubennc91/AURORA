// =============================================================================
// AURORA - Software del PS (ARM): orquestacion de kernels, Cascade y PA
// =============================================================================
// En el PC estas funciones llaman directamente a las funciones C++ de los
// kernels (hw/). En el Zynq se sustituyen por las llamadas al driver generado
// por Vitis HLS (XAurora_sync_Start, ...) manteniendo la misma interfaz.
// =============================================================================
#ifndef AURORA_HOST_H
#define AURORA_HOST_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../hw/aurora_kernels.h"

namespace aurora {

// Semiancho por defecto de la ventana de coincidencia (ps). Barrido sobre los
// 101 batches reales (clave neta con Cascade, PA asintotica): 250 ps -> 24.945,
// 350 -> 28.381, 400 -> 28.686 (maximo), 450 -> 28.339. Ver docs/REVIEW.md.
constexpr uint32_t DEFAULT_HALF_WINDOW_PS = 400;
// Con la compensacion de retardos por detector (apply_detector_delays) el pico
// se estrecha y el optimo pasa a +-350 ps (28.333 bits netos).
constexpr uint32_t COMPENSATED_HALF_WINDOW_PS = 350;

struct Detections {
    std::vector<uint64_t> tags;    // ps, ordenados
    std::vector<uint8_t> states;   // 0..3
    size_t size() const { return tags.size(); }
};

// Lee "<prefix>_alice.detections" / "<prefix>_bob.detections" en formato
// "(t,s),(t,s),...". Devuelve false si falta algun fichero o esta mal formado.
bool load_detections(const std::string &path, Detections &out, std::string *err = nullptr);
bool load_batch(const std::string &prefix, Detections &alice, Detections &bob, std::string *err = nullptr);

// Comprueba invariantes de entrada (orden, estados validos, rango < 2^62).
bool validate_detections(const Detections &d, std::string *err = nullptr);

// ---------------------------------------------------------------- bits -------
using Bits = std::vector<uint8_t>;   // un bit por byte (comodo en el PS)
Bits unpack_bits(const uint64_t *words, uint32_t n_bits);
std::vector<uint64_t> pack_bits(const Bits &bits);
std::string bits_to_hex(const Bits &bits);   // nibble = 4 bits, LSB primero

// -------------------------------------------------------- sincronizacion -----
struct SyncConfig {
    // Paso grueso: 1024 bins de 1.024 ns => +-524 ns.
    int64_t  coarse_lag_min = -(int64_t)512 * 1024;
    uint32_t coarse_shift = 10;
    uint32_t coarse_bins = 1024;
    // Paso fino: 1024 bins de 8 ps alrededor del pico grueso => +-4 ns.
    uint32_t fine_shift = 3;
    uint32_t fine_bins = 1024;
    // Centroide: semiancho de la region de senal y separacion para el fondo (ps).
    int64_t signal_half_width_ps = 600;
    int64_t background_guard_ps = 1200;
    double  min_snr = 5.0;          // significancia minima para aceptar el lag
};

struct LagEstimate {
    bool ok = false;
    int64_t coarse_lag = 0;     // ps (centro del bin grueso con mas cuentas)
    double lag = 0;             // ps, centroide fino con fondo restado
    double sigma = 0;           // ps, anchura (desviacion tipica) del pico
    double signal = 0;          // pares de senal estimados (fondo restado)
    double background_per_ps = 0;
    double snr = 0;
    uint32_t n_alice_used = 0, n_bob_used = 0;
    // Histograma fino (para elegir la ventana: choose_window).
    std::vector<uint32_t> fine_hist;
    int64_t fine_min = 0;
    uint32_t fine_shift = 0;
    double background_per_bin = 0;
};

LagEstimate estimate_lag(const Detections &alice, const Detections &bob, const SyncConfig &cfg = SyncConfig());

// ------------------------------------------- retardos de cada detector -------
// Cada detector (estado 0..3 = H, V, D, A) de Alice y de Bob tiene su propio
// retardo. Si no se compensa, la diferencia de tiempos de un par depende del
// bit (fuga por tiempo, ver docs/REVIEW.md) y el pico se ensancha.
// Calibracion SOLO con tiempos (publicos): para los pares de las 16
// combinaciones de estados (tambien las de base distinta),
//   media(t_a - t_b - lag) = dA[a] - dB[b]
// se ajusta por minimos cuadrados ponderados con media de dB = 0.
struct DetectorDelays {
    double alice[4] = {0, 0, 0, 0}, bob[4] = {0, 0, 0, 0};   // ps
    bool valid = false;
    double max_residual = 0;          // ps, peor combinacion con >= 500 pares
    uint64_t pairs = 0;
};
// Acumulador por batch (para calibrar con muchos batches sin guardarlos).
struct DelayCalibrator {
    double sum[4][4] = {}, cnt[4][4] = {};
    uint32_t pair_window_ps = 300;
    // Anade los pares de un batch (estimate_lag incluido). false si no sincroniza.
    bool add(const Detections &alice, const Detections &bob);
    DetectorDelays solve() const;
};
// Calibracion iterativa: la ventana de pares (+-300 ps) recorta el pico, y con
// las colas anchas de los detectores reales la media de cada combinacion solo
// responde ~55 % a un desplazamiento (medido). Se compensa, se mide el residuo
// y se acumula hasta que todas las combinaciones quedan centradas (el residuo
// cae a la mitad en cada pasada con los datos reales: 8 pasadas, < 1 ps). 'for_each_batch' recorre los batches
// llamando a la funcion que recibe (Alice, Bob) - p. ej. cargandolos de disco.
DetectorDelays calibrate_detector_delays(
    const std::function<void(const std::function<void(Detections &, Detections &)> &)> &for_each_batch,
    int passes = 8);

// Resta a cada tag el retardo de su detector (y suma un margen fijo para no
// quedar negativo) y reordena. Aplicar a los dos lados antes de sincronizar.
void apply_detector_delays(Detections &d, const double delay_ps[4]);
bool save_detector_delays(const std::string &path, const DetectorDelays &d, std::string *err = nullptr);
bool load_detector_delays(const std::string &path, DetectorDelays &d, std::string *err = nullptr);

// ------------------------------------------------- ventana adaptativa --------
// Elige el semiancho de la ventana de coincidencia de un batch SOLO con datos
// de tiempo (publicos): el histograma fino de la sincronizacion da, para cada
// ventana w, los pares verdaderos S(w) (pico con el fondo restado) y los
// accidentales A(w) = fondo * 2w. Con el QBER intrinseco de los pares
// verdaderos (e, de bloques anteriores ya corregidos) el QBER esperado es
//   Q(w) = (e S + A/2) / (S + A)
// y se maximiza la clave esperada (S + A) (1 - (1 + f) h(Q)). No mira los bits
// del batch: no hay post-seleccion (el QBER se sigue midiendo despues).
struct WindowChoice {
    uint32_t half_window = DEFAULT_HALF_WINDOW_PS;
    double signal = 0, accidentals = 0, qber = 0, key_rate = 0;
    bool adaptive = false;   // false: datos insuficientes, se usa la ventana por defecto
};
struct WindowConfig {
    // QBER de los pares verdaderos en funcion de |d| (distancia al centro del
    // pico), en tramos de 'profile_bin_ps'. Los pares de las colas tienen mas
    // errores (retardos distintos entre detectores, ver docs/REVIEW.md).
    // Calibrado con los 101 batches reales (bloques ya corregidos: publico).
    // Si esta vacio, se usa 'intrinsic_qber' constante.
    std::vector<double> error_profile = {0.0060, 0.0032, 0.0052, 0.0092, 0.0074, 0.0119, 0.0184,
                                         0.0216, 0.0484, 0.1677, 0.35};
    uint32_t profile_bin_ps = 50;
    double intrinsic_qber = 0.008;
    double f_ec = 1.2;               // eficiencia de la correccion de errores
    uint32_t w_min = 150, w_max = 800, step = 25;
};
WindowChoice choose_window(const LagEstimate &e, const WindowConfig &cfg = WindowConfig());
// QBER intrinseco a partir del QBER medido con unas ventanas dadas: suma de
// pares verdaderos y accidentales esperados de cada batch (choose_window).
double intrinsic_qber(double qber_measured, double sum_signal, double sum_accidentals);
// Pares verdaderos y accidentales esperados con una ventana concreta (y, si se
// pasa 'cfg', los errores esperados en los pares verdaderos).
void window_expectation(const LagEstimate &e, uint32_t half_window, double &signal, double &accidentals,
                        const WindowConfig *cfg = nullptr, double *true_errors = nullptr);

// ---------------------------------------------------------------- sifting ----
struct SiftOutput {
    uint32_t n_coincidences = 0;
    Bits alice_key, bob_key;
    uint32_t n_chunks = 0;
};

// Procesa el batch completo en lotes de como mucho max_chunk tags por lado
// (exactamente equivalente a una unica pasada).
SiftOutput sift_batch(const Detections &alice, const Detections &bob, int64_t lag,
                      uint32_t half_window, uint32_t max_chunk = AURORA_MAX_TAGS);

// ---------------------------------------------------------------- Cascade ----
// Generador determinista compartido (en el sistema real la semilla viene de un
// QRNG y se intercambia por el canal clasico autenticado).
struct SplitMix64 {
    uint64_t s;
    explicit SplitMix64(uint64_t seed) : s(seed) {}
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    uint64_t bounded(uint64_t n);   // uniforme en [0, n), sin sesgo
};

std::vector<uint32_t> random_permutation(uint32_t n, SplitMix64 &rng);  // Fisher-Yates

struct CascadeConfig {
    int passes = 4;
    double qber_hint = 0.02;    // estimacion previa del QBER para el bloque k1
    uint64_t seed = 0xA0B0C0D0E0F00102ULL;
    // Canal clasico (opcional). Cada paridad que revela Alice pasa por
    // 'channel' (p. ej. un atacante) antes de llegar a Bob, y se notifica a
    // 'on_parity' para registrarla en las transcripciones de ambos lados.
    //   index: numero de orden de la paridad revelada
    //   pass, lo, hi: bloque consultado (posiciones [lo,hi) del pase)
    std::function<uint8_t(uint64_t index, uint8_t parity)> channel;
    std::function<void(uint32_t pass, uint32_t lo, uint32_t hi, uint8_t sent, uint8_t received)> on_parity;
};

struct CascadeResult {
    uint32_t corrected = 0;     // bits corregidos en Bob
    uint64_t leaked_bits = 0;   // paridades reveladas por el canal publico
    uint32_t first_block = 0;   // tamano de bloque del pase 1
    bool converged = true;      // false si se supero el limite de trabajo (canal
                                // inconsistente, p. ej. paridades alteradas)
};

// Corrige 'bob' para que coincida con 'alice'. Cada consulta de paridad de
// Alice cuenta como un bit filtrado.
CascadeResult cascade_correct(const Bits &alice, Bits &bob, const CascadeConfig &cfg = CascadeConfig());

// Hash de verificacion 2-universal (polinomio mod 2^61-1) tras la correccion.
uint64_t verification_hash(const Bits &key, uint64_t seed);

// -------------------------------------------------------- privacidad ---------
double binary_entropy(double p);

// Longitud de clave segura (BB84, asintotica, sin correcciones de tamano finito):
//   m = n (1 - h(Q)) - leak_ec - leak_verify - 2 log2(1/eps_pa)
// AVISO: no es una prueba de seguridad completa; validar con el equipo de UPM.
uint32_t pa_output_length(uint32_t n, double qber, uint64_t leak_ec, uint64_t leak_verify, double eps_pa = 1e-10);

// Hash de Toeplitz (PL). Entradas mayores que AURORA_PA_MAX_IN_BITS se
// procesan por bloques, comprimiendo cada uno en la misma proporcion.
//
// toeplitz_hash_seeded: semilla COMPLETA de toeplitz_seed_length(n_in, n_out)
// bits uniformes (p. ej. del TRNG). Es la que exige el Leftover Hash Lemma y
// la unica que debe usarse en produccion. Devuelve vacio si la longitud de la
// semilla no es exacta.
size_t toeplitz_seed_length(size_t n_in, uint32_t n_out);
Bits toeplitz_hash_seeded(const Bits &key, uint32_t n_out, const Bits &seed);
// SOLO PRUEBAS: semilla de 64 bits expandida con un PRNG (solo hay 2^64
// matrices posibles: la garantia matematica deja de cumplirse formalmente).
Bits toeplitz_hash(const Bits &key, uint32_t n_out, uint64_t seed);

// --------------------------------------------------------------- pipeline ----
struct BatchReport {
    std::string name;
    bool loaded = false;
    LagEstimate lag;
    uint32_t n_alice = 0, n_bob = 0;
    uint32_t n_coincidences = 0, n_sifted = 0;
    uint32_t n_errors = 0;          // solo para validacion (requiere ambas claves)
};

} // namespace aurora

#endif
