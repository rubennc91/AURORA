"""Simulacion RTL del calefactor (rtl/heater/heater_axi.v) con Icarus Verilog."""
import os
import shutil
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
IVERILOG = os.environ.get("IVERILOG", "iverilog")
VVP = os.environ.get("VVP", "vvp")


@unittest.skipUnless(shutil.which(IVERILOG) and shutil.which(VVP), "sin Icarus Verilog")
class HeaterRtl(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out = ROOT / "build" / "tb_heater"
        out.parent.mkdir(exist_ok=True)
        src = [ROOT / "rtl/trng/trng_ro.v", ROOT / "rtl/heater/heater_axi.v", ROOT / "tests/heater/tb_heater.v"]
        subprocess.run([IVERILOG, "-g2005", "-DSIMULATION", "-o", str(out), *map(str, src)], check=True)
        log = subprocess.run([VVP, "-n", str(out)], capture_output=True, text=True, timeout=600).stdout
        cls.r = {}
        for line in log.splitlines():
            parts = line.split()
            if len(parts) >= 2:
                cls.r[parts[0]] = parts[1]
        cls.log = log

    def test_identification(self):
        self.assertEqual(self.r["ID"], "48454154")
        self.assertEqual(self.r["CONFIG"], "00030303")   # 3 osciladores/banco, 3 bancos, 3 etapas
        self.assertEqual(self.r["MASK_RESET"], "00000000")

    def test_banks_oscillate_only_when_enabled(self):
        self.assertEqual(self.r["ACT_OFF_CHANGES"], "0")
        self.assertEqual(self.r["MASK_SET"], "00000002")
        self.assertGreater(int(self.r["ACT_ON_CHANGES"]), 5)
        self.assertNotIn("OTHER_BANK_ACTIVE", self.log)

    def test_watchdog(self):
        # Con refrescos sigue encendido; sin ellos se apaga solo y lo indica.
        self.assertEqual(self.r["MASK_KICKED"], "00000002")
        self.assertEqual(self.r["TRIPPED_KICKED"], "0")
        self.assertEqual(self.r["MASK_WDOG"], "00000000")
        self.assertEqual(self.r["TRIPPED_WDOG"], "1")
        self.assertEqual(self.r["WDOG"], "5000")
        self.assertEqual(self.r["TRIPPED_CLEARED"], "0")
        self.assertIn("DONE", self.log)


if __name__ == "__main__":
    unittest.main()
