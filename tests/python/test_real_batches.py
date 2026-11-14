"""
Validacion de la cadena C++ sobre los batches REALES del receptor.

No hay ground truth, asi que se combinan dos tipos de comprobacion:
  1. Equivalencia: la cadena C++ (kernels HLS + PS) da exactamente lo mismo
     que un modelo de referencia independiente en Python.
  2. Plausibilidad fisica ("en tiempo de vuelo"): magnitudes que deben ser
     estables y razonables en un enlace QKD sano (lag, anchura del pico,
     QBER, ratio senal/accidentales, uso del batch completo).

Variables de entorno:
  AURORA_CLI      ruta a aurora_cli          (por defecto build/aurora_cli)
  AURORA_BATCHES  directorio de batches      (por defecto batches)
  AURORA_MAX_BATCHES  limita el numero de batches (por defecto todos)
"""
from __future__ import annotations

import json
import os
import statistics
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aurora_ref as ref  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("AURORA_CLI", ROOT / "build" / "aurora_cli"))
if not CLI.exists() and CLI.with_suffix(".exe").exists():   # Windows
    CLI = CLI.with_suffix(".exe")
BATCHES = Path(os.environ.get("AURORA_BATCHES", ROOT / "batches"))
import re as _re
HALF_WINDOW = int(_re.search(r"DEFAULT_HALF_WINDOW_PS\s*=\s*(\d+)",
                             (Path(__file__).resolve().parents[2] / "sw" / "aurora_host.h").read_text()).group(1))

# Umbrales de plausibilidad (ajustables segun el montaje experimental).
# Observado en las capturas reales: cada batch tiene su propio desfase
# (~77 ps rms entre batches, sin correlacion entre batches consecutivos), muy
# por encima del error estadistico del centroide (~7 ps). Dentro de un batch
# el lag es estable. => hay que sincronizar CADA batch (no reutilizar el lag).
MAX_LAG_SPREAD_PS = 300      # salto maximo entre batches respecto a la mediana
MAX_INTRA_BATCH_DRIFT_PS = 60  # variacion del lag entre cuartos de un batch
SIGMA_RANGE_PS = (100, 350)  # anchura del pico de coincidencias
MAX_BATCH_QBER = 0.06
MAX_GLOBAL_QBER = 0.03
MIN_SIGNAL_TO_ACCIDENTALS = 10.0
SIFT_RATIO_RANGE = (0.40, 0.60)


def batch_ids():
    ids = sorted(int(p.name[5:-len("_alice.detections")]) for p in BATCHES.glob("batch*_alice.detections"))
    limit = os.environ.get("AURORA_MAX_BATCHES")
    return ids[: int(limit)] if limit else ids


