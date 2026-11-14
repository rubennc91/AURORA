#!/usr/bin/env bash
# =============================================================================
# AURORA - Sesion completa en la placa: enciende el rele, espera la UART,
# captura la salida, programa la placa por JTAG y APAGA EL RELE al terminar
# (tambien si algo falla o se interrumpe con Ctrl+C).
#
#   tools/board_session.sh [opciones] -- <comando que programa la placa>
#
# Opciones (tambien por variables de entorno):
#   --port P     UART del PS           (PORT; por defecto la de la ZCU104 por by-id)
#   --relay R    rele de la placa      (RELAY, 959BI_1; "none" = sin rele, placa ya encendida)
#   --log F      log de la UART        (LOG, board_log.txt)
#   --out F      captura del TRNG      (OUT, trng_capture.txt)
#   --timeout S  s sin datos por la UART antes de cortar (TIMEOUT, 120)
#   --boot S     s de espera tras aparecer la UART (BOOT_S, 8)
# Devuelve 0 solo si la aplicacion termina con "RESULTADO: TODO OK".
# =============================================================================
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${PORT:-}"
RELAY="${RELAY:-959BI_1}"
LOG="${LOG:-board_log.txt}"
OUT="${OUT:-trng_capture.txt}"
TIMEOUT="${TIMEOUT:-120}"
BOOT_S="${BOOT_S:-8}"
UART_CAPTURE="${UART_CAPTURE:-$HERE/uart_capture.py}"

while [ $# -gt 0 ]; do
    case "$1" in
        --port) PORT="$2"; shift 2 ;;
        --relay) RELAY="$2"; shift 2 ;;
        --log) LOG="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --boot) BOOT_S="$2"; shift 2 ;;
        --) shift; break ;;
        *) echo "opcion desconocida: $1" >&2; exit 2 ;;
    esac
done
[ $# -gt 0 ] || { echo "falta el comando que programa la placa (despues de --)" >&2; exit 2; }

# UART: la interfaz 1 del cable JTAG+3Serial de la ZCU104 es la del PS.
find_port() {
    ls /dev/serial/by-id/*Xilinx*JTAG*if01-port0 2>/dev/null | head -1
}

relay_set() {   # relay_set 0|1
    [ "$RELAY" = "none" ] && return 0
    usbrelay "$RELAY=$1" >/dev/null 2>&1
}

CAP_PID=""
cleanup() {
    local rc=$?
    [ -n "$CAP_PID" ] && kill "$CAP_PID" 2>/dev/null
    if [ "$RELAY" != "none" ]; then
        relay_set 0
        if usbrelay 2>/dev/null | grep -q "^$RELAY=0"; then
            echo "Rele $RELAY apagado."
        else
            echo "ATENCION: no se pudo confirmar que el rele $RELAY este apagado: usbrelay $RELAY=0" >&2
        fi
    fi
    exit $rc
}
trap cleanup EXIT
trap 'exit 130' INT TERM

if [ "$RELAY" != "none" ]; then
    command -v usbrelay >/dev/null || { echo "no se encuentra usbrelay (usa --relay none si la placa ya esta encendida)" >&2; exit 2; }
    usbrelay 2>/dev/null | grep -q "^$RELAY=" || { echo "el rele $RELAY no esta conectado (usbrelay lo lista: $(usbrelay 2>/dev/null | tr '\n' ' '))" >&2; exit 2; }
    echo "Encendiendo la placa (rele $RELAY)..."
    relay_set 1 || { echo "no se pudo encender el rele $RELAY" >&2; exit 2; }
fi

# Espera a que aparezca la UART (el cable USB-JTAG/UART esta en la placa).
t0=$(date +%s)
while :; do
    P="${PORT:-$(find_port)}"
    [ -n "$P" ] && [ -e "$P" ] && break
    if [ $(( $(date +%s) - t0 )) -ge 60 ]; then
        echo "la UART ${PORT:-(ZCU104 por by-id)} no aparece tras 60 s" >&2; exit 3
    fi
    sleep 0.5
done
PORT="$P"
if [ "$RELAY" != "none" ]; then
    echo "UART $PORT presente; esperando ${BOOT_S} s al arranque de la placa..."
    sleep "$BOOT_S"
fi

# Captura en segundo plano; se lanza el programa cuando ya esta escuchando.
mkdir -p "$(dirname "$LOG")" "$(dirname "$OUT")"
CAP_STDOUT="$LOG.capture.txt"
python3 "$UART_CAPTURE" --port "$PORT" --log "$LOG" --out "$OUT" --timeout "$TIMEOUT" > "$CAP_STDOUT" 2>&1 &
CAP_PID=$!
for _ in $(seq 1 100); do
    grep -q "Escuchando" "$CAP_STDOUT" 2>/dev/null && break
    kill -0 "$CAP_PID" 2>/dev/null || { cat "$CAP_STDOUT" >&2; echo "la captura de la UART no arranco" >&2; exit 3; }
    sleep 0.1
done

echo "Programando la placa: $*"
"$@" > "$LOG.jtag.txt" 2>&1
JTAG_RC=$?
if [ $JTAG_RC -ne 0 ]; then
    tail -20 "$LOG.jtag.txt" >&2
    echo "la programacion por JTAG fallo (codigo $JTAG_RC, ver $LOG.jtag.txt)" >&2
    exit 4
fi
echo "Aplicacion lanzada; esperando el final por la UART (puede tardar ~1-2 min)..."
wait "$CAP_PID"
CAP_PID=""
cat "$CAP_STDOUT"

if grep -q "RESULTADO: TODO OK" "$LOG"; then
    grep "RESULTADO" "$LOG"
    exit 0
fi
grep -E "FALLO|RESULTADO" "$LOG" >&2 || echo "la aplicacion no llego al final (ver $LOG)" >&2
exit 1
