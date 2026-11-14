# AURORA v0.1.0 — primera versión validada en placa (14/11/2026)

Post-procesado QKD (BB84) en Zynq UltraScale+ (ZCU104, XCZU7EV), con la parte
de datos en la PL y el control en el PS. Validado sobre 101 capturas reales y
en la placa (aplicación bare-metal).

## Contenido

**PL (Vivado/Vitis HLS 2022.2, 100 MHz, WNS +3,24 ns)**
- `aurora_sync`, `aurora_sift`: sincronización y sifting en streaming (II = 1).
- `aurora_toeplitz`: amplificación de privacidad y extractor del TRNG.
- `aurora_ldpc_dec`: decodificador LDPC min-sum en coma fija (opcional; el A53 ya va sobrado).
- TRNG de osciladores en anillo con tests de salud SP 800-90B en hardware.
- Calefactor para pruebas de temperatura (solo con `VIVADO_ARGS=heater`).
- Recursos: 13 % LUT, 9 % FF, 20,5 % BRAM, 27 % URAM, 0 DSP; 3,76 W estimados.

**PS (C++, bare-metal y PC)**
- Cascade y LDPC (sum-product / min-sum) con autenticación Wegman–Carter de
  todas las fases (GF(2^128) + OTP; 1152 bits de clave por ronda, repuestos).
- Ventana de coincidencia ±400 ps por defecto (óptimo del barrido) y ventana
  adaptativa por batch (`auto`), sin mirar los bits.
- Compensación de retardos por detector (`calibrate-delays`, `--delays`).
- `aurora_cli stream`: procesa los batches según llegan a una carpeta
  (integración con la API del receptor).

## Resultados

| | |
|---|---|
| Datos reales | 101 batches, 42.142 bits cribados (±400 ps), QBER 1,87 % |
| Clave neta (Cascade, PA asintótica) | 28.686 bits (68 % de la cribada) |
| Placa frente a PC | sync, sift, Toeplitz y LDPC idénticos bit a bit |
| Tiempo por batch de 25 ms | sync 5,1 ms + sift 2,7 ms |
| LDPC en la PL frente al A53 | ×1,1–1,3, idéntico bit a bit |
| TRNG | H_min 0,76–0,81 (24–60 °C), test de reinicios superado (también con corte de alimentación); diseño con H = 0,5 |

## Limitaciones conocidas (ver docs/REVIEW.md)

- **Seguridad**: PA con la fórmula asintótica (sin tamaño finito); validar con UPM.
- **Detectores desajustados** (retardos y eficiencias): en H/V el tiempo da
  0,16 bits de información por bit sin compensar (0,025 compensando) y la
  clave cribada está sesgada (P(0) ≈ 0,38–0,40). Debe entrar en el análisis
  de seguridad antes de separar Alice y Bob o publicar las marcas de tiempo.
- Alice y Bob se procesan en el mismo equipo; el sifting de dos equipos debe
  intercambiar solo tiempos y bases (no los bits).
- La aplicación bare-metal no aplica aún la compensación de retardos.
- Sin la API del receptor todavía (el modo `stream` la espera por carpeta).

## Reproducir

```bash
source <Xilinx>/Vivado/2022.2/settings64.sh; source <Xilinx>/Vitis/2022.2/settings64.sh
make test-all                      # tests en PC (C++, Python, RTL, bare-metal simulado)
make hls-all && make bitstream     # 4 kernels HLS + bitstream (+ .xsa)
make baremetal && make board-run   # aplicacion en la ZCU104 (uart_capture.py en otra terminal)
```
