#!/bin/sh
# Falla si el codigo que va a la placa usa funciones de std:: que la libstdc++
# bare-metal de Xilinx no tiene (C99 en std::). Usar aurora::compat::*.
FILES="sw/aurora_host.cpp sw/aurora_auth.cpp sw/aurora_trng.cpp sw/aurora_trng_driver.cpp sw/baremetal/*.cpp sw/*.h sw/baremetal/*.h hw/aurora_kernels.h"
PAT='std::(round|llround|lround|log2|lgamma|log1p|expm1|cbrt|hypot|isnan|isinf|isfinite|fmin|fmax|trunc|nearbyint|exp2|strto[a-z]+|to_string|sto[a-z]+|snprintf|vsnprintf|sscanf|atoll|llabs)\('
if grep -nE "$PAT" $FILES | grep -v "aurora_compat.h"; then
    echo "ERROR: funciones std:: no disponibles en bare-metal (usar aurora::compat)" >&2
    exit 1
fi
echo "check_baremetal_std: OK"
