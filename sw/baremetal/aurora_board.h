// =============================================================================
// AURORA - Configuracion de la placa para la aplicacion bare-metal
// =============================================================================
// Direcciones de build/vivado/address_map.txt (block design de create_vivado.tcl).
// Si se cambia el block design, actualizar aqui.
// =============================================================================
#ifndef AURORA_BOARD_H
#define AURORA_BOARD_H

#include <cstdint>

namespace aurora {
namespace board {
constexpr uintptr_t TRNG_BASE     = 0xA0000000u;
constexpr uintptr_t SYNC_BASE     = 0xA0010000u;
constexpr uintptr_t SIFT_BASE     = 0xA0020000u;
constexpr uintptr_t TOEPLITZ_BASE = 0xA0030000u;

// Espera maxima de un kernel (iteraciones de sondeo del registro de control).
constexpr uint64_t KERNEL_TIMEOUT_POLLS = 2000000000ull;
} // namespace board

// Estado del backend: true si algun kernel no termino a tiempo.
extern bool hw_kernel_timeout;
} // namespace aurora

#endif
