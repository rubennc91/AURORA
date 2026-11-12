#!/usr/bin/env python3
"""
Subconjunto de la bateria estadistica NIST SP 800-22 rev1a (sin dependencias).

Tests: frequency (monobit), block frequency, runs, longest run of ones,
cumulative sums (adelante/atras), approximate entropy y serial.

Uso:
    python3 tools/nist_sts.py <captura.txt> [--alpha 0.01]
donde la captura tiene una palabra hexadecimal de 32 bits por linea (MSB
primero), como la generan el testbench del TRNG y el volcado del driver.

IMPORTANTE: pasar estos tests NO demuestra que una fuente tenga entropia
(un generador determinista tambien los pasa). Sirven para detectar defectos
groseros; la entropia se evalua con los estimadores de SP 800-90B
(aurora_cli trng-analyze) sobre los bits BRUTOS.
"""
from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass


# ------------------------------------------------- funciones especiales -----
def igamc(a: float, x: float) -> float:
    """Funcion gamma incompleta superior regularizada Q(a, x)."""
    if x <= 0:
        return 1.0
    if x < a + 1.0:
        return 1.0 - _gser(a, x)
    return _gcf(a, x)


def _gser(a: float, x: float) -> float:
    ap, s, d = a, 1.0 / a, 1.0 / a
    for _ in range(10000):
        ap += 1.0
        d *= x / ap
        s += d
        if abs(d) < abs(s) * 1e-15:
            break
    return s * math.exp(-x + a * math.log(x) - math.lgamma(a))


def _gcf(a: float, x: float) -> float:
    tiny = 1e-300
    b = x + 1.0 - a
    c = 1.0 / tiny
    d = 1.0 / b
    h = d
    for i in range(1, 10000):
        an = -i * (i - a)
        b += 2.0
        d = an * d + b
        d = tiny if abs(d) < tiny else d
        c = b + an / c
        c = tiny if abs(c) < tiny else c
        d = 1.0 / d
        de = d * c
        h *= de
        if abs(de - 1.0) < 1e-15:
            break
    return math.exp(-x + a * math.log(x) - math.lgamma(a)) * h


def _phi(x: float) -> float:
    return 0.5 * math.erfc(-x / math.sqrt(2.0))


# ------------------------------------------------------------------ tests ---
def frequency(bits) -> float:
    n = len(bits)
    s = sum(2 * b - 1 for b in bits)
    return math.erfc(abs(s) / math.sqrt(n) / math.sqrt(2.0))


def block_frequency(bits, m: int = 128) -> float:
    n_blocks = len(bits) // m
    chi2 = 4.0 * m * sum((sum(bits[i * m:(i + 1) * m]) / m - 0.5) ** 2 for i in range(n_blocks))
    return igamc(n_blocks / 2.0, chi2 / 2.0)


def runs(bits) -> float:
    n = len(bits)
    pi = sum(bits) / n
    if abs(pi - 0.5) >= 2.0 / math.sqrt(n):
        return 0.0
    v = 1 + sum(1 for i in range(n - 1) if bits[i] != bits[i + 1])
    return math.erfc(abs(v - 2 * n * pi * (1 - pi)) / (2 * math.sqrt(2 * n) * pi * (1 - pi)))


_LONGEST = {
    8: (3, [1, 2, 3, 4], [0.2148, 0.3672, 0.2305, 0.2031]),
    128: (5, [4, 5, 6, 7, 8, 9], [0.1174, 0.2430, 0.2493, 0.1752, 0.1027, 0.1124]),
    10000: (6, [10, 11, 12, 13, 14, 15, 16], [0.0882, 0.2092, 0.2483, 0.1933, 0.1208, 0.0675, 0.0727]),
}


