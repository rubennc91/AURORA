// =============================================================================
// AURORA - Funciones portables (PC y bare-metal)
// =============================================================================
// La libstdc++ del compilador bare-metal de Xilinx (aarch64-none-elf, newlib)
// esta configurada sin las funciones C99 en std:: (std::round, std::log2,
// std::lgamma, std::to_string, std::stoul, std::snprintf...). El codigo que va
// a la placa usa estas versiones, iguales en el PC y en la placa (misma
// aritmetica). tools/check_baremetal_std.sh vigila que no vuelvan a usarse.
// =============================================================================
#ifndef AURORA_COMPAT_H
#define AURORA_COMPAT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <cstdint>
#include <string>

namespace aurora {
namespace compat {

constexpr double LN2 = 0.693147180559945309417232121458176568;

inline double log2(double x) { return ::log(x) / LN2; }
inline double round(double x) { return x < 0 ? -::floor(-x + 0.5) : ::floor(x + 0.5); }
inline int64_t llround(double x) { return (int64_t)round(x); }
inline double log1p(double x) {
    // log(1+x) preciso tambien para |x| pequeno.
    const double u = 1.0 + x;
    return (u == 1.0) ? x : ::log(u) * x / (u - 1.0);
}
// ln Gamma(x) para x > 0 (Lanczos, g = 7, n = 9): error relativo ~1e-15.
inline double lgamma(double x) {
    static const double c[9] = {0.99999999999980993,  676.5203681218851,     -1259.1392167224028,
                                771.32342877765313,   -176.61502916214059,   12.507343278686905,
                                -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7};
    if (x < 0.5) return ::log(3.14159265358979323846 / ::fabs(::sin(3.14159265358979323846 * x))) - lgamma(1.0 - x);
    x -= 1.0;
    double a = c[0];
    const double t = x + 7.5;
    for (int i = 1; i < 9; ++i) a += c[i] / (x + i);
    return 0.91893853320467274178 + (x + 0.5) * ::log(t) - t + ::log(a);
}

inline std::string to_string(long long v) {
    char buf[32];
    ::snprintf(buf, sizeof buf, "%lld", v);
    return buf;
}
inline std::string to_string(unsigned long long v) {
    char buf[32];
    ::snprintf(buf, sizeof buf, "%llu", v);
    return buf;
}
inline std::string to_string(int v) { return to_string((long long)v); }
inline std::string to_string(unsigned v) { return to_string((unsigned long long)v); }
inline std::string to_string(long v) { return to_string((long long)v); }
inline std::string to_string(unsigned long v) { return to_string((unsigned long long)v); }

} // namespace compat
} // namespace aurora

#endif
