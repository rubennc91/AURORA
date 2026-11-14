# =============================================================================
# AURORA - paquete de la release: prueba en la ZCU104 SIN compilar nada.
#
#   make check    comprueba ficheros (SHA-256), xsdb, python3, rele y UART
#   make run      enciende la placa, la programa, captura la salida y la APAGA
#   make analyze  entropia del TRNG (ultima captura) y NIST SP 800-22 de la salida extraida
#   make off      apaga el rele de la placa (por si acaso)
#
# Variables: RELAY=959BI_1 (RELAY=none si la placa se enciende a mano),
#            PORT=/dev/... (por defecto la UART del PS de la ZCU104 por by-id),
#            XSDB=/ruta/a/xsdb (por defecto se busca en el PATH y en /tools/Xilinx, /opt/Xilinx).
# =============================================================================
RELAY ?= 959BI_1
PORT  ?=
XSDB  ?= $(firstword $(shell command -v xsdb xsct 2>/dev/null) \
           $(wildcard /tools/Xilinx/Vivado_Lab/2022.2/bin/xsdb /tools/Xilinx/Vivado/2022.2/bin/xsdb \
                      /opt/Xilinx/Vivado_Lab/2022.2/bin/xsdb /opt/Xilinx/Vivado/2022.2/bin/xsdb \
                      /tools/Xilinx/Vitis/2022.2/bin/xsct /opt/Xilinx/Vitis/2022.2/bin/xsct))
STAMP := $(shell date +%Y%m%d_%H%M%S)
BOARD := board/aurora.bit board/psu_init.tcl board/aurora_test.elf

.PHONY: run check analyze off

run: check
	@mkdir -p results
	PORT="$(PORT)" RELAY="$(RELAY)" UART_CAPTURE=tools/uart_capture.py \
	  tools/board_session.sh --log results/$(STAMP)_log.txt --out results/$(STAMP)_trng.txt -- \
	  "$(XSDB)" tools/run_board.tcl $(BOARD)
	@echo "Log: results/$(STAMP)_log.txt   Captura del TRNG: results/$(STAMP)_trng.txt"

check:
	@sha256sum --quiet -c SHA256SUMS && echo "ficheros: OK (SHA-256)"
	@test -n "$(XSDB)" || { echo "no se encuentra xsdb: instala Vivado Lab Edition 2022.2 (o Vivado/Vitis) o pasa XSDB=/ruta/xsdb"; exit 1; }
	@echo "xsdb: $(XSDB)"
	@command -v python3 >/dev/null || { echo "falta python3"; exit 1; }
	@if [ "$(RELAY)" != "none" ]; then \
	  command -v usbrelay >/dev/null || { echo "falta usbrelay (o usa RELAY=none y enciende la placa a mano)"; exit 1; }; \
	  usbrelay 2>/dev/null | grep -q "^$(RELAY)=" || { echo "el rele $(RELAY) no aparece en usbrelay"; exit 1; }; \
	  echo "rele: $(RELAY)"; \
	else \
	  p="$(PORT)"; [ -n "$$p" ] || p=$$(ls /dev/serial/by-id/*Xilinx*JTAG*if01-port0 2>/dev/null | head -1); \
	  [ -n "$$p" ] && [ -e "$$p" ] || { echo "no se ve la UART de la placa (esta encendida y conectada por USB?)"; exit 1; }; \
	  echo "UART: $$p"; \
	fi

analyze:
	@f=$$(ls -t results/*_trng.txt 2>/dev/null | head -1); \
	[ -n "$$f" ] || { echo "no hay capturas en results/ (ejecuta antes make run)"; exit 1; }; \
	x="$${f%_trng.txt}_extraido.txt"; \
	echo "== $$f: min-entropia en bruto (SP 800-90B) y extraccion Toeplitz =="; \
	bin/aurora_cli trng-analyze "$$f" --extract "$$x" || exit 1; \
	echo "== NIST SP 800-22 sobre la salida del extractor ($$x) =="; \
	echo "   (la captura en bruto falla NIST por el sesgo de la fuente: es lo esperado)"; \
	python3 tools/nist_sts.py "$$x"

off:
	@if [ "$(RELAY)" != "none" ]; then usbrelay $(RELAY)=0 && usbrelay 2>/dev/null | grep "^$(RELAY)="; fi
