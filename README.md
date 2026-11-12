# AURORA: Accelerated URJC Reconciliation Operations for Real-time Alice-Bob

AURORA is a high-performance framework dedicated to accelerating the key distillation pipeline in Quantum Key Distribution (QKD) systems. Developed as a research collaboration between the Universidad Rey Juan Carlos (URJC) and the Universidad Politécnica de Madrid (UPM), the project focuses on leveraging FPGA technology to overcome the computational bottlenecks of quantum post-processing.

---
## Overview

In QKD protocols like BB84, Alice and Bob generate raw data that requires significant processing before becoming a secure cryptographic key. AURORA offloads these tasks to the Zynq UltraScale+ MPSoC, utilizing a Hardware/Software (HW/SW) co-design approach.

- Synchronization & Correlation: High-speed time-tag alignment using parallel FPGA logic.
- Sifting: Real-time basis matching and bit extraction.
- Error Reconciliation: Hardware-accelerated correction (e.g., LDPC or Cascade) to handle channel noise.
- Privacy Amplification: High-throughput hashing to eliminate potential information leakage.

---
## System Architecture
The project is designed to run on the Zynq UltraScale+ platform, utilizing its unique architecture:
- Processing System (PS): The ARM Cortex-A53 cores handle high-level orchestration, network communication, and the management of Alice and Bob as independent processes.
- Programmable Logic (PL): The FPGA fabric hosts high-performance kernels developed via Vivado High-Level Synthesis (HLS) for intensive data processing.

---
## Development Roadmap

The project is structured in incremental phases to ensure validation at every step:

1. Phase 1: PC Prototyping (Current)
- [ ] Refactoring legacy C++ code into HLS-compatible structures (static arrays, fixed-point logic).
- [ ] Validation of synchronization and sifting algorithms in a desktop environment (VS Code).

2. Phase 2: HLS Synthesis & Optimization
- [ ] Porting C++ kernels to Vivado HLS.
- [ ] Applying hardware pragmas (#pragma HLS PIPELINE, UNROLL) to achieve maximum throughput.

3. Phase 3: PS/PL Integration
- [ ] Deploying kernels on the Zynq board.
- [ ] Implementing AXI-Stream interfaces for low-latency data transfer between the ARM cores and the FPGA.

4. Phase 4: Full Distillation Stack
- [ ] Integration of Error Correction and Privacy Amplification.
- [ ] Real-time QBER (Quantum Bit Error Rate) monitoring.

---
## Project Structure
```text
AURORA/
├── hw/                  # Kernels HLS (PL): aurora_sync, aurora_sift, aurora_toeplitz
├── rtl/trng/            # TRNG de osciladores en anillo (Verilog + AXI4-Lite) y su testbench
│   └── scripts/         # run_hls.tcl (csim / csynth / cosim / export)
├── sw/                  # Software del PS: orquestacion, Cascade, PA, autenticacion, TRNG, CLI
├── tools/               # nist_sts.py (bateria NIST SP 800-22 para capturas del TRNG)
├── tests/
│   ├── cpp/             # Tests de kernels (datos sinteticos) y fallos documentados del codigo anterior
│   ├── python/          # Modelo de referencia independiente + validacion sobre batches reales
│   └── data/            # Informe de sintesis del kernel anterior (evidencia)
├── docs/REVIEW.md       # Revision tecnica: fallos, evidencia, tests y soluciones
├── batches/             # Capturas reales del receptor (Alice/Bob)
├── hls*/ src/ include/  # Implementacion anterior (se mantiene como referencia)
└── Makefile
```

## Quick start (PC, sin Vitis)
Requisitos: `g++` (C++17), `python3` y `numpy`.
```bash
make test          # kernels nuevos + validacion sobre los 101 batches reales
make test-legacy   # fallos del codigo anterior (XFAIL = fallo reproducido)
make test-trng     # simulacion RTL del TRNG (requiere Icarus Verilog)
make pipeline      # sync -> sifting -> Cascade -> verificacion -> PA
make hls KERNEL=aurora_sync   # sintesis con Vitis HLS
```

## Processing chain
1. **Sync (PL)**: histograma de diferencias de tiempo Alice-Bob (grueso 1 ns, fino 8 ps);
   el PS calcula el lag como centroide con el fondo restado. Se sincroniza cada batch.
2. **Sifting (PL)**: emparejamiento voraz en la ventana +-350 ps y descarte de bases distintas;
   genera las claves de Alice y Bob empaquetadas. El PS trocea batches de cualquier tamano
   en lotes de hasta 65.536 tags (resultado identico a una sola pasada).
3. **Correccion de errores (PS)**: Cascade de 4 pases con efecto cascada y recuento de la informacion filtrada.
4. **Verificacion (PS)**: hash polinomico 2-universal.
5. **Amplificacion de privacidad (PL)**: hash de Toeplitz; longitud a partir del QBER y la informacion filtrada.
6. **Autenticacion del canal clasico (PS)**: Wegman-Carter (hash polinomico GF(2^128) + OTP) sobre la
   transcripcion de cada fase en ambos sentidos; la clave se entrega solo si todo verifica y la reserva
   de autenticacion se repone con clave QKD.
7. **TRNG (PL)**: osciladores en anillo con tests de salud SP 800-90B en hardware y extractor Toeplitz,
   para las semillas publicas de Cascade, verificacion y PA (ver `rtl/trng/README.md`).

Ver `docs/REVIEW.md` para el detalle de la revision y de las limitaciones conocidas.

---
### Institutional Collaboration
This project represents a synergy between two leading Spanish institutions:
- Universidad Rey Juan Carlos (URJC): Hardware acceleration and embedded systems integration. 
- Universidad Politécnica de Madrid (UPM): Algorithm design and quantum protocol logic.
---
### License
TBD: Specify your license here (e.g., MIT, GPL, or Institutional Research License).
