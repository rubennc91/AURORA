// =============================================================================
// AURORA - Reconciliacion LDPC (alternativa a Cascade)
// =============================================================================
// Reconciliacion por sindrome en un solo sentido:
//   Alice envia s = H x_A (m - p bits utiles); Bob busca x tal que H x = s
//   partiendo de su clave (canal binario simetrico de probabilidad Q) con
//   propagacion de creencias (BP). No hay idas y vueltas como en Cascade: un
//   mensaje de Alice y una respuesta de Bob (exito / numero de errores).
//
// Adaptacion de tasa (Elkouss et al., 2009): sobre un codigo madre (n, m) de
// una familia fija, cada trama lleva k bits de clave, p bits PERFORADOS
// (aleatorios de Alice, desconocidos para Bob) y s bits ACORTADOS (valor
// conocido por ambos, 0). k + p + s = n. Fuga = m - p. Eficiencia
//   f = (m - p) / (k h(Q)).
// Las posiciones de p y s salen de una semilla publica nueva en cada ronda.
//
// Los codigos se construyen de forma determinista (misma matriz en Alice y en
// Bob sin intercambiarla): grados de variable irregulares {2, 3, D}, grados
// de comprobacion casi uniformes, sin ciclos de longitud 4.
//
// Decodificador: BP por capas (layered), sum-product exacto o min-sum
// normalizado (el que se llevaria a la PL). Exito = el sindrome coincide; la
// verificacion global (hash) detecta los raros falsos exitos.
// =============================================================================
#ifndef AURORA_LDPC_H
#define AURORA_LDPC_H

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "aurora_host.h"

namespace aurora {

struct LdpcCode {
    uint32_t n = 0, m = 0;
    // Grafo de Tanner: para cada comprobacion, sus variables (CSR).
    std::vector<uint32_t> chk_ptr, chk_var;
    // Y para cada variable, sus comprobaciones (CSR).
    std::vector<uint32_t> var_ptr, var_chk;
    double rate() const { return n ? 1.0 - (double)m / n : 0.0; }
    uint32_t edges() const { return (uint32_t)chk_var.size(); }
};

// Familia de codigos madre (n fijo, tasas 0.50 .. 0.95). Deterministas.
constexpr uint32_t LDPC_N = 16384;
const std::vector<double> &ldpc_family_rates();
// Construye (y guarda en cache) el codigo madre de tasa 'rate' y longitud n.
std::shared_ptr<const LdpcCode> ldpc_code(double rate, uint32_t n = LDPC_N);
// Distribucion de grados de variable (nodos): m-1 nodos de grado 2 en
// escalera (estructura IRA, sin ciclos entre ellos), una fraccion 'frac_high'
// de grado 'd_high' y el resto de grado 3.
struct LdpcDegrees {
    double frac_high = 0.25;
    uint32_t d_high = 12;
};
LdpcDegrees ldpc_default_degrees(double rate);
LdpcCode ldpc_build(uint32_t n, uint32_t m, uint64_t seed, const LdpcDegrees &deg);

// Sindrome H x (x de n bits).
Bits ldpc_syndrome(const LdpcCode &c, const Bits &x);
// Perforado "untainted" (Elkouss et al., 2012): recorre las variables en el
// orden de 'order' y acepta una si ninguna de sus comprobaciones toca ya a otra
// perforada, de modo que cada bit perforado se recupera en la 1.a iteracion.
// Devuelve hasta 'max_p' posiciones.
std::vector<uint32_t> ldpc_untainted_puncturing(const LdpcCode &c, const std::vector<uint32_t> &order, uint32_t max_p);
// true si el codigo no tiene ciclos de longitud 4.
bool ldpc_girth_at_least_6(const LdpcCode &c);

enum class LdpcAlgo { SUM_PRODUCT, MIN_SUM };

struct LdpcDecodeResult {
    bool ok = false;
    uint32_t iterations = 0;
};

// Decodifica en 'x' (entrada: LLR a priori por bit, positivo = 0).
LdpcDecodeResult ldpc_decode(const LdpcCode &c, const Bits &syndrome, const std::vector<float> &llr, Bits &x,
                             uint32_t max_iter = 100, LdpcAlgo algo = LdpcAlgo::SUM_PRODUCT,
                             float min_sum_alpha = 0.8f);

// ----------------------------------------------- reconciliacion por tramas ----
struct LdpcFramePlan {
    double rate = 0;     // codigo madre
    uint32_t k = 0;      // bits de clave en la trama
    uint32_t p = 0;      // perforados
    uint32_t s = 0;      // acortados
    uint32_t leak() const;
};

// Elige codigo madre, k, p y s para reconciliar hasta 'k_avail' bits con QBER
// estimado 'q' y eficiencia objetivo 'f'. k = 0 si no hay plan posible.
LdpcFramePlan ldpc_plan_frame(uint32_t k_avail, double q, double f, uint32_t n = LDPC_N);

struct LdpcConfig {
    double qber_hint = 0.02;       // QBER inicial (despues se adapta trama a trama)
    // Eficiencia objetivo. Medido (n = 16384, Q = 1.72 %, untainted puncturing):
    // FER 13 % con f = 1.20, 8 % con 1.25, 2.5 % con 1.35 y 0/124 con 1.40.
    // 1.40 maximiza la clave neta (las tramas que fallan se pierden enteras).
    double f_target = 1.40;
    uint32_t max_iter = 100;
    LdpcAlgo algo = LdpcAlgo::SUM_PRODUCT;
    uint32_t n = LDPC_N;
};

// Simulacion local (sin canal autenticado): Alice y Bob en el mismo proceso.
// Los bits de las tramas que fallan se descartan (no se usan como clave), por
// lo que su sindrome no filtra nada sobre la clave que queda.
struct LdpcReconcileResult {
    Bits alice, bob;               // claves conservadas (corregidas en Bob)
    uint32_t frames = 0, failed = 0;
    uint32_t corrected = 0;        // errores corregidos (en tramas con exito)
    uint64_t leaked_bits = 0;      // sum(m - p) de las tramas conservadas
    uint32_t discarded_bits = 0;
    double max_iter_seen = 0;
};

// alice_random(n, out): n bits aleatorios privados de Alice (bits perforados).
// seed: semilla publica (posiciones de perforado y acortado).
struct LdpcIo;
LdpcReconcileResult ldpc_reconcile(const Bits &alice, const Bits &bob, const LdpcConfig &cfg, uint64_t seed,
                                   const std::function<bool(size_t, Bits &)> &alice_random,
                                   LdpcIo *io = nullptr);

// Ganchos del canal clasico (autenticacion / atacante). Todos opcionales.
struct LdpcIo {
    // Mensaje de Alice a Bob (plan + sindrome): Bob recibe lo que devuelva.
    std::function<std::vector<uint8_t>(const std::vector<uint8_t> &)> alice_to_bob;
    // Respuesta de Bob (exito + errores): Alice recibe lo que devuelva.
    std::function<std::vector<uint8_t>(const std::vector<uint8_t> &)> bob_to_alice;
};

} // namespace aurora

#endif
