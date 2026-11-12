"""
Verificacion del RTL del TRNG (rtl/trng/) con Icarus Verilog.

Simula el testbench (tb_trng.v) y contrasta lo que hace el hardware con
modelos de referencia independientes:
  * Tests de salud: indice exacto de la muestra que dispara cada alarma, en el
    RTL, en el modelo C++ (aurora_cli trng-health) y en un modelo Python.
  * Camino de datos: las palabras que salen por la FIFO son exactamente los
    bits brutos aceptados tras el arranque, en orden MSB primero.
  * Calidad: estimacion de min-entropia (SP 800-90B) de los bits brutos y
    bateria NIST SP 800-22 sobre la salida del extractor (criterio de
    proporcion sobre varias semillas).

El modelo de simulacion de los osciladores es de comportamiento: valida la
logica digital, NO la entropia real (hay que medirla con capturas de la placa).

Variables: AURORA_CLI, IVERILOG, VVP. Se omite si no hay Icarus Verilog.
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import nist_sts as sts  # noqa: E402

CLI = Path(os.environ.get("AURORA_CLI", ROOT / "build" / "aurora_cli"))
if not CLI.exists() and CLI.with_suffix(".exe").exists():
    CLI = CLI.with_suffix(".exe")
IVERILOG = os.environ.get("IVERILOG", "iverilog")
VVP = os.environ.get("VVP", "vvp")
SIM = ROOT / "build" / "trng_sim"
RTL = [ROOT / "rtl" / "trng" / f for f in ("trng_ro.v", "trng_source.v", "trng_health.v", "trng_axi.v", "tb_trng.v")]
NONE = 0xFFFFFFFF


def golden_health(bits, rct_cut: int, apt_cut: int, window: int = 1024):
    """Modelo Python independiente de RCT/APT (SP 800-90B 4.4)."""
    rct_idx = apt_idx = -1
    a = b = 0
    apt_a = apt_b = 0
    for i, x in enumerate(bits):
        if b and x == a:
            b += 1
            if b >= rct_cut and rct_idx < 0:
                rct_idx = i
        else:
            a, b = x, 1
        if i % window == 0:
            apt_a, apt_b = x, 1
        elif x == apt_a:
            apt_b += 1
            if apt_b >= apt_cut and apt_idx < 0:
                apt_idx = i
    return rct_idx, apt_idx


def load_raw(path: Path):
    """'<indice> <bit>' por linea; se queda con la ultima secuencia desde 0."""
    bits = []
    for line in path.read_text().split("\n"):
        if not line.strip():
            continue
        idx, bit = map(int, line.split())
        if idx == 0:
            bits = []
        assert idx == len(bits), f"indice discontinuo en {path}"
        bits.append(bit)
    return bits


class TrngRtl(unittest.TestCase):
    log: list[str] = []
    idx: dict = {}

    @classmethod
    def setUpClass(cls):
        if not (shutil.which(IVERILOG) and shutil.which(VVP)):
            raise unittest.SkipTest("Icarus Verilog no instalado")
        if not CLI.exists():
            raise unittest.SkipTest(f"no existe {CLI}; ejecuta 'make' primero")
        if SIM.exists():
            shutil.rmtree(SIM)
        SIM.mkdir(parents=True)
        vvp_file = SIM / "tb.vvp"
        subprocess.run([IVERILOG, "-g2005", "-DSIMULATION", "-o", str(vvp_file), *map(str, RTL)], check=True)
        subprocess.run([VVP, "-n", str(vvp_file), f"+outdir={SIM}"], check=True, timeout=1800,
                       stdout=subprocess.DEVNULL)
        cls.log = (SIM / "tb_log.txt").read_text().splitlines()
        for line in cls.log:
            if line.startswith("IDX "):
                _, phase, r, a, n, rc, ac = line.split()
                cls.idx[phase] = dict(rct=int(r), apt=int(a), samples=int(n), rct_cut=int(rc), apt_cut=int(ac))

    def test_testbench_checks_pass(self):
        checks = [l for l in self.log if l.startswith("CHECK ")]
        self.assertGreaterEqual(len(checks), 22)
        for c in checks:
            with self.subTest(c.split()[1]):
                self.assertEqual(c.split()[2], "PASS", c)
        self.assertIn("TB_RESULT PASS", self.log)

    def test_expected_alarm_per_phase(self):
        expect = {"normal": (False, False), "recover": (False, False), "stuck": (True, False),
                  "ro_off": (True, False), "biased_apt": (False, True)}
        for phase, (rct, apt) in expect.items():
            with self.subTest(phase):
                self.assertEqual(self.idx[phase]["rct"] != NONE, rct)
                self.assertEqual(self.idx[phase]["apt"] != NONE, apt)
        self.assertTrue(self.idx["biased"]["rct"] != NONE or self.idx["biased"]["apt"] != NONE)

    def test_rtl_health_matches_reference_models(self):
        for phase, v in self.idx.items():
            bits = load_raw(SIM / f"raw_{phase}.txt")
            py_rct, py_apt = golden_health(bits, v["rct_cut"], v["apt_cut"])
            out = subprocess.run([str(CLI), "trng-health", str(SIM / f"raw_{phase}.txt"), str(v["rct_cut"]),
                                  str(v["apt_cut"])], capture_output=True, text=True, check=True)
            cpp = json.loads(out.stdout)
            rtl_rct = -1 if v["rct"] == NONE else v["rct"]
            rtl_apt = -1 if v["apt"] == NONE else v["apt"]
            with self.subTest(phase):
                self.assertEqual((rtl_rct, rtl_apt), (py_rct, py_apt))
                self.assertEqual((cpp["rct_idx"], cpp["apt_idx"]), (py_rct, py_apt))

    def test_fifo_output_is_the_accepted_raw_stream(self):
        raw = load_raw(SIM / "raw_normal.txt")
        words = [int(l, 16) for l in (SIM / "words_normal.txt").read_text().split()]
        out_bits = [(w >> b) & 1 for w in words for b in range(31, -1, -1)]
        # Los bits empiezan a aceptarse justo tras el arranque (muestra 1024 o la siguiente).
        starts = [k for k in range(1020, 1040) if raw[k:k + len(out_bits)] == out_bits]
        self.assertTrue(starts, "la salida de la FIFO no coincide con los bits brutos")
        self.assertGreaterEqual(starts[0], 1024)

    def test_raw_min_entropy_estimate(self):
        out = subprocess.run([str(CLI), "trng-analyze", "--raw-indexed", str(SIM / "raw_normal.txt")],
                             capture_output=True, text=True)
        r = json.loads(out.stdout)
        self.assertEqual(out.returncode, 0, out.stdout)
        # El modelo de simulacion se diseno con jitter suficiente: H_min >= 0.5 asumido.
        self.assertGreaterEqual(r["h_min"], 0.5, r)

    def test_extracted_output_passes_nist_in_proportion(self):
        fails: dict[str, int] = {}
        n_seeds = 10
        for seed in range(1, n_seeds + 1):
            ext = SIM / f"extracted_{seed}.txt"
            subprocess.run([str(CLI), "trng-analyze", "--raw-indexed", str(SIM / "raw_normal.txt"),
                            "--seed", str(seed * 7919), "--extract", str(ext)], capture_output=True, check=True)
            bits = sts.load_words(str(ext))
            self.assertGreater(len(bits), 10000)
            for r in sts.run_all(bits):
                fails[r.name] = fails.get(r.name, 0) + (r.p < 0.01)
        for name, f in fails.items():
            with self.subTest(name):
                self.assertLessEqual(f, 2, f"{f}/{n_seeds} semillas fallan")


if __name__ == "__main__":
    unittest.main(verbosity=2)
