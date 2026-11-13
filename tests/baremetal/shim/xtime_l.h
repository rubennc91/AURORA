// SIMULACION EN PC: sustituye a xtime_l.h.
#pragma once
#include <chrono>
#include <cstdint>
typedef uint64_t XTime;
#define COUNTS_PER_SECOND 1000000000ull
inline void XTime_GetTime(XTime *t) {
    *t = (XTime)std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}
