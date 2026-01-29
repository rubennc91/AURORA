all:
	g++ -O3 -Iinclude src/main.cpp src/quantum_kernels.cpp -o aurora_sim -std=c++17