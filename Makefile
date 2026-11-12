# =============================================================================
# AURORA - Build y tests en PC (g++ / Python 3 + numpy). No requiere Vitis.
#
#   make             compila build/aurora_cli
#   make test        tests de los kernels nuevos + validacion sobre batches reales
#   make test-legacy tests que documentan los fallos del codigo anterior (XFAIL)
#   make test-all    todo lo anterior
#   make pipeline    cadena completa sobre todos los batches
#   make legacy-sim  simulador original (src/main.cpp)
#   make hls KERNEL=aurora_sync|aurora_sift|aurora_toeplitz   (requiere Vitis HLS)
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

HW_SRC   := hw/aurora_sync.cpp hw/aurora_sift.cpp hw/aurora_toeplitz.cpp
SW_SRC   := sw/aurora_host.cpp
HDRS     := hw/aurora_kernels.h sw/aurora_host.h

LEGACY_FLAGS := -O2 -std=c++17 -w -Itests/cpp/legacy_stub

.PHONY: all test test-cpp test-python test-legacy test-all pipeline clean legacy-sim hls

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

test-cpp: $(BUILD)/test_kernels$(EXE)
	$(BUILD)/test_kernels$(EXE)

test-python: $(CLI)
	AURORA_CLI=$(CLI) AURORA_BATCHES=$(BATCHES) $(PYTHON) -m unittest discover -s tests/python -v

test-legacy: $(BUILD)/test_legacy_sift$(EXE) $(BUILD)/test_legacy_hashing$(EXE)
	AURORA_BATCHES=$(BATCHES) $(BUILD)/test_legacy_sift$(EXE)
	$(BUILD)/test_legacy_hashing$(EXE)

test-all: test test-legacy

pipeline: $(CLI)
	$(CLI) pipeline $(BATCHES) 0 100

legacy-sim: | $(BUILD)
	$(CXX) -O3 -Iinclude -std=c++17 src/main.cpp src/quantum_kernels.cpp -o $(BUILD)/aurora_sim$(EXE)

KERNEL ?= aurora_sync
hls:
	vitis_hls -f hw/scripts/run_hls.tcl -tclargs $(KERNEL)

clean:
	rm -rf $(BUILD)
