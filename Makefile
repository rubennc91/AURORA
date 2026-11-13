# =============================================================================
# AURORA - Build y tests en PC (g++ / Python 3 + numpy). No requiere Vitis.
#
#   make             compila build/aurora_cli
#   make test        tests de los kernels nuevos + validacion sobre batches reales
#   make test-legacy tests que documentan los fallos del codigo anterior (XFAIL)
#   make test-trng   simulacion RTL del TRNG (requiere Icarus Verilog)
#   make test-all    todo lo anterior
#   make pipeline    cadena completa sobre todos los batches
#   make legacy-sim  simulador original (src/main.cpp)
#   make hls KERNEL=aurora_sync|aurora_sift|aurora_toeplitz|aurora_ldpc_dec   (requiere Vitis HLS)
#   make hls-all     csim + csynth + export de los 4 kernels (requiere Vitis HLS)
#   make vivado      crea build/vivado/aurora.xpr con el block design
#   make bitstream   idem + bitstream + build/vivado/aurora.xsa
#   make test-baremetal  aplicacion bare-metal sobre una plataforma simulada (PC)
#   make baremetal   aplicacion bare-metal para la placa (requiere XSCT)
#   make board-run   programa la placa por JTAG y lanza la aplicacion
#   make board-restart PORT=/dev/ttyUSB1   test de reinicios del TRNG (SP 800-90B)
# =============================================================================
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-label
PYTHON   ?= python3
BUILD    := build
BATCHES  ?= batches

# Windows (MSYS2 UCRT64 / Git Bash + MinGW): los binarios llevan extension .exe
ifeq ($(OS),Windows_NT)
EXE := .exe
else
EXE :=
endif
CLI := $(BUILD)/aurora_cli$(EXE)

HW_SRC   := hw/aurora_sync.cpp hw/aurora_sift.cpp hw/aurora_toeplitz.cpp hw/aurora_ldpc_dec.cpp
SW_SRC   := sw/aurora_host.cpp sw/aurora_auth.cpp sw/aurora_ldpc.cpp sw/aurora_trng.cpp sw/aurora_trng_driver.cpp sw/aurora_random.cpp
HDRS     := hw/aurora_kernels.h sw/aurora_host.h sw/aurora_auth.h sw/aurora_ldpc.h sw/aurora_trng.h sw/aurora_trng_driver.h sw/aurora_random.h

LEGACY_FLAGS := -O2 -std=c++17 -w -Itests/cpp/legacy_stub

.PHONY: all test test-cpp test-python test-legacy test-trng test-all pipeline clean legacy-sim hls hls-all vivado bitstream baremetal-data test-baremetal baremetal board-run board-restart

all: $(CLI)

$(BUILD):
	mkdir -p $(BUILD)

$(CLI): $(HW_SRC) $(SW_SRC) sw/aurora_cli.cpp $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) sw/aurora_cli.cpp -o $@

$(BUILD)/test_kernels$(EXE): $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/*.h tests/cpp/test_kernels.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_kernels.cpp -o $@

$(BUILD)/test_legacy_sift$(EXE): hls_sift/srcs/quantum_kernels.cpp $(HW_SRC) $(SW_SRC) tests/cpp/test_legacy_sift.cpp | $(BUILD)
	$(CXX) $(LEGACY_FLAGS) hls_sift/srcs/quantum_kernels.cpp $(HW_SRC) $(SW_SRC) tests/cpp/test_legacy_sift.cpp -o $@

$(BUILD)/test_legacy_hashing$(EXE): hls_hashing/srcs/quantum_kernels.cpp tests/cpp/test_legacy_hashing.cpp | $(BUILD)
	$(CXX) $(LEGACY_FLAGS) hls_hashing/srcs/quantum_kernels.cpp tests/cpp/test_legacy_hashing.cpp -o $@

test: test-cpp test-python

$(BUILD)/test_auth$(EXE): $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/test_framework.h tests/cpp/test_auth.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_auth.cpp -o $@

$(BUILD)/test_trng$(EXE): $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/test_framework.h tests/cpp/test_trng.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_trng.cpp -o $@

$(BUILD)/test_trng_driver$(EXE): $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/test_framework.h tests/cpp/test_trng_driver.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_trng_driver.cpp -o $@

$(BUILD)/test_ldpc$(EXE): $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/test_framework.h tests/cpp/test_ldpc.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_ldpc.cpp -o $@

test-cpp: $(BUILD)/test_kernels$(EXE) $(BUILD)/test_auth$(EXE) $(BUILD)/test_ldpc$(EXE) $(BUILD)/test_trng$(EXE) $(BUILD)/test_trng_driver$(EXE)
	$(BUILD)/test_kernels$(EXE)
	$(BUILD)/test_auth$(EXE)
	$(BUILD)/test_ldpc$(EXE)
	$(BUILD)/test_trng$(EXE)
	$(BUILD)/test_trng_driver$(EXE)

test-python: $(CLI)
	AURORA_CLI=$(CLI) AURORA_BATCHES=$(BATCHES) $(PYTHON) -m unittest discover -s tests/python -v

test-legacy: $(BUILD)/test_legacy_sift$(EXE) $(BUILD)/test_legacy_hashing$(EXE)
	AURORA_BATCHES=$(BATCHES) $(BUILD)/test_legacy_sift$(EXE)
	$(BUILD)/test_legacy_hashing$(EXE)

# TRNG: simulacion RTL (Icarus Verilog) contrastada con los modelos de referencia.
test-trng: $(CLI)
	AURORA_CLI=$(CLI) $(PYTHON) -m unittest discover -s tests/trng -v

test-all: test test-legacy test-trng test-baremetal

pipeline: $(CLI)
	$(CLI) pipeline $(BATCHES) 0 100

legacy-sim: | $(BUILD)
	$(CXX) -O3 -Iinclude -std=c++17 src/main.cpp src/quantum_kernels.cpp -o $(BUILD)/aurora_sim$(EXE)

KERNEL ?= aurora_sync
hls:
	vitis_hls -f hw/scripts/run_hls.tcl -tclargs $(KERNEL)

# Requiere Vitis HLS / Vivado en el PATH (source <Xilinx>/Vivado/<ver>/settings64.sh)
hls-all:
	for k in aurora_sync aurora_sift aurora_toeplitz aurora_ldpc_dec; do \
		vitis_hls -f hw/scripts/run_hls.tcl -tclargs $$k csim csynth export || exit 1; \
	done

VIVADO_ARGS ?=
vivado:
	vivado -mode batch -nojournal -log build/vivado_create.log -source hw/scripts/create_vivado.tcl -tclargs $(VIVADO_ARGS)

bitstream:
	vivado -mode batch -nojournal -log build/vivado_bit.log -source hw/scripts/create_vivado.tcl -tclargs bitstream $(VIVADO_ARGS)

# ---------------------------------------------------------------- bare-metal --
BM_SRCS := sw/baremetal/main.cpp sw/baremetal/hw_backend.cpp sw/baremetal/ldpc_sw_model.cpp \
           sw/aurora_host.cpp sw/aurora_auth.cpp sw/aurora_ldpc.cpp \
           sw/aurora_trng.cpp sw/aurora_trng_driver.cpp

baremetal-data: $(CLI)
	$(PYTHON) tools/make_baremetal_data.py

# Aplicacion bare-metal en una plataforma SIMULADA en el PC (registros emulados
# que ejecutan los modelos C++ de los kernels y del TRNG).
test-baremetal: baremetal-data
	sh tools/check_baremetal_std.sh
	mkdir -p $(BUILD)/bm_sim
	for k in sync sift toeplitz; do \
		$(CXX) $(CXXFLAGS) -c hw/aurora_$$k.cpp -Daurora_$$k=sw_aurora_$$k -o $(BUILD)/bm_sim/sw_$$k.o || exit 1; \
	done
	$(CXX) $(CXXFLAGS) -Itests/baremetal/shim -Isw -Ihw -Isw/baremetal $(BM_SRCS) tests/baremetal/mock_platform.cpp \
		$(BUILD)/baremetal_src/aurora_data.cpp $(BUILD)/bm_sim/sw_*.o -o $(BUILD)/bm_sim/aurora_bm_sim$(EXE)
	$(BUILD)/bm_sim/aurora_bm_sim$(EXE) > $(BUILD)/bm_sim/log.txt
	grep -v -E '^[0-9a-f]{8}$$' $(BUILD)/bm_sim/log.txt
	grep -q "RESULTADO: TODO OK" $(BUILD)/bm_sim/log.txt
	$(PYTHON) tools/uart_capture.py --from-log $(BUILD)/bm_sim/log.txt --out $(BUILD)/bm_sim/trng_capture.txt
	AURORA_SIM_RESTART="40 1024 100" $(BUILD)/bm_sim/aurora_bm_sim$(EXE) > $(BUILD)/bm_sim/restart_log.txt
	grep -q "RESTART_END" $(BUILD)/bm_sim/restart_log.txt
	grep -q "RESULTADO: TODO OK" $(BUILD)/bm_sim/restart_log.txt
	$(PYTHON) tools/restart_test.py analyze $(BUILD)/bm_sim/restart_log.txt --out $(BUILD)/bm_sim/restart

# Placa (requiere Vitis/XSCT en el PATH y build/vivado/aurora.xsa)
baremetal: baremetal-data
	sh tools/check_baremetal_std.sh
	cp $(BM_SRCS) $(BUILD)/baremetal_src/
	xsct hw/scripts/create_vitis.tcl

board-run:
	xsct hw/scripts/run_board.tcl

# Test de reinicios del TRNG (soft: sin cortar la alimentacion). SKIP=1024 evalua
# la salida tal como la usa produccion (tras el test de arranque).
PORT ?= /dev/ttyUSB1
SKIP ?= 0
board-restart:
	$(PYTHON) tools/restart_test.py soft --port $(PORT) --skip $(SKIP)

clean:
	rm -rf $(BUILD)
