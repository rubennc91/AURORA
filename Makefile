# Makefile adaptado para entorno AURORA (UCRT64)
CXX = g++
CXXFLAGS = -O3 -Iinclude -std=c++17 -fopenmp

SRC = src/main.cpp src/quantum_kernels.cpp
TARGET = aurora_sim.exe

all:
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)