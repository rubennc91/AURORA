#!/usr/bin/env python3
"""
Genera build/baremetal_src/aurora_data.cpp para la aplicacion bare-metal:
batches reales con sus resultados esperados (calculados con aurora_cli en el
PC), un caso de Toeplitz con su resultado calculado aqui con la definicion, la
semilla publica del extractor del TRNG y una clave precompartida de pruebas.

Uso: python3 tools/make_baremetal_data.py [--batches 0,1,2] [--out build/baremetal_src]
"""
from __future__ import annotations

import argparse
import json
import os
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / "python"))
import aurora_ref as ref  # noqa: E402


def cli_path() -> Path:
    p = Path(os.environ.get("AURORA_CLI", ROOT / "build" / "aurora_cli"))
    if not p.exists() and p.with_suffix(".exe").exists():
        p = p.with_suffix(".exe")
    if not p.exists():
        sys.exit(f"no existe {p}: ejecuta 'make' primero")
    return p


def carray(ctype: str, name: str, values, per_line: int = 16, static: bool = True) -> str:
    vals = [str(int(v)) + ("ULL" if ctype == "uint64_t" else "") for v in values]
    lines = [", ".join(vals[i:i + per_line]) for i in range(0, len(vals), per_line)]
    body = ",\n    ".join(lines) if lines else "0"
    st = "static " if static else ""
    return f"{st}const {ctype} {name}[] = {{\n    {body}\n}};\n"


def bits_from_hex_file(path: Path) -> list[int]:
    lines = path.read_text().split("\n")
    n = int(lines[0].split()[2])
    bits = []
    for line in lines[1:]:
        line = line.strip()
        if line:
            w = int(line, 16)
            bits.extend((w >> b) & 1 for b in range(31, -1, -1))
    return bits[:n]


def toeplitz_reference(x: list[int], seed: list[int], m: int) -> list[int]:
    n = len(x)
    out = []
    for i in range(m):
        acc = 0
        for j in range(n):
            if x[j] and seed[i - j + n - 1]:
                acc ^= 1
        out.append(acc)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--batches", default="0,1,2")
    ap.add_argument("--out", default=str(ROOT / "build" / "baremetal_src"))
    ap.add_argument("--batches-dir", default=str(ROOT / "batches"))
    args = ap.parse_args()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    cli = cli_path()

    parts = ['// GENERADO por tools/make_baremetal_data.py: no editar.\n#include "aurora_data.h"\n\n']
    entries = []
    for i in [int(x) for x in args.batches.split(",") if x != ""]:
        prefix = Path(args.batches_dir) / f"batch{i}"
        ta, sa, tb, sb = ref.load_batch(prefix)
        r = json.loads(subprocess.run([str(cli), "batch", str(prefix), "350"], capture_output=True, text=True).stdout)
        if not r.get("ok"):
            sys.exit(f"batch{i}: la cadena del PC no da resultado valido: {r}")
        parts.append(carray("uint64_t", f"b{i}_at", ta, 8))
        parts.append(carray("uint8_t", f"b{i}_as", sa, 40))
        parts.append(carray("uint64_t", f"b{i}_bt", tb, 8))
        parts.append(carray("uint8_t", f"b{i}_bs", sb, 40))
        entries.append(f'    {{"batch{i}", {len(ta)}u, b{i}_at, b{i}_as, {len(tb)}u, b{i}_bt, b{i}_bs, '
                       f'{r["lag_ps"]}LL, {r["coincidences"]}u, {r["sifted"]}u, "{r["alice_key"]}", "{r["bob_key"]}"}}')
        print(f"batch{i}: {len(ta)} Alice, {len(tb)} Bob, lag {r['lag_ps']} ps, {r['sifted']} bits cribados")
    parts.append("const BmBatch bm_batches[] = {\n" + ",\n".join(entries) + "\n};\n")
    parts.append(f"const uint32_t bm_n_batches = {len(entries)};\n\n")

    # Claves cribadas de todos los batches (cadena completa).
    def hex_to_bits(h: str, n: int) -> list[int]:
        bits = []
        for ch in h:
            v = int(ch, 16)
            bits.extend((v >> j) & 1 for j in range(4))
        return bits[:n]
    ka, kb = [], []
    all_ids = sorted(int(p.name[5:-len("_alice.detections")]) for p in Path(args.batches_dir).glob("batch*_alice.detections"))
    for i in all_ids:
        r = json.loads(subprocess.run([str(cli), "batch", str(Path(args.batches_dir) / f"batch{i}"), "350"],
                                      capture_output=True, text=True).stdout)
        if not r.get("ok"):
            continue
        ka += hex_to_bits(r["alice_key"], r["sifted"])
        kb += hex_to_bits(r["bob_key"], r["sifted"])
    parts.append(f"const uint32_t bm_sifted_len = {len(ka)};\n")
    parts.append(carray("uint8_t", "bm_sifted_alice", ka, 64, static=False))
    parts.append(carray("uint8_t", "bm_sifted_bob", kb, 64, static=False))
    print(f"claves cribadas de {len(all_ids)} batches: {len(ka)} bits")

    # Toeplitz: entrada y semilla aleatorias, resultado por la definicion.
    rng = random.Random(20261113)
    n, m = 2000, 1300
    x = [rng.getrandbits(1) for _ in range(n)]
    seed = [rng.getrandbits(1) for _ in range(n + m - 1)]
    exp = toeplitz_reference(x, seed, m)
    parts.append(f"const uint32_t bm_toep_n_in = {n}, bm_toep_n_out = {m};\n")
    parts.append(carray("uint8_t", "bm_toep_in", x, 64, static=False))
    parts.append(carray("uint8_t", "bm_toep_seed", seed, 64, static=False))
    parts.append(f'const char *bm_toep_expected = "{ref.bits_to_hex(exp)}";\n\n')

    # Semilla publica del extractor (instalacion) y PSK de pruebas.
    seed_file = ROOT / "build" / "extractor_seed.txt"
    if not seed_file.exists():
        subprocess.run([str(cli), "gen-random", "extractor", str(seed_file)], check=True)
    es = bits_from_hex_file(seed_file)
    parts.append(f"const uint32_t bm_extractor_seed_len = {len(es)};\n")
    parts.append(carray("uint8_t", "bm_extractor_seed", es, 64, static=False))
    psk = [random.SystemRandom().getrandbits(1) for _ in range(4096)]
    parts.append(f"const uint32_t bm_psk_len = {len(psk)};\n")
    parts.append(carray("uint8_t", "bm_psk", psk, 64, static=False))

    (out_dir / "aurora_data.cpp").write_text("".join(parts))
    print(f"escrito {out_dir / 'aurora_data.cpp'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
