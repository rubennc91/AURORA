#!/usr/bin/env python3
"""
Test de reinicios del TRNG (NIST SP 800-90B, seccion 3.1.4).

Se arranca la fuente 1000 veces y de cada arranque se guardan las primeras
1000 muestras en bruto. Con eso se forma una matriz 1000 x 1000 (fila =
reinicio, columna = posicion de la muestra tras el arranque) y se comprueba
que ni las filas ni las columnas son mas predecibles de lo que dice H_I (la
entropia de la evaluacion normal, H_original de ea_non_iid). Una columna muy
sesgada significa que la fuente arranca siempre igual: la salida justo despues
del arranque es predecible.

Modos:

  soft   Un solo arranque de la placa por JTAG; la aplicacion para y arranca
         el TRNG N veces (enable = 0 apaga los osciladores en anillo).
         Unos 30 s para 1000 reinicios.

             python3 tools/restart_test.py soft --port /dev/ttyUSB1

  power  Reinicio fisico: corta la alimentacion de la placa con el rele USB,
         la vuelve a encender, la programa por JTAG y captura el primer
         arranque del TRNG. Un ciclo por fila (unos 30-40 s por ciclo: ~10 h
         para 1000). Se puede parar con Ctrl+C y continuar despues: las filas
         ya capturadas se conservan.

             python3 tools/restart_test.py power --port /dev/serial/by-id/... \\
                 --relay 0NSAZ_1,959BI_1 --off-at-end

  analyze  Analiza filas ya capturadas (rows.txt o un log de la UART).

             python3 tools/restart_test.py analyze build/restart/soft/rows.txt

Cada modo deja en --out: log(s), rows.txt, matrix.bin (1000 x 1000 bytes 0/1,
por filas, para ea_restart de NIST) y report.txt.

  ~/ea/cpp/ea_restart -n -v build/restart/soft/matrix.bin 1 0.785228

--skip descarta las primeras muestras tras cada arranque. Con --skip 0 se
evalua la fuente desde el primer instante; con --skip 1024 se evalua lo que
de verdad sale en produccion (el TRNG no entrega bits hasta superar el test de
arranque de 1024 muestras).

Requiere: xsct en el PATH (source <Xilinx>/Vitis/<ver>/settings64.sh),
build/vitis_ws construido (make baremetal), placa en arranque JTAG.
"""
from __future__ import annotations

import argparse
import math
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

ROW = re.compile(r"^R (\d+) ([0-9a-f]+)\s*$")
N_SAMPLES = 1000
N_RESTARTS = 1000


# ------------------------------------------------------------------ analisis --
def parse_rows(text: str) -> list[list[int]]:
    rows = []
    for line in text.splitlines():
        m = ROW.match(line.strip())
        if not m:
            continue
        hx = m.group(2)
        bits = [(int(c, 16) >> (3 - b)) & 1 for c in hx for b in range(4)]
        if len(bits) != N_SAMPLES:
            raise ValueError(f"fila {m.group(1)} con {len(bits)} muestras (se esperaban {N_SAMPLES})")
        rows.append(bits)
    return rows


def row_to_line(idx: int, bits: list[int]) -> str:
    hx = "".join(f"{(bits[k] << 3) | (bits[k + 1] << 2) | (bits[k + 2] << 1) | bits[k + 3]:x}"
                 for k in range(0, len(bits), 4))
    return f"R {idx} {hx}"


def log_binom_pmf(n: int, k: int, p: float) -> float:
    return (math.lgamma(n + 1) - math.lgamma(k + 1) - math.lgamma(n - k + 1)
            + k * math.log(p) + (n - k) * math.log1p(-p))


def sanity_cutoff(h_i: float, n: int = N_SAMPLES, k_symbols: int = 2) -> int:
    """SP 800-90B 3.1.4.3: valor critico U de Binomial(n, p = 2^-H_I) al nivel
    1 - alpha, alpha = 0.01 / (k * 2000). La prueba falla si la frecuencia del
    valor mas comun en alguna fila o columna es mayor que U."""
    p = 2.0 ** (-h_i)
    alpha = 0.01 / (k_symbols * 2000)
    tail = 0.0   # P(X > u)
    for u in range(n, -1, -1):
        if tail + math.exp(log_binom_pmf(n, u, p)) > alpha:
            return u
        tail += math.exp(log_binom_pmf(n, u, p))
    return 0


def mcv_count(values) -> int:
    ones = sum(values)
    return max(ones, len(values) - ones)


