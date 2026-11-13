// =============================================================================
// AURORA - Fuente de aleatoriedad: generador del sistema operativo
// =============================================================================
#include "aurora_random.h"

#include <random>

namespace aurora {

bool os_random_bits(size_t n, Bits &out, std::string *err) {
    try {
        std::random_device rd;
        // Algunas implementaciones antiguas (MinGW < 9.2) eran deterministas e
        // informaban entropia 0: se rechazan.
        if (rd.entropy() == 0.0) {
            if (err) *err = "std::random_device sin entropia en esta plataforma";
            return false;
        }
        out.resize(n);
        for (size_t i = 0; i < n; i += 32) {
            uint32_t w = rd();
            for (size_t j = 0; j < 32 && i + j < n; ++j) out[i + j] = (w >> j) & 1;
        }
        return true;
    } catch (const std::exception &e) {
        if (err) *err = e.what();
        return false;
    }
}

} // namespace aurora
