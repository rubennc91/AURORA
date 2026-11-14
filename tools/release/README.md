# AURORA @VERSION@ — paquete listo para la placa

Todo va ya compilado (bitstream, inicialización del PS y aplicación bare-metal),
así que **no hace falta Vitis ni compilar nada**. Commit: `@COMMIT@`.

## Requisitos del PC (Linux x86-64)

- `xsdb` de **Vivado Lab Edition 2022.2** (gratuita, ~1 GB) o de un Vivado/Vitis 2022.2
  completo, con los drivers del cable instalados
  (`<Xilinx>/Vivado_Lab/2022.2/data/xicom/cable_drivers/lin64/install_script/install_drivers/install_drivers`, como root).
- `python3` (sin paquetes extra) y `make`.
- Usuario en el grupo `dialout` (UART) y permiso sobre el relé (`usbrelay`).
- ZCU104 en **modo de arranque JTAG** (SW6 = ON, ON, ON, ON) y el cable micro-USB JTAG/UART conectado.

## Uso

```bash
make check            # comprueba ficheros, xsdb, relé y UART
make run              # enciende la placa, la programa, captura la salida y la APAGA
make analyze          # entropía del TRNG (última captura) y NIST SP 800-22 de la salida extraída
make off              # apaga el relé a mano (por si acaso)
```

`make run` termina con `RESULTADO: TODO OK (0 fallos)` y el relé de la placa
**se apaga siempre al terminar**, también si falla o se interrumpe con Ctrl+C.
El log y la captura del TRNG quedan en `results/`.

Opciones: `RELAY=959BI_1` (por defecto; `RELAY=none` si la placa se enciende a
mano), `PORT=/dev/ttyUSB1` (por defecto la UART del PS se busca en
`/dev/serial/by-id`), `XSDB=/ruta/a/xsdb` si no está en el PATH ni en `/tools/Xilinx`.

## Contenido

| | |
|---|---|
| `board/aurora.bit` | bitstream (4 kernels HLS + TRNG, sin calefactor) |
| `board/psu_init.tcl` | inicialización del PS (relojes, DDR, MIO) |
| `board/aurora_test.elf` | aplicación bare-metal de prueba (A53 #0) |
| `board/aurora.xsa` | plataforma hardware (para Vitis/PetaLinux, no hace falta para `make run`) |
| `bin/aurora_cli` | CLI del PC enlazada estáticamente (`pipeline`, `stream`, `trng-analyze`...) |
| `tools/` | lanzador por JTAG, sesión con el relé, captura de la UART, NIST SP 800-22 |
| `AURORA_informe.pdf`, `RELEASE_NOTES.md` | informe y notas de la versión |

Qué prueba la aplicación: captura del TRNG en bruto y autodiagnóstico; kernels
sync y sift frente a los resultados del PC (bit a bit) en 3 batches reales;
Toeplitz; cadena completa autenticada (Cascade) y LDPC en la PL frente al A53.
