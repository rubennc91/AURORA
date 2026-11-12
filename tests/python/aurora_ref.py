"""
AURORA - Modelo de referencia independiente en Python (numpy).

Implementacion "de libro", escrita sin mirar los kernels HLS, para validar
la cadena C++ (hw/ + sw/) sobre los batches reales, donde no hay ground truth.

Convenciones (identicas a hw/aurora_kernels.h):
  * lag = t_alice - t_bob  (ps)
  * estado 0=H 1=V 2=D 3=A ; base = estado >> 1 ; bit = estado & 1
  * claves en hex: cada nibble son 4 bits, LSB primero
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np

_PAIR = re.compile(rb"\((\d+),(\d+)\)")


def load_detections(path: str | Path) -> tuple[np.ndarray, np.ndarray]:
    data = Path(path).read_bytes()
    pairs = _PAIR.findall(data)
    if not pairs:
        return np.zeros(0, np.int64), np.zeros(0, np.uint8)
    arr = np.array(pairs, dtype=np.int64)
    return arr[:, 0], arr[:, 1].astype(np.uint8)


def load_batch(prefix: str | Path):
    prefix = str(prefix)
    ta, sa = load_detections(prefix + "_alice.detections")
    tb, sb = load_detections(prefix + "_bob.detections")
    return ta, sa, tb, sb


def pair_differences(ta: np.ndarray, tb: np.ndarray, lo: int, hi: int) -> np.ndarray:
    """Todas las diferencias d = ta - tb con lo <= d < hi."""
    start = np.searchsorted(ta, tb + lo, side="left")
    stop = np.searchsorted(ta, tb + hi, side="left")
    counts = stop - start
    out = []
    for k in range(int(counts.max(initial=0))):
        sel = counts > k
        out.append(ta[start[sel] + k] - tb[sel])
    return np.concatenate(out) if out else np.zeros(0, np.int64)


@dataclass
class Lag:
    lag: float
    sigma: float
    signal: float
    snr: float
    background_per_ps: float


def estimate_lag(ta, tb, search=524_288, bin_ps=8, signal_half=600, guard=1200, fine_half=4096) -> Lag:
    """Pico del histograma de diferencias, centroide con el fondo restado."""
    if len(ta) == 0 or len(tb) == 0:
        return Lag(float("nan"), 0, 0, 0, 0)
    d = pair_differences(ta, tb, -search, search)
    if len(d) == 0:
        return Lag(float("nan"), 0, 0, 0, 0)
    coarse_h, coarse_e = np.histogram(d, bins=np.arange(-search, search + 1, 1024))
    c = coarse_e[np.argmax(coarse_h)] + 512
    fine = d[(d >= c - fine_half) & (d < c + fine_half)]
    edges = np.arange(c - fine_half, c + fine_half + 1, bin_ps)
    h, e = np.histogram(fine, bins=edges)
    centers = e[:-1] + bin_ps / 2
    center = centers[np.argmax(h)]
    lag = Lag(center, 0, 0, 0, 0)
    for _ in range(3):
        bg = h[np.abs(centers - center) > guard].mean()
        sel = np.abs(centers - center) <= signal_half
        v = h[sel] - bg
        s0 = v.sum()
        if s0 <= 0:
            break
        mu = (v * centers[sel]).sum() / s0
        var = (v * centers[sel] ** 2).sum() / s0 - mu * mu
        lag = Lag(mu, float(np.sqrt(max(var, 0))), float(s0), float(s0 / np.sqrt(h[sel].sum())), bg / bin_ps)
        center = mu
    return lag


@dataclass
class Sift:
    coincidences: int
    alice_bits: list
    bob_bits: list
    matched_bob: np.ndarray   # indices de Bob emparejados


def sift(ta, sa, tb, sb, lag: int, half_window: int) -> Sift:
    """Emparejamiento voraz: cada Bob toma el primer Alice libre en la ventana."""
    ai, na = 0, len(ta)
    ka, kb, matched = [], [], []
    ta_l, tb_l = ta.tolist(), tb.tolist()
    lo_off, hi_off = lag - half_window, lag + half_window
    for j, t in enumerate(tb_l):
        lo = t + lo_off
        while ai < na and ta_l[ai] < lo:
            ai += 1
        if ai < na and ta_l[ai] <= t + hi_off:
            matched.append(j)
            a_s, b_s = int(sa[ai]), int(sb[j])
            if (a_s >> 1) == (b_s >> 1):
                ka.append(a_s & 1)
                kb.append(b_s & 1)
            ai += 1
    return Sift(len(matched), ka, kb, np.array(matched, dtype=np.int64))


def bits_to_hex(bits) -> str:
    out = []
    for k in range(0, len(bits), 4):
        v = 0
        for j, b in enumerate(bits[k:k + 4]):
            v |= int(b) << j
        out.append("0123456789abcdef"[v])
    return "".join(out)


def expected_accidentals(ta, tb, half_window: int) -> float:
    """Coincidencias accidentales esperadas: N_bob * tasa_alice * 2w."""
    if len(ta) < 2:
        return 0.0
    rate_a = len(ta) / float(ta[-1] - ta[0])
    return len(tb) * rate_a * 2 * half_window