def analyze(rows: list[list[int]], h_i: float) -> tuple[bool, str]:
    out = []
    n_rows = len(rows)
    if n_rows == 0:
        return False, "No hay filas.\n"
    if n_rows < N_RESTARTS:
        out.append(f"AVISO: solo {n_rows} reinicios (SP 800-90B pide {N_RESTARTS}); resultado orientativo.")
    rows = rows[:N_RESTARTS]
    n_rows = len(rows)
    cols = [[rows[r][c] for r in range(n_rows)] for c in range(N_SAMPLES)]

    u_row = sanity_cutoff(h_i, N_SAMPLES)
    u_col = sanity_cutoff(h_i, n_rows)
    f_rows = [mcv_count(r) for r in rows]
    f_cols = [mcv_count(c) for c in cols]
    fr, fc = max(f_rows), max(f_cols)
    bad_rows = [i for i, f in enumerate(f_rows) if f > u_row]
    bad_cols = [i for i, f in enumerate(f_cols) if f > u_col]

    total_ones = sum(sum(r) for r in rows)
    out.append(f"Matriz: {n_rows} reinicios x {N_SAMPLES} muestras; P(1) global = {total_ones / (n_rows * N_SAMPLES):.4f}")
    out.append(f"H_I = {h_i:.6f} -> p = 2^-H_I = {2 ** -h_i:.4f}")
    out.append(f"Filas:    max. frecuencia del valor mas comun = {fr} (umbral {u_row}) -> {len(bad_rows)} filas por encima")
    out.append(f"Columnas: max. frecuencia del valor mas comun = {fc} (umbral {u_col}) -> {len(bad_cols)} columnas por encima")
    if bad_cols:
        out.append(f"  columnas por encima del umbral: {bad_cols[:40]}{' ...' if len(bad_cols) > 40 else ''}")
        last = max(bad_cols)
        out.append(f"  transitorio de arranque: la ultima columna mala es la {last}; "
                   f"descartar >= {last + 1} muestras tras el arranque (ademas de las de --skip)")
    # Primeras columnas: como de predecible es el arranque.
    head = ", ".join(f"{f_cols[c] / n_rows:.2f}" for c in range(min(16, N_SAMPLES)))
    out.append(f"Frecuencia del valor mas comun en las primeras 16 columnas: {head}")
    ok = not bad_rows and not bad_cols
    out.append("Prueba de cordura (3.1.4.3): " + ("SUPERADA" if ok else "FALLIDA"))
    if ok:
        out.append("Siguiente paso: estimar la entropia de filas y columnas con ea_restart (debe ser >= H_I / 2).")
    return ok, "\n".join(out) + "\n"


def write_outputs(rows: list[list[int]], out: Path, h_i: float) -> bool:
    out.mkdir(parents=True, exist_ok=True)
    (out / "rows.txt").write_text("".join(row_to_line(i, r) + "\n" for i, r in enumerate(rows)))
    if len(rows) >= N_RESTARTS:
        (out / "matrix.bin").write_bytes(bytes(b for r in rows[:N_RESTARTS] for b in r))
    ok, report = analyze(rows, h_i)
    (out / "report.txt").write_text(report)
    print(report, end="")
    if (out / "matrix.bin").exists():
        print(f"Matriz para NIST: {out / 'matrix.bin'}\n"
              f"  ~/ea/cpp/ea_restart -n -v {out / 'matrix.bin'} 1 {h_i}")
    return ok


# --------------------------------------------------------------- placa -------
def run_board_once(port: str, baud: int, log: Path, n: int, skip: int, off_us: int, timeout: float) -> int:
    """Programa la placa con XSCT en modo reinicios y lee la UART hasta AURORA_END."""
    from uart_capture import read_serial

    cmd = ["xsct", str(ROOT / "hw" / "scripts" / "run_board.tcl"), "restart", str(n), str(skip), str(off_us)]
    xsct_log = log.with_suffix(".xsct.txt")
    with open(xsct_log, "w") as xl:
        proc = subprocess.Popen(cmd, cwd=ROOT, stdout=xl, stderr=subprocess.STDOUT)
        try:
            read_serial(port, baud, log, timeout)
        finally:
            try:
                proc.wait(timeout=120)
            except subprocess.TimeoutExpired:
                proc.kill()
    if proc.returncode != 0:
        print(f"xsct termino con codigo {proc.returncode} (ver {xsct_log})")
    return proc.returncode or 0


