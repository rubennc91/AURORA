// =============================================================================
// AURORA - Autenticacion del canal clasico (Wegman-Carter)
// =============================================================================
// Sin autenticacion del canal clasico, un atacante en medio puede suplantar a
// Alice o a Bob y la QKD no protege nada. Esquema usado:
//
//   tag = PolyHash_k(mensaje) XOR r
//
//   * PolyHash_k: evaluacion polinomica sobre GF(2^128) (como GHASH), con
//     polinomio de reduccion x^128 + x^7 + x^2 + x + 1. Es una familia
//     eps-AXU con eps <= (L+1) / 2^128 para mensajes de L bloques de 16 bytes.
//   * k (128 bits): clave del hash, secreta, puede reutilizarse en la sesion.
//   * r (128 bits): clave de un solo uso (OTP). NUNCA se reutiliza.
//
// Ambas claves salen de una reserva de clave secreta compartida (KeyPool):
// al principio una clave precompartida, y despues se rellena con clave QKD.
// Alice y Bob consumen la reserva exactamente en el mismo orden; cada etiqueta
// lleva el offset de clave usado para detectar desincronizacion o repeticion.
//
// Representacion: un U128 es el entero (hi << 64 | lo); el bit i es el
// coeficiente de x^i. Los bloques de 16 bytes se leen en big-endian.
// =============================================================================
#ifndef AURORA_AUTH_H
#define AURORA_AUTH_H

#include <cstdint>
#include <string>
#include <vector>

#include "aurora_host.h"

namespace aurora {

struct U128 {
    uint64_t hi = 0, lo = 0;
    bool operator==(const U128 &o) const { return hi == o.hi && lo == o.lo; }
    bool operator!=(const U128 &o) const { return !(*this == o); }
    U128 operator^(const U128 &o) const { return {hi ^ o.hi, lo ^ o.lo}; }
    bool is_zero() const { return (hi | lo) == 0; }
};

U128 gf128_mul(U128 a, U128 b);
U128 poly_hash(U128 k, const uint8_t *msg, size_t len);
U128 u128_from_bytes(const uint8_t *b16);          // big-endian
std::string u128_hex(U128 v);                      // 32 digitos hex
bool u128_from_hex(const std::string &s, U128 &out);
bool ct_equal(U128 a, U128 b);                     // comparacion en tiempo constante

// ------------------------------------------------------------ reserva -------
class KeyPool {
public:
    KeyPool() = default;
    explicit KeyPool(const Bits &initial) { add(initial); }
    // Extrae n bits consecutivos. Devuelve false (sin consumir) si no hay suficientes.
    bool take(size_t n, Bits &out);
    bool take_u128(U128 &out);
    void add(const Bits &bits);
    size_t available() const { return bits_.size() - pos_; }
    uint64_t offset() const { return base_ + pos_; }   // bits consumidos desde el inicio
private:
    Bits bits_;
    size_t pos_ = 0;
    uint64_t base_ = 0;
};

// ------------------------------------------------------- transcripciones ----
// Serializacion canonica de los mensajes de una fase del protocolo.
struct Transcript {
    std::vector<uint8_t> bytes;
    void u8(uint8_t v) { bytes.push_back(v); }
    void u32(uint32_t v) { for (int i = 3; i >= 0; --i) bytes.push_back((uint8_t)(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 7; i >= 0; --i) bytes.push_back((uint8_t)(v >> (8 * i))); }
    void raw(const std::vector<uint8_t> &v) { u64(v.size()); bytes.insert(bytes.end(), v.begin(), v.end()); }
    void clear() { bytes.clear(); }
};

enum Direction : uint8_t { ALICE_TO_BOB = 0xA1, BOB_TO_ALICE = 0xB0 };

struct AuthTag {
    U128 value;
    uint64_t key_offset = 0;   // offset de la OTP en la reserva del emisor
};

// Un extremo (Alice o Bob). La clave del hash se toma de la reserva al crear
// el objeto; despues cada tag/verify consume 128 bits de OTP.
class Authenticator {
public:
    explicit Authenticator(KeyPool &pool);
    bool ready() const { return ready_; }
    // Firma (direccion, secuencia, mensaje). False si no queda clave.
    bool tag(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg, AuthTag &out);
    // Verifica. Consume la OTP aunque falle (nunca se reutiliza). Rechaza si el
    // offset no coincide con la posicion local (repeticion o desincronizacion).
    bool verify(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg, const AuthTag &t);
    uint64_t key_bits_consumed() const { return consumed_; }
private:
    U128 mac(Direction dir, uint64_t seq, const std::vector<uint8_t> &msg) const;
    KeyPool &pool_;
    U128 k_;
    bool ready_ = false;
    uint64_t consumed_ = 0;
};

// ------------------------------------------- post-procesado autenticado ------
// Interfaz del atacante para los tests: puede alterar lo que viaja por el
// canal clasico. Por defecto no toca nada.
struct ChannelAttacker {
    virtual ~ChannelAttacker() = default;
    virtual uint8_t parity(uint64_t index, uint8_t p) { (void)index; return p; }
    virtual void message(int phase, std::vector<uint8_t> &msg) { (void)phase; (void)msg; }
    virtual void tag(int phase, AuthTag &t) { (void)phase; (void)t; }
};

enum Phase : int { PHASE_PARAMS = 0, PHASE_CASCADE = 1, PHASE_VERIFY = 2, PHASE_PA = 3 };

struct AuthenticatedResult {
    bool ok = false;               // true solo si TODAS las etiquetas verificaron
    std::string abort_reason;
    uint32_t sifted = 0;
    uint32_t corrected = 0;
    uint64_t leaked_ec = 0;
    uint32_t pa_length = 0;        // salida de la amplificacion de privacidad
    uint64_t auth_key_bits = 0;    // clave consumida por la autenticacion (cada lado)
    int64_t net_key_bits = 0;      // pa_length - auth_key_bits
    Bits alice_key, bob_key;       // clave neta entregada (vacia si !ok)
};

struct AuthenticatedConfig {
    CascadeConfig cascade;
    int64_t lag_ps = 0;
    uint32_t half_window = 350;
    uint64_t verify_seed = 0x5EED5EED12345678ULL;   // en el sistema real: QRNG/TRNG
    uint64_t pa_seed = 0xC0FFEE0123456789ULL;       // en el sistema real: QRNG/TRNG
    bool refill_pools = true;      // reponer la reserva con clave nueva
};

// Ejecuta Cascade -> verificacion -> PA con autenticacion de cada fase en ambos
// sentidos. Las reservas de Alice y Bob deben empezar identicas.
AuthenticatedResult run_authenticated_postprocessing(const Bits &alice_sifted, const Bits &bob_sifted,
                                                     KeyPool &alice_pool, KeyPool &bob_pool,
                                                     const AuthenticatedConfig &cfg,
                                                     ChannelAttacker *attacker = nullptr);

} // namespace aurora

#endif
