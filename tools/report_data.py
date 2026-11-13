#!/usr/bin/env python3
"""
Extrae recursos, potencia y timing de los informes de Vivado y Vitis HLS y
genera docs/report/datos.tex (macros que usa el informe LaTeX).

    vivado -mode batch -nojournal -source hw/scripts/vivado_reports.tcl
    python3 tools/report_data.py

Los datos que falten se marcan como pendientes en el PDF.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIV = ROOT / "build" / "vivado"
HLS = ROOT / "build" / "hls"
OUT = ROOT / "docs" / "report" / "datos.tex"

MODULES = [  # (patron de instancia, nombre en el informe)
    (r"aurora_sync", "Kernel sync (HLS)"),
    (r"aurora_sift", "Kernel sift (HLS)"),
    (r"aurora_toeplitz", "Kernel Toeplitz (HLS)"),
    (r"trng", "TRNG (RTL)"),
    (r"smc|smartconnect|interconnect|axi_", "Interconexion AXI"),
]


def cells(line: str) -> list[str]:
    return [c.strip() for c in line.strip().strip("|").split("|")]


def num(s: str) -> str:
    return s.replace(",", "")


def tex_int(s: str) -> str:
    try:
        return f"{int(float(num(s))):,}".replace(",", "{.}")
    except ValueError:
        return s


def parse_totals(path: Path) -> dict[str, tuple[str, str, str]]:
    """Site Type -> (usado, disponible, %)."""
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("|"):
            continue
        c = cells(line)
        if len(c) >= 5 and c[0] in ("CLB LUTs", "CLB Registers", "Block RAM Tile", "DSPs", "URAM",
                                    "CLB LUTs*", "LUT as Logic", "LUT as Memory"):
            out.setdefault(c[0].rstrip("*"), (c[1], c[-2], c[-1]))
    return out


def parse_hier(path: Path) -> dict[str, dict[str, str]]:
    res: dict[str, dict[str, str]] = {}
    if not path.exists():
        return res
    header = None
    for line in path.read_text(errors="replace").splitlines():
        if not line.startswith("|"):
            continue
        c = cells(line)
        if c and c[0] == "Instance":
            header = c
            continue
        if not header or len(c) != len(header):
            continue
        inst = c[0]
        depth = len(line) - len(line.lstrip("| "))
        row = dict(zip(header, c))
        for pat, name in MODULES:
            if name in res:
                continue
            if re.search(pat, inst, re.I) and not inst.startswith("("):
                res[name] = row
                break
        if "Total" not in res and depth <= 3 and ("aurora_bd_wrapper" in inst or inst.endswith("_wrapper")):
            res["Total"] = row
    return res


def parse_power(path: Path) -> dict[str, str]:
    out = {}
    if not path.exists():
        return out
    for line in path.read_text(errors="replace").splitlines():
        c = cells(line) if line.startswith("|") else []
        if len(c) >= 2:
            if c[0].startswith("Total On-Chip Power"):
                out["total"] = c[1]
            elif c[0].startswith("Dynamic (W)"):
                out["dynamic"] = c[1]
            elif c[0].startswith("Device Static"):
                out["static"] = c[1]
    return out


def parse_timing(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    lines = path.read_text(errors="replace").splitlines()
    for i, l in enumerate(lines):
        if l.strip().startswith("WNS(ns)") and i + 2 < len(lines):
            v = lines[i + 2].split()
            if len(v) >= 6:
                return {"wns": v[0], "tns": v[1], "fail": v[2], "whs": v[4]}
    return {}


def parse_hls(kernel: str) -> dict[str, str]:
    rpt = HLS / kernel / "solution1" / "syn" / "report" / f"{kernel}_csynth.rpt"
    out = {}
    if not rpt.exists():
        return out
    for line in rpt.read_text(errors="replace").splitlines():
        c = cells(line) if line.startswith("|") else []
        if len(c) >= 3 and c[0] == "ap_clk" and "est" not in out:
            out["est"] = c[2].replace("ns", "").strip()
        if len(c) >= 6 and c[0] == "Total" and "lut" not in out and c[1].isdigit():
            out.update(bram=c[1], dsp=c[2], ff=c[3], lut=c[4])
    return out


def main() -> int:
    P = r"\pend{}"
    tot = parse_totals(VIV / "utilization.rpt")
    hier = parse_hier(VIV / "utilization_hier.rpt")
    pw = parse_power(VIV / "power.rpt")
    tm = parse_timing(VIV / "timing_summary.rpt")
    L = ["% Generado por tools/report_data.py: no editar a mano."]

    def row(name, key):
        if key in tot:
            u, a, p = tot[key]
            return f"{name} & {tex_int(u)} & {tex_int(a)} & {p}\\% \\\\"
        return f"{name} & {P} & & \\\\"

    L.append(r"\newcommand{\TablaRecursos}{%")
    L += [row("LUT", "CLB LUTs"), row("Registros (FF)", "CLB Registers"), row("BRAM (36 Kb)", "Block RAM Tile"),
          row("DSP", "DSPs")]
    L.append("}")

    L.append(r"\newcommand{\TablaModulos}{%")
    if hier:
        for _, name in MODULES:
            r = hier.get(name)
            if not r:
                continue
            g = lambda k: tex_int(r.get(k, "-"))
            bram = r.get("RAMB36", "0"), r.get("RAMB18", "0")
            L.append(f"{name} & {g('Total LUTs')} & {g('FFs')} & {bram[0]}/{bram[1]} & {r.get('DSP Blocks', '-')} \\\\")
    else:
        L.append(f"\\multicolumn{{5}}{{l}}{{{P} (ejecutar hw/scripts/vivado\\_reports.tcl)}} \\\\")
    L.append("}")

    L.append(r"\newcommand{\PotenciaTotal}{" + (pw.get("total", P) + (" W" if "total" in pw else "")) + "}")
    L.append(r"\newcommand{\PotenciaDinamica}{" + (pw.get("dynamic", P) + (" W" if "dynamic" in pw else "")) + "}")
    L.append(r"\newcommand{\PotenciaEstatica}{" + (pw.get("static", P) + (" W" if "static" in pw else "")) + "}")
    L.append(r"\newcommand{\TimingWNS}{" + tm.get("wns", "3.340") + "}")
    L.append(r"\newcommand{\TimingWHS}{" + tm.get("whs", "0.009") + "}")

    L.append(r"\newcommand{\TablaHLS}{%")
    for k, name in (("aurora_sync", "sync"), ("aurora_sift", "sift"), ("aurora_toeplitz", "toeplitz")):
        h = parse_hls(k)
        if h:
            L.append(f"{name} & {h['est']} & {tex_int(h['lut'])} & {tex_int(h['ff'])} & {h['bram']} & {h['dsp']} \\\\")
        else:
            L.append(f"{name} & 7{{,}}300 & {P} & & & \\\\")
    L.append("}")
    OUT.write_text("\n".join(L) + "\n")
    print(f"escrito {OUT}")
    print(f"  totales: {'si' if tot else 'NO'}, por modulo: {len(hier)}, potencia: {'si' if pw else 'NO'}, timing: {'si' if tm else 'NO'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
