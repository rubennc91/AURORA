"""Respuestas conocidas de NIST SP 800-22 rev1a (ejemplos de la propia norma)."""
from __future__ import annotations

import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import nist_sts as sts  # noqa: E402


def b(s: str):
    return [int(c) for c in s]


class KnownAnswers(unittest.TestCase):
    def test_frequency(self):
        self.assertAlmostEqual(sts.frequency(b("1011010101")), 0.527089, places=6)

    def test_block_frequency(self):
        self.assertAlmostEqual(sts.block_frequency(b("0110011010"), 3), 0.801252, places=6)

    def test_runs(self):
        self.assertAlmostEqual(sts.runs(b("1001101011")), 0.147232, places=6)

    def test_cumulative_sums(self):
        self.assertAlmostEqual(sts.cumulative_sums(b("1011010111")), 0.4116588, places=6)

    def test_approximate_entropy(self):
        self.assertAlmostEqual(sts.approximate_entropy(b("0100110101"), 3), 0.261961, places=6)

    def test_serial(self):
        p1, p2 = sts.serial(b("0011011101"), 3)
        self.assertAlmostEqual(p1, 0.808792, places=6)
        self.assertAlmostEqual(p2, 0.670320, places=6)

    def test_igamc(self):
        self.assertAlmostEqual(sts.igamc(1.0, 2.0), 0.1353352832, places=9)   # e^-2
        self.assertAlmostEqual(sts.igamc(0.5, 2.0), 0.0455002639, places=9)   # erfc(sqrt(2))


class Behaviour(unittest.TestCase):
    def test_rejects_obviously_bad_sequences(self):
        n = 20000
        for name, bits in (("ceros", [0] * n), ("alterna", [i & 1 for i in range(n)])):
            with self.subTest(name):
                self.assertTrue(any(r.p < 0.001 for r in sts.run_all(bits)))

    def test_accepts_good_generator_in_proportion(self):
        rng = random.Random(7)
        fails = {}
        for _ in range(20):
            bits = [rng.getrandbits(1) for _ in range(20000)]
            for r in sts.run_all(bits):
                fails[r.name] = fails.get(r.name, 0) + (r.p < 0.01)
        for name, f in fails.items():
            with self.subTest(name):
                self.assertLessEqual(f, 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
