#!/usr/bin/env python3
"""Convierte una captura del TRNG (palabras hex de 32 bits, lineas '#' ignoradas)
al formato de NIST ea_non_iid: un byte (0/1) por muestra, bit mas antiguo primero.

    python3 tools/trng_to_bin.py build/trng_frio.txt build/trng_frio.bin
    ~/ea/cpp/ea_non_iid -v build/trng_frio.bin 1
"""
import sys

if len(sys.argv) != 3:
    sys.exit(__doc__)
out = bytearray()
temps = []
for line in open(sys.argv[1]):
    line = line.strip()
    if line.startswith("# TEMP"):
        temps.append(line[2:])
    if not line or line.startswith("#"):
        continue
    w = int(line, 16)
    out += bytes((w >> (31 - i)) & 1 for i in range(32))
open(sys.argv[2], "wb").write(out)
print(f"{len(out)} muestras -> {sys.argv[2]}")
for t in temps:
    print(t)
