"""
Contraste del hash polinomico GF(2^128) de la autenticacion (sw/aurora_auth.cpp)
con una implementacion independiente en Python basada en enteros.

Convencion: bloque de 16 bytes en big-endian -> entero; el bit i es el
coeficiente de x^i; reduccion x^128 = x^7 + x^2 + x + 1; bloque final con la
longitud del mensaje en bits.
"""
from __future__ import annotations

import os
import random
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("AURORA_CLI", ROOT / "build" / "aurora_cli"))
if not CLI.exists() and CLI.with_suffix(".exe").exists():
    CLI = CLI.with_suffix(".exe")

MASK = (1 << 128) - 1
POLY = (1 << 128) | 0x87


def gf_mul(a: int, b: int) -> int:
    """Producto sin acarreo y reduccion polinomica (escrito de forma distinta al C++)."""
    p = 0
    while b:
        if b & 1:
            p ^= a
        a <<= 1
        b >>= 1
    for bit in range(p.bit_length() - 1, 127, -1):
        if p >> bit & 1:
            p ^= POLY << (bit - 128)
    return p & MASK


def poly_hash(k: int, msg: bytes) -> int:
    h = 0
    for off in range(0, len(msg), 16):
        blk = msg[off:off + 16].ljust(16, b"\0")
        h = gf_mul(h ^ int.from_bytes(blk, "big"), k)
    return gf_mul(h ^ (len(msg) * 8), k)


class PolyHashReference(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CLI.exists():
            raise unittest.SkipTest(f"no existe {CLI}; ejecuta 'make' primero")

    def cli(self, k: int, msg: bytes) -> int:
        out = subprocess.run([str(CLI), "polyhash", f"{k:032x}", msg.hex()], capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stderr)
        return int(out.stdout.strip(), 16)

    def test_field_reduction(self):
        self.assertEqual(gf_mul(1 << 127, 2), 0x87)

    def test_cli_matches_python(self):
        rng = random.Random(2024)
        lengths = [0, 1, 15, 16, 17, 31, 32, 33, 100, 1000]
        for n in lengths:
            k = rng.getrandbits(128)
            msg = bytes(rng.getrandbits(8) for _ in range(n))
            with self.subTest(len=n):
                self.assertEqual(self.cli(k, msg), poly_hash(k, msg))

    def test_edge_keys(self):
        for k in (1, MASK, 1 << 127):
            with self.subTest(k=hex(k)):
                self.assertEqual(self.cli(k, b"AURORA"), poly_hash(k, b"AURORA"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
