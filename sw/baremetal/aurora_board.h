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
// Asignada por Vivado al anadir el kernel LDPC: comprobar en address_map.txt.
constexpr uintptr_t LDPC_BASE     = 0xA0040000u;

// Buzon en DDR (fuera del codigo, la pila y el heap) que XSCT escribe antes de
// lanzar la aplicacion para elegir el modo (hw/scripts/run_board.tcl):
//   +0 MAILBOX_MAGIC_RESTART -> test de reinicios del TRNG (SP 800-90B 3.1.4)
//   +4 numero de reinicios   +8 muestras descartadas tras cada arranque
//   +12 tiempo apagado entre reinicios (us)
// Cualquier otro valor -> prueba completa. La aplicacion lo borra al leerlo.
constexpr uintptr_t MAILBOX               = 0x7FFF0000u;
constexpr uint32_t  MAILBOX_MAGIC_RESTART = 0x52535452u;   // "RSTR"

// Sensores de temperatura (AMS de Zynq UltraScale+, base 0xFFA50000):
// SYSMON del PS en +0x800 y del PL en +0xC00; registro de temperatura en +0x00.
// Solo como termometro: no se usan como fuente de entropia.
constexpr uintptr_t AMS_PS_TEMP = 0xFFA50800u;
constexpr uintptr_t AMS_PL_TEMP = 0xFFA50C00u;

// Espera maxima de un kernel (iteraciones de sondeo del registro de control).
constexpr uint64_t KERNEL_TIMEOUT_POLLS = 2000000000ull;
} // namespace board

// Estado del backend: true si algun kernel no termino a tiempo.
extern bool hw_kernel_timeout;
} // namespace aurora

// Modelo software del kernel LDPC (sw/baremetal/ldpc_sw_model.cpp).
void sw_aurora_ldpc_dec(const uint32_t *chk_ptr, const uint16_t *chk_var, uint32_t n, uint32_t m, uint32_t load_code,
                        const int8_t *llr, const uint8_t *syndrome, uint32_t max_iter, uint8_t *x_out,
                        uint32_t *status);

#endif
