# AURORA TRNG (osciladores en anillo)

Generador de números aleatorios verdadero para la PL del Zynq UltraScale+,
pensado para las semillas públicas del post-procesado (Cascade, verificación,
Toeplitz). Las claves de autenticación **no** salen de aquí: salen de la clave
precompartida o de la clave QKD.

```
osciladores (N) --XOR--> sincronizador --decimación--> tests de salud --> empaquetado --> FIFO --> AXI4-Lite
                                                      (RCT, APT, arranque)
```

| Fichero | Contenido |
|---|---|
| `trng_ro.v` | Oscilador en anillo (LUT1/LUT2 + DONT_TOUCH); modelo con jitter en simulación |
| `trng_source.v` | N osciladores, XOR, sincronizador de 2 FF, decimación e inyección de fallos |
| `trng_health.v` | Tests continuos SP 800-90B: Repetition Count, Adaptive Proportion y arranque |
| `trng_axi.v` | Top con registros AXI4-Lite (mapa en la cabecera del fichero y en `sw/aurora_trng.h`) |
| `tb_trng.v` | Testbench (Icarus Verilog o xsim, con `-DSIMULATION`) |
| `trng.xdc` | Plantilla de restricciones (bucles combinacionales, falsos caminos, pblocks) |

## Integración en Vivado
1. Añadir `trng_ro.v`, `trng_source.v`, `trng_health.v` y `trng_axi.v` como
   fuentes de diseño (**sin** `SIMULATION`), y `trng.xdc` como restricciones.
2. Instanciar `trng_axi` en el block design (puertos `s_axi_*` como AXI4-Lite
   esclavo), conectarlo al PS por un AXI Interconnect / SmartConnect y asignar
   una dirección base.
3. Tras la implementación, revisar que los anillos no se han optimizado y que
   el informe de DRC solo avisa de los bucles permitidos.

## Puesta en marcha y evaluación de la entropía (en la placa)
1. **Autodiagnóstico**: con `fault_sel` = 1, 2 y 3 deben saltar las alarmas
   (el testbench hace exactamente esa secuencia).
2. **Captura en bruto**: activar `gate_bypass`, leer ≥ 1 Mbit por `DATA` y
   guardarlo como una palabra hexadecimal por línea. Repetir a varias
   temperaturas y valores de `DECIM`.
3. **Análisis**:
   ```bash
   ./build/aurora_cli trng-analyze captura.txt --h 0.5 --extract salida.txt
   python3 tools/nist_sts.py salida.txt
   ```
   Fijar H_min por debajo del mínimo medido, recalcular los umbrales con
   `rct_cutoff(h)` / `apt_cutoff(h)` y escribirlos en `RCT_CUT` / `APT_CUT`.
4. En producción: `gate_bypass` = 0 y la salida siempre pasa por el
   extractor (`extract_random`) antes de usarse.

Para una evaluación formal conviene pasar las capturas por la herramienta
oficial de NIST (SP 800-90B EntropyAssessment), que implementa todos los
estimadores; aquí solo están MCV y Markov.
