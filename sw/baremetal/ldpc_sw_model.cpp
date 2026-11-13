// =============================================================================
// AURORA - Modelo SOFTWARE del kernel LDPC en el PS (para comparar con la PL)
// =============================================================================
// En la placa, aurora_ldpc_dec lo implementa hw_backend.cpp (kernel de la PL).
// Aqui se compila el MISMO fuente del kernel con otro nombre, de modo que la
// aplicacion puede ejecutar ambos y comparar resultados (bit a bit) y tiempos.
// =============================================================================
#define aurora_ldpc_dec sw_aurora_ldpc_dec
#include "aurora_ldpc_dec.cpp"