def longest_run(bits) -> float:
    n = len(bits)
    if n < 128:
        raise ValueError("longest_run necesita n >= 128")
    m = 8 if n < 6272 else (128 if n < 750000 else 10000)
    k, classes, pis = _LONGEST[m]
    nb = n // m
    counts = [0] * (k + 1)
    for i in range(nb):
        best = cur = 0
        for b in bits[i * m:(i + 1) * m]:
            cur = cur + 1 if b else 0
            best = max(best, cur)
        idx = 0
        while idx < k and best > classes[idx]:
            idx += 1
        counts[idx] += 1
    chi2 = sum((counts[i] - nb * pis[i]) ** 2 / (nb * pis[i]) for i in range(k + 1))
    return igamc(k / 2.0, chi2 / 2.0)


def cumulative_sums(bits, reverse: bool = False) -> float:
    seq = list(reversed(bits)) if reverse else bits
    n = len(seq)
    s = z = 0
    for b in seq:
        s += 2 * b - 1
        z = max(z, abs(s))
    if z == 0:
        return 0.0
    sq = math.sqrt(n)
    s1 = 0.0
    for k in range(int((-n / z + 1) / 4), int((n / z - 1) / 4) + 1):
        s1 += _phi((4 * k + 1) * z / sq) - _phi((4 * k - 1) * z / sq)
    s2 = 0.0
    for k in range(int((-n / z - 3) / 4), int((n / z - 1) / 4) + 1):
        s2 += _phi((4 * k + 3) * z / sq) - _phi((4 * k + 1) * z / sq)
    return 1.0 - s1 + s2


def _pattern_counts(bits, m: int):
    n = len(bits)
    ext = list(bits) + list(bits[:m - 1]) if m > 1 else list(bits)
    counts = [0] * (1 << m)
    for i in range(n):
        v = 0
        for j in range(m):
            v = (v << 1) | ext[i + j]
        counts[v] += 1
    return counts


def approximate_entropy(bits, m: int = 2) -> float:
    n = len(bits)

    def phi(mm):
        if mm == 0:
            return 0.0
        return sum((c / n) * math.log(c / n) for c in _pattern_counts(bits, mm) if c)

    apen = phi(m) - phi(m + 1)
    chi2 = 2.0 * n * (math.log(2) - apen)
    return igamc(2 ** (m - 1), chi2 / 2.0)


def serial(bits, m: int = 3):
    n = len(bits)

    def psi2(mm):
        if mm <= 0:
            return 0.0
        return (2 ** mm / n) * sum(c * c for c in _pattern_counts(bits, mm)) - n

    p0, p1, p2 = psi2(m), psi2(m - 1), psi2(m - 2)
    return igamc(2 ** (m - 2), (p0 - p1) / 2.0), igamc(2 ** (m - 3), (p0 - 2 * p1 + p2) / 2.0)


# --------------------------------------------------------------- informe ----
@dataclass
class Result:
    name: str
    p: float


def run_all(bits) -> list[Result]:
    n = len(bits)
    out = [
        Result("frequency", frequency(bits)),
        Result("block_frequency(M=128)", block_frequency(bits, 128)),
        Result("runs", runs(bits)),
        Result("longest_run", longest_run(bits)),
        Result("cusum_forward", cumulative_sums(bits)),
        Result("cusum_backward", cumulative_sums(bits, reverse=True)),
    ]
    # m recomendado: m < floor(log2 n) - 5
    m = max(2, min(8, int(math.log2(n)) - 6))
    out.append(Result(f"approximate_entropy(m={m})", approximate_entropy(bits, m)))
    p1, p2 = serial(bits, m + 1)
    out.append(Result(f"serial_1(m={m + 1})", p1))
    out.append(Result(f"serial_2(m={m + 1})", p2))
    return out


def load_words(path: str):
    bits = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            w = int(line, 16)
            bits.extend((w >> b) & 1 for b in range(31, -1, -1))
    return bits


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture")
    ap.add_argument("--alpha", type=float, default=0.01)
    args = ap.parse_args()
    bits = load_words(args.capture)
    print(f"{len(bits)} bits")
    ok = True
    for r in run_all(bits):
        passed = r.p >= args.alpha
        ok &= passed
        print(f"  {r.name:28s} p = {r.p:.6f}  {'PASS' if passed else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
