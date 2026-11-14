#!/usr/bin/env bash
# =============================================================================
# AURORA - Empaqueta una release lista para la placa (sin compilar en destino).
#
#   tools/make_release.sh <version> [salida.zip]
#
# Toma el bitstream, psu_init.tcl y el ELF del build actual (make bitstream &&
# make baremetal, SIN calefactor), compila la CLI del PC enlazada estaticamente
# y anade los scripts para lanzarlo con 'make run' (ver tools/release/README.md).
# Por defecto deja ~/aurora_<version>.zip.
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VER="${1:?uso: tools/make_release.sh <version> [salida.zip]}"
ZIP="${2:-$HOME/aurora_${VER}.zip}"
cd "$ROOT"

WS=build/vitis_ws
BIT=$(find "$WS/aurora_pf/export" -name '*.bit' | head -1)
PSU=$(find "$WS/aurora_pf/export" -name psu_init.tcl | head -1)
ELF="$WS/aurora_test/Release/aurora_test.elf"
XSA=build/vivado/aurora.xsa
for f in "$BIT" "$PSU" "$ELF" "$XSA" docs/report/AURORA_informe.pdf docs/RELEASE_NOTES.md; do
    [ -n "$f" ] && [ -f "$f" ] || { echo "falta $f (make bitstream && make baremetal)" >&2; exit 1; }
done
if grep -qi heater build/vivado/address_map.txt 2>/dev/null; then
    echo "el bitstream actual lleva el calefactor: make bitstream && make baremetal (sin VIVADO_ARGS=heater)" >&2
    exit 1
fi
[ -z "$(git status --porcelain -- hw sw rtl)" ] || echo "AVISO: hay cambios sin commit en hw/ sw/ rtl/" >&2

NAME="aurora_${VER}"
STAGE="build/release/$NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"/{board,bin,tools}
cp "$BIT" "$STAGE/board/aurora.bit"
cp "$PSU" "$STAGE/board/psu_init.tcl"
cp "$ELF" "$STAGE/board/aurora_test.elf"
cp "$XSA" "$STAGE/board/aurora.xsa"

# CLI del PC estatica: funciona en cualquier Linux x86-64 sin dependencias.
SRCS="hw/aurora_sync.cpp hw/aurora_sift.cpp hw/aurora_toeplitz.cpp hw/aurora_ldpc_dec.cpp \
      sw/aurora_host.cpp sw/aurora_auth.cpp sw/aurora_ldpc.cpp sw/aurora_trng.cpp sw/aurora_trng_driver.cpp \
      sw/aurora_random.cpp sw/aurora_cli.cpp"
g++ -O2 -std=c++17 -w -static $SRCS -o "$STAGE/bin/aurora_cli"
strip "$STAGE/bin/aurora_cli"

cp tools/release/Makefile "$STAGE/Makefile"
cp tools/release/run_board.tcl tools/board_session.sh tools/uart_capture.py tools/nist_sts.py "$STAGE/tools/"
chmod +x "$STAGE/tools/board_session.sh" "$STAGE/bin/aurora_cli"
COMMIT=$(git rev-parse --short HEAD)
sed -e "s/@VERSION@/$VER/g" -e "s/@COMMIT@/$COMMIT/g" tools/release/README.md > "$STAGE/README.md"
cp docs/report/AURORA_informe.pdf docs/RELEASE_NOTES.md "$STAGE/"
(cd "$STAGE" && find board bin tools -type f | sort | xargs sha256sum > SHA256SUMS)

rm -f "$ZIP"
(cd build/release && zip -qr "$ZIP" "$NAME")
echo "release: $ZIP"
(cd "$STAGE" && cat SHA256SUMS)
