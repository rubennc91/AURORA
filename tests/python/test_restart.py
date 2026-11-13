"""Tests del analisis del test de reinicios del TRNG (tools/restart_test.py)."""
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import restart_test as rt  # noqa: E402


def matrix(n_rows, seed=1, p1=0.5, stuck_cols=0):
    rng = random.Random(seed)
    rows = [[1 if rng.random() < p1 else 0 for _ in range(rt.N_SAMPLES)] for _ in range(n_rows)]
    for r in rows:                       # arranque determinista en las primeras columnas
        for c in range(stuck_cols):
            r[c] = c & 1
    return rows


class RestartAnalysis(unittest.TestCase):
    def test_row_text_roundtrip(self):
        rows = matrix(3)
        text = "ruido\n" + "\n".join(rt.row_to_line(i, r) for i, r in enumerate(rows)) + "\nAURORA_END\n"
        self.assertEqual(rt.parse_rows(text), rows)

    def test_row_bit_order_msb_first(self):
        bits = rt.parse_rows("R 0 8" + "0" * 249)[0]
        self.assertEqual(bits[:4], [1, 0, 0, 0])
        self.assertEqual(sum(bits), 1)

    def test_rejects_short_row(self):
        with self.assertRaises(ValueError):
            rt.parse_rows("R 0 ff")

    def test_cutoff_is_binomial_critical_value(self):
        # H_I = 1: p = 0.5, alpha = 2.5e-6 -> media 500 + ~4.56 sigma (15.8) = ~572
        u = rt.sanity_cutoff(1.0)
        self.assertTrue(568 <= u <= 576, u)
        # Menos entropia declarada -> umbral mas alto (mas permisivo)
        self.assertGreater(rt.sanity_cutoff(0.5), rt.sanity_cutoff(0.785228))
        # P(X > U) <= alpha < P(X > U - 1)
        import math
        p, n, alpha = 0.5, 1000, 0.01 / 4000
        tail = lambda u: sum(math.exp(rt.log_binom_pmf(n, k, p)) for k in range(u + 1, n + 1))
        self.assertLessEqual(tail(u), alpha)
        self.assertGreater(tail(u - 1), alpha)

    def test_ideal_source_passes(self):
        ok, report = rt.analyze(matrix(1000), 0.785228)
        self.assertTrue(ok, report)

    def test_biased_but_within_h_i_passes(self):
        ok, _ = rt.analyze(matrix(1000, p1=0.479), 0.785228)   # sesgo medido en la placa
        self.assertTrue(ok)

    def test_deterministic_startup_fails_and_reports_transient(self):
        ok, report = rt.analyze(matrix(1000, stuck_cols=5), 0.785228)
        self.assertFalse(ok)
        self.assertIn("ultima columna mala es la 4", report)

    def test_outputs_matrix_for_nist(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            rows = matrix(1000)
            rt.write_outputs(rows, Path(d), 0.785228)
            data = (Path(d) / "matrix.bin").read_bytes()
            self.assertEqual(len(data), 1000 * 1000)
            self.assertEqual(list(data[1000:2000]), rows[1])   # orden por filas


if __name__ == "__main__":
    unittest.main()
