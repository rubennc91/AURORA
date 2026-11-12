"""
Fallos documentados a partir del informe de sintesis (Vitis HLS 2023.2) del
kernel anterior hls_sift. El informe se guarda en tests/data/ porque los
productos generados de HLS ya no se versionan.

Los tests marcados con expectedFailure documentan un fallo conocido.
"""
from __future__ import annotations

import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

REPORT = Path(__file__).resolve().parents[1] / "data" / "legacy_hls_sift_csynth.xml"
BATCH_DURATION_S = 25e-3   # un batch real cubre ~25 ms de adquisicion


def _summary():
    root = ET.parse(REPORT).getroot()
    perf = root.find("PerformanceEstimates")
    timing = root.find("UserAssignments")
    return {
        "target_ns": float(timing.findtext("TargetClockPeriod")),
        "uncertainty_ns": float(timing.findtext("ClockUncertainty")),
        "estimated_ns": float(perf.find("SummaryOfTimingAnalysis").findtext("EstimatedClockPeriod")),
        "worst_latency": int(perf.find("SummaryOfOverallLatency").findtext("Worst-caseLatency")),
    }


class LegacySynthesisReport(unittest.TestCase):
    def test_report_is_readable(self):
        s = _summary()
        self.assertGreater(s["worst_latency"], 0)

    @unittest.expectedFailure
    def test_legacy_sync_meets_realtime(self):
        """SYNC por fuerza bruta: 2.16e9 ciclos = 21.6 s por batch de 25 ms."""
        s = _summary()
        latency_s = s["worst_latency"] * s["target_ns"] * 1e-9
        self.assertLessEqual(latency_s, BATCH_DURATION_S, f"{latency_s:.2f} s por batch")

    @unittest.expectedFailure
    def test_legacy_timing_has_margin(self):
        """Estimacion 9.62 ns > presupuesto efectivo (10 - 2.7 = 7.3 ns)."""
        s = _summary()
        self.assertLessEqual(s["estimated_ns"], s["target_ns"] - s["uncertainty_ns"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
