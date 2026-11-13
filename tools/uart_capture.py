#!/usr/bin/env python3
"""
Lee la salida de la aplicacion bare-metal por la UART (o de un log ya
guardado), la guarda y extrae la captura del TRNG.

    python3 tools/uart_capture.py --port /dev/ttyUSB1          # lee la placa
    python3 tools/uart_capture.py --from-log build/board_log.txt   # solo extrae

Salidas:
    build/board_log.txt     log completo
    build/trng_capture.txt  palabras del TRNG en bruto (para trng-analyze / nist_sts)

Sin dependencias (usa termios). En la ZCU104 la UART del PS suele ser
/dev/ttyUSB1 (si no, prueba ttyUSB0..3); el usuario debe estar en el grupo dialout.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEX = re.compile(r"^[0-9a-f]{8}$")
ROW = re.compile(r"^R \d+ [0-9a-f]+$")   # filas del test de reinicios


def read_serial(port: str, baud: int, log: Path, timeout: float) -> None:
    import termios
    import tty

    speeds = {115200: termios.B115200, 230400: termios.B230400, 921600: termios.B921600}
    fd = os.open(port, os.O_RDONLY | os.O_NOCTTY)
    try:
        tty.setraw(fd)
        attrs = termios.tcgetattr(fd)
        attrs[4] = attrs[5] = speeds[baud]
        attrs[2] = (attrs[2] & ~termios.CSIZE & ~termios.PARENB & ~termios.CSTOPB) | termios.CS8 | termios.CLOCAL | termios.CREAD
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIFLUSH)
        print(f"Escuchando {port} a {baud} baudios (Ctrl+C para parar). Lanza ahora la aplicacion en la placa.")
        buf = b""
        t_last = time.time()
        n_words = n_rows = 0
        with open(log, "wb") as f:
            while True:
                chunk = os.read(fd, 4096)
                if chunk:
                    t_last = time.time()
                    f.write(chunk)
                    f.flush()
                    buf += chunk
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode(errors="replace").rstrip("\r")
                        if HEX.match(text):
                            n_words += 1
                            if n_words % 4096 == 0:
                                print(f"  ... {n_words} palabras del TRNG recibidas")
                        elif ROW.match(text):
                            n_rows += 1
                            if n_rows % 100 == 0:
                                print(f"  ... {n_rows} reinicios recibidos")
                        else:
                            print(text)
                        if text.startswith("AURORA_END"):
                            return
                elif time.time() - t_last > timeout:
                    print(f"Sin datos durante {timeout:.0f} s: se corta la captura.")
                    return
                else:
                    time.sleep(0.05)
    finally:
        os.close(fd)


def extract(log: Path, out: Path) -> int:
    lines = log.read_text(errors="replace").splitlines()
    words, inside, expected = [], False, None
    for line in lines:
        line = line.strip()
        if line.startswith("TRNG_RAW_BEGIN"):
            inside, words = True, []
            parts = line.split()
            expected = int(parts[1]) if len(parts) > 1 else None
        elif line.startswith("TRNG_RAW_END"):
            inside = False
        elif inside and HEX.match(line):
            words.append(line)
    if not words:
        print("No hay captura del TRNG en el log.")
        return 1
    out.write_text("\n".join(words) + "\n")
    print(f"Captura del TRNG: {len(words)} palabras ({32 * len(words)} bits) -> {out}")
    if expected is not None and expected != len(words):
        print(f"AVISO: se esperaban {expected} palabras (se perdieron lineas en la UART)")
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--from-log")
    ap.add_argument("--log", default=str(ROOT / "build" / "board_log.txt"))
    ap.add_argument("--out", default=str(ROOT / "build" / "trng_capture.txt"))
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args()
    if not args.port and not args.from_log:
        ap.error("indica --port o --from-log")
    log = Path(args.from_log or args.log)
    log.parent.mkdir(parents=True, exist_ok=True)
    if args.port:
        read_serial(args.port, args.baud, log, args.timeout)
    return extract(log, Path(args.out))


if __name__ == "__main__":
    sys.exit(main())