def relay(cmd: str, names: str, state: int) -> None:
    """Pone todos los reles de 'names' (separados por comas) en 'state'."""
    for name in [n.strip() for n in names.split(",") if n.strip()]:
        subprocess.run([cmd, f"{name}={state}"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wait_port(port: str, timeout: float) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        if Path(port).exists():
            return True
        time.sleep(0.5)
    return False


def cmd_soft(a) -> int:
    out = Path(a.out or ROOT / "build" / "restart" / f"soft_skip{a.skip}")
    out.mkdir(parents=True, exist_ok=True)
    log = out / "log.txt"
    run_board_once(a.port, a.baud, log, a.n, a.skip, a.off_us, a.timeout)
    text = log.read_text(errors="replace")
    rows = parse_rows(text)
    if "RESTART_END" not in text or len(rows) != a.n:
        print(f"AVISO: {len(rows)} filas de {a.n} (captura incompleta o lineas perdidas en la UART)")
    return 0 if write_outputs(rows, out, a.h_i) else 1


def cmd_power(a) -> int:
    out = Path(a.out or ROOT / "build" / "restart" / f"power_skip{a.skip}")
    (out / "cycles").mkdir(parents=True, exist_ok=True)
    rows_file = out / "rows.txt"
    rows = parse_rows(rows_file.read_text()) if rows_file.exists() else []
    if rows:
        print(f"Continuando: {len(rows)} filas ya capturadas en {rows_file}")
    consecutive_fail = 0
    try:
        while len(rows) < a.n:
            cycle = len(rows)
            t0 = time.time()
            relay(a.relay_cmd, a.relay, 0)
            time.sleep(a.off_s)
            relay(a.relay_cmd, a.relay, 1)
            if not wait_port(a.port, 60):
                print(f"[{cycle}] la UART {a.port} no aparece tras encender")
                consecutive_fail += 1
            else:
                time.sleep(a.boot_s)
                log = out / "cycles" / f"cycle_{cycle:04d}.txt"
                run_board_once(a.port, a.baud, log, 1, a.skip, 0, a.timeout)
                got = parse_rows(log.read_text(errors="replace")) if log.exists() else []
                if len(got) == 1:
                    rows.append(got[0])
                    with open(rows_file, "a") as f:
                        f.write(row_to_line(cycle, got[0]) + "\n")
                    consecutive_fail = 0
                    eta = (a.n - len(rows)) * (time.time() - t0) / 3600
                    print(f"[{cycle + 1}/{a.n}] fila capturada en {time.time() - t0:.0f} s; quedan ~{eta:.1f} h")
                else:
                    consecutive_fail += 1
                    print(f"[{cycle}] sin fila en {log}; se repite el ciclo")
            if consecutive_fail >= a.max_fail:
                print(f"{consecutive_fail} ciclos seguidos fallidos: se aborta (las filas capturadas se conservan)")
                break
    except KeyboardInterrupt:
        print(f"\nInterrumpido: {len(rows)} filas guardadas en {rows_file}; vuelve a lanzar el mismo comando para continuar")
    finally:
        if a.off_at_end:
            relay(a.relay_cmd, a.relay, 0)
            print(f"Reles {a.relay} apagados")
    return 0 if write_outputs(rows, out, a.h_i) else 1


def cmd_analyze(a) -> int:
    rows = parse_rows(Path(a.rows).read_text(errors="replace"))
    out = Path(a.out) if a.out else Path(a.rows).parent
    return 0 if write_outputs(rows, out, a.h_i) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="mode", required=True)

    def common(p, board=True):
        p.add_argument("--h-i", type=float, default=0.785228, help="H_I: H_original de ea_non_iid (por defecto 0.785228)")
        p.add_argument("--out")
        if board:
            p.add_argument("--port", required=True, help="UART del PS (mejor /dev/serial/by-id/...: no cambia al reenchufar)")
            p.add_argument("--baud", type=int, default=115200)
            p.add_argument("--skip", type=int, default=0, help="muestras descartadas tras cada arranque")
            p.add_argument("--timeout", type=float, default=90.0, help="s sin datos por la UART antes de cortar")

    p = sub.add_parser("soft", help="reinicios del TRNG sin cortar la alimentacion")
    common(p)
    p.add_argument("--n", type=int, default=N_RESTARTS)
    p.add_argument("--off-us", type=int, default=1000, help="tiempo con los osciladores parados (us)")
    p.set_defaults(func=cmd_soft)

    p = sub.add_parser("power", help="reinicios con corte de alimentacion (rele USB)")
    common(p)
    p.add_argument("--n", type=int, default=N_RESTARTS)
    p.add_argument("--relay", default="0NSAZ_1", help="rele(s) del enchufe de la placa, separados por comas (p. ej. 0NSAZ_1,959BI_1)")
    p.add_argument("--relay-cmd", default="usbrelay")
    p.add_argument("--off-s", type=float, default=5.0, help="s con la placa apagada")
    p.add_argument("--boot-s", type=float, default=8.0, help="s de espera tras aparecer la UART")
    p.add_argument("--max-fail", type=int, default=10)
    p.add_argument("--off-at-end", action="store_true", help="apagar la placa al terminar")
    p.set_defaults(func=cmd_power)

    p = sub.add_parser("analyze", help="analiza filas ya capturadas")
    common(p, board=False)
    p.add_argument("rows")
    p.set_defaults(func=cmd_analyze)

    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)   # progreso visible tambien con nohup > fichero
    return a.func(a)


if __name__ == "__main__":
    sys.exit(main())