class RealBatches(unittest.TestCase):
    cpp: dict = {}
    data: dict = {}

    @classmethod
    def setUpClass(cls):
        if not CLI.exists():
            raise unittest.SkipTest(f"no existe {CLI}; ejecuta 'make' primero")
        ids = batch_ids()
        if not ids:
            raise unittest.SkipTest(f"no hay batches en {BATCHES}")
        for i in ids:
            prefix = BATCHES / f"batch{i}"
            out = subprocess.run([str(CLI), "batch", str(prefix), str(HALF_WINDOW)], capture_output=True, text=True)
            cls.cpp[i] = json.loads(out.stdout)
            cls.data[i] = ref.load_batch(prefix)

    # ------------------------------------------------------- equivalencia --
    def test_cpp_lag_matches_python_reference(self):
        for i, r in self.cpp.items():
            ta, _, tb, _ = self.data[i]
            py = ref.estimate_lag(ta, tb)
            with self.subTest(batch=i):
                self.assertTrue(r["ok"], f"sync C++ no valido: {r}")
                self.assertAlmostEqual(r["lag"], py.lag, delta=5.0)
                self.assertAlmostEqual(r["sigma"], py.sigma, delta=10.0)

    def test_cpp_sift_identical_to_python_reference(self):
        for i, r in self.cpp.items():
            ta, sa, tb, sb = self.data[i]
            py = ref.sift(ta, sa, tb, sb, r["lag_ps"], HALF_WINDOW)
            with self.subTest(batch=i):
                self.assertEqual(r["coincidences"], py.coincidences)
                self.assertEqual(r["sifted"], len(py.alice_bits))
                self.assertEqual(r["alice_key"], ref.bits_to_hex(py.alice_bits))
                self.assertEqual(r["bob_key"], ref.bits_to_hex(py.bob_bits))

    # ------------------------------------------------------- plausibilidad --
    def test_lag_is_stable_across_batches(self):
        lags = [r["lag"] for r in self.cpp.values()]
        med = statistics.median(lags)
        for i, r in self.cpp.items():
            with self.subTest(batch=i):
                self.assertLess(abs(r["lag"] - med), MAX_LAG_SPREAD_PS)

    def test_lag_is_stable_within_batch(self):
        """Sin deriva de reloj apreciable dentro de un batch (lag constante valido)."""
        for i in list(self.cpp)[::5]:
            ta, _, tb, _ = self.data[i]
            edges = [tb[0] + (tb[-1] - tb[0]) * k // 4 for k in range(5)]
            lags = []
            for k in range(4):
                sel_b = (tb >= edges[k]) & (tb < edges[k + 1])
                sel_a = (ta >= edges[k] - 10**6) & (ta < edges[k + 1] + 10**6)
                lags.append(ref.estimate_lag(ta[sel_a], tb[sel_b]).lag)
            with self.subTest(batch=i, lags=[round(x) for x in lags]):
                self.assertLess(max(lags) - min(lags), MAX_INTRA_BATCH_DRIFT_PS)

    def test_peak_width_is_physical(self):
        for i, r in self.cpp.items():
            with self.subTest(batch=i):
                self.assertGreater(r["sigma"], SIGMA_RANGE_PS[0])
                self.assertLess(r["sigma"], SIGMA_RANGE_PS[1])
                # La ventana por defecto debe cubrir al menos ~1.5 sigma.
                self.assertGreater(HALF_WINDOW, 1.5 * r["sigma"])

    def test_qber_is_below_threshold(self):
        tot = err = 0
        for i, r in self.cpp.items():
            with self.subTest(batch=i):
                self.assertGreater(r["sifted"], 0)
                self.assertLess(r["errors"] / r["sifted"], MAX_BATCH_QBER)
            tot += r["sifted"]
            err += r["errors"]
        self.assertLess(err / tot, MAX_GLOBAL_QBER)

    def test_coincidences_well_above_accidentals(self):
        for i, r in self.cpp.items():
            ta, _, tb, _ = self.data[i]
            acc = ref.expected_accidentals(ta, tb, HALF_WINDOW)
            with self.subTest(batch=i):
                self.assertGreater(r["coincidences"], MIN_SIGNAL_TO_ACCIDENTALS * acc)

    def test_sifting_ratio_is_about_half(self):
        for i, r in self.cpp.items():
            with self.subTest(batch=i):
                ratio = r["sifted"] / r["coincidences"]
                self.assertGreater(ratio, SIFT_RATIO_RANGE[0])
                self.assertLess(ratio, SIFT_RATIO_RANGE[1])

    def test_whole_batch_is_used(self):
        """La tasa de coincidencias es uniforme en el tiempo (no hay truncado)."""
        for i, r in list(self.cpp.items())[:20]:
            ta, sa, tb, sb = self.data[i]
            py = ref.sift(ta, sa, tb, sb, r["lag_ps"], HALF_WINDOW)
            t = tb[py.matched_bob]
            q = (t - tb[0]) * 4 // (tb[-1] - tb[0] + 1)
            per_quarter = [int((q == k).sum()) for k in range(4)]
            with self.subTest(batch=i, quarters=per_quarter):
                self.assertGreater(min(per_quarter), 0.7 * max(per_quarter))
                self.assertEqual(r["chunks"], 1 if max(len(ta), len(tb)) <= 65536 else r["chunks"])

    def test_alice_and_bob_keys_have_same_length(self):
        for i, r in self.cpp.items():
            with self.subTest(batch=i):
                self.assertEqual(len(r["alice_key"]), len(r["bob_key"]))


class FullPipeline(unittest.TestCase):
    def test_end_to_end_pipeline_produces_identical_final_keys(self):
        if not CLI.exists():
            self.skipTest("sin aurora_cli")
        ids = batch_ids()
        if not ids:
            self.skipTest("sin batches")
        out = subprocess.run([str(CLI), "pipeline", str(BATCHES), str(ids[0]), str(ids[-1])],
                             capture_output=True, text=True)
        self.assertEqual(out.returncode, 0, out.stdout + out.stderr)
        self.assertIn("claves finales iguales: SI", out.stdout)
        self.assertIn("autenticacion        : OK", out.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
