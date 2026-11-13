// SIMULACION EN PC: sustituye a xil_io.h del BSP de Xilinx.
#pragma once
#include <cstdint>
uint32_t mock_read32(uintptr_t addr);
void mock_write32(uintptr_t addr, uint32_t v);
inline uint32_t Xil_In32(uintptr_t a) { return mock_read32(a); }
inline void Xil_Out32(uintptr_t a, uint32_t v) { mock_write32(a, v); }
