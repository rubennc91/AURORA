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

HW_SRC   := hw/aurora_sync.cpp hw/aurora_sift.cpp hw/aurora_toeplitz.cpp
SW_SRC   := sw/aurora_host.cpp
HDRS     := hw/aurora_kernels.h sw/aurora_host.h

LEGACY_FLAGS := -O2 -std=c++17 -w -Itests/cpp/legacy_stub

.PHONY: all test test-cpp test-python test-legacy test-all pipeline clean legacy-sim hls

all: $(BUILD)/aurora_cli

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/aurora_cli: $(HW_SRC) $(SW_SRC) sw/aurora_cli.cpp $(HDRS) | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) sw/aurora_cli.cpp -o $@

$(BUILD)/test_kernels: $(HW_SRC) $(SW_SRC) $(HDRS) tests/cpp/*.h tests/cpp/test_kernels.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(HW_SRC) $(SW_SRC) tests/cpp/test_kernels.cpp -o $@

$(BUILD)/test_legacy_sift: hls_sift/srcs/quantum_kernels.cpp $(HW_SRC) $(SW_SRC) tests/cpp/test_legacy_sift.cpp | $(BUILD)
	$(CXX) $(LEGACY_FLAGS) hls_sift/srcs/quantum_kernels.cpp $(HW_SRC) $(SW_SRC) tests/cpp/test_legacy_sift.cpp -o $@

$(BUILD)/test_legacy_hashing: hls_hashing/srcs/quantum_kernels.cpp tests/cpp/test_legacy_hashing.cpp | $(BUILD)
	$(CXX) $(LEGACY_FLAGS) hls_hashing/srcs/quantum_kernels.cpp tests/cpp/test_legacy_hashing.cpp -o $@

test: test-cpp test-python

test-cpp: $(BUILD)/test_kernels
	$(BUILD)/test_kernels

test-python: $(BUILD)/aurora_cli
	AURORA_CLI=$(BUILD)/aurora_cli AURORA_BATCHES=$(BATCHES) $(PYTHON) -m unittest discover -s tests/python -v

test-legacy: $(BUILD)/test_legacy_sift $(BUILD)/test_legacy_hashing
	AURORA_BATCHES=$(BATCHES) $(BUILD)/test_legacy_sift
	$(BUILD)/test_legacy_hashing

test-all: test test-legacy

pipeline: $(BUILD)/aurora_cli
	$(BUILD)/aurora_cli pipeline $(BATCHES) 0 100

legacy-sim: | $(BUILD)
	$(CXX) -O3 -Iinclude -std=c++17 src/main.cpp src/quantum_kernels.cpp -o $(BUILD)/aurora_sim

KERNEL ?= aurora_sync
hls:
	vitis_hls -f hw/scripts/run_hls.tcl -tclargs $(KERNEL)

clean:
	rm -rf $(BUILD)
