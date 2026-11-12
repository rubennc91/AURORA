// Utilidades comunes de los tests legacy: carga de batches reales y
// resultados de referencia de la cadena nueva para comparar.
#ifndef AURORA_LEGACY_COMMON_H
#define AURORA_LEGACY_COMMON_H

#include <cstdlib>
#include <string>
#include "../../sw/aurora_host.h"

inline std::string batches_dir() {
    const char *e = std::getenv("AURORA_BATCHES");
    return e ? e : "batches";
}

inline bool load_real_batch(int i, aurora::Detections &a, aurora::Detections &b) {
    return aurora::load_batch(batches_dir() + "/batch" + std::to_string(i), a, b);
}

#endif
