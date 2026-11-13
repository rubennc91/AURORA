"""Modo de integracion 'aurora_cli stream' (carpeta vigilada; el cliente de la API escribe ahi los batches)."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = os.environ.get("AURORA_CLI", str(ROOT / "build" / "aurora_cli"))
BATCHES = Path(os.environ.get("AURORA_BATCHES", ROOT / "batches"))


@unittest.skipUnless(Path(CLI).exists() and (BATCHES / "batch0_alice.detections").exists(), "falta CLI o batches")
class StreamMode(unittest.TestCase):
    def run_stream(self, folder, *args):
        out = Path(folder) / "claves.txt"
        r = subprocess.run([CLI, "stream", str(folder), "--once", "--out", str(out), *args],
                           capture_output=True, text=True, timeout=600)
        self.assertEqual(r.returncode, 0, r.stderr)
        return r.stdout, (out.read_text().splitlines() if out.exists() else [])

    def test_all_batches_one_block(self):
        with tempfile.TemporaryDirectory() as d:
            for f in BATCHES.glob("batch*_*.detections"):
                shutil.copy(f, d)
            log, keys = self.run_stream(d, "--block-bits", "40000")
            self.assertIn("BLOQUE 1: OK", log)
            self.assertIn("101 batches (0 descartados)", log)
            self.assertEqual(len(keys), 1)
            n = int(keys[0].split()[3])
            self.assertGreater(n, 27000)                     # pipeline sobre los mismos datos: ~28.400
            self.assertEqual(len(keys[0].split()[4]), (n + 3) // 4)

    def test_processes_in_natural_order_and_skips_incomplete(self):
        with tempfile.TemporaryDirectory() as d:
            for i in (10, 2, 1):
                for who in ("alice", "bob"):
                    shutil.copy(BATCHES / f"batch{i}_{who}.detections", d)
            shutil.copy(BATCHES / "batch3_alice.detections", d)   # sin el de Bob: se espera
            log, _ = self.run_stream(d, "--block-bits", "100000")
            order = [l.split(":")[0] for l in log.splitlines() if l.startswith("batch")]
            self.assertEqual(order, ["batch1", "batch2", "batch10"])

    def test_corrupt_batch_is_discarded(self):
        with tempfile.TemporaryDirectory() as d:
            for who in ("alice", "bob"):
                shutil.copy(BATCHES / f"batch0_{who}.detections", d)
            (Path(d) / "roto_alice.detections").write_text("(12,9)(5,1)")
            (Path(d) / "roto_bob.detections").write_text("(1,0)")
            log, _ = self.run_stream(d)
            self.assertIn("roto: descartado", log)
            self.assertIn("2 batches (1 descartados)", log)


if __name__ == "__main__":
    unittest.main()
