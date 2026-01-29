# AURORA (Accelerated Urjc Reconciliation Operations for Real-time Alice-bob)

Of course. Here is the complete content for your README.md file. I have designed it to be professional, emphasizing the academic collaboration and the technical roadmap for the Zynq UltraScale+ implementation.
AURORA: Accelerated URJC Reconciliation Operations for Real-time Alice-Bob

AURORA is a high-performance framework dedicated to accelerating the key distillation pipeline in Quantum Key Distribution (QKD) systems. Developed as a research collaboration between the Universidad Rey Juan Carlos (URJC) and the Universidad Politécnica de Madrid (UPM), the project focuses on leveraging FPGA technology to overcome the computational bottlenecks of quantum post-processing.
📖 Overview

In QKD protocols like BB84, Alice and Bob generate raw data that requires significant processing before becoming a secure cryptographic key. AURORA offloads these tasks to the Zynq UltraScale+ MPSoC, utilizing a Hardware/Software (HW/SW) co-design approach:

    Synchronization & Correlation: High-speed time-tag alignment using parallel FPGA logic.

    Sifting: Real-time basis matching and bit extraction.

    Error Reconciliation: Hardware-accelerated correction (e.g., LDPC or Cascade) to handle channel noise.

    Privacy Amplification: High-throughput hashing to eliminate potential information leakage.

🏗️ System Architecture

The project is designed to run on the Zynq UltraScale+ platform, utilizing its unique architecture:

    Processing System (PS): The ARM Cortex-A53 cores handle high-level orchestration, network communication, and the management of Alice and Bob as independent processes.

    Programmable Logic (PL): The FPGA fabric hosts high-performance kernels developed via Vivado High-Level Synthesis (HLS) for intensive data processing.

🚀 Development Roadmap

The project is structured in incremental phases to ensure validation at every step:
Phase 1: PC Prototyping (Current)

    Refactoring legacy C++ code into HLS-compatible structures (static arrays, fixed-point logic).

    Validation of synchronization and sifting algorithms in a desktop environment (VS Code).

Phase 2: HLS Synthesis & Optimization

    Porting C++ kernels to Vivado HLS.

    Applying hardware pragmas (#pragma HLS PIPELINE, UNROLL) to achieve maximum throughput.

Phase 3: PS/PL Integration

    Deploying kernels on the Zynq board.

    Implementing AXI-Stream interfaces for low-latency data transfer between the ARM cores and the FPGA.

Phase 4: Full Distillation Stack

    Integration of Error Correction and Privacy Amplification.

    Real-time QBER (Quantum Bit Error Rate) monitoring.
	
🛠️ Project Structure

AURORA/
├── include/           # Header files (HLS-friendly structures)
├── src/               # Source code for PS and PC simulation
├── hls/               # Vivado HLS project files and scripts
├── batches/           # Sample Alice/Bob detection data
└── Makefile           # Build system for PC validation

🤝 Institutional Collaboration

This project represents a synergy between two leading Spanish institutions:

    Universidad Rey Juan Carlos (URJC): Algorithm design and quantum protocol logic.

    Universidad Politécnica de Madrid (UPM): Hardware acceleration and embedded systems integration.

📄 License

TBD: Specify your license here (e.g., MIT, GPL, or Institutional Research License).