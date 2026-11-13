// SIMULACION EN PC: sustituye a xil_cache.h (sin cache que gestionar).
#pragma once
#include <cstdint>
typedef intptr_t INTPTR;
extern unsigned long mock_cache_ops;
inline void Xil_DCacheFlushRange(INTPTR, INTPTR) { ++mock_cache_ops; }
inline void Xil_DCacheInvalidateRange(INTPTR, INTPTR) { ++mock_cache_ops; }
