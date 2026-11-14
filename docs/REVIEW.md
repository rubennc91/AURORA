# Revisión técnica de AURORA y plan de validación

Este documento recoge los fallos encontrados en la implementación anterior
(`hls/`, `hls_sift/`, `hls_parity/`, `hls_hashing/`), la evidencia que los
demuestra, el test que los deja documentados y cómo los resuelve la nueva
implementación (`hw/` + `sw/`).

Los batches de `batches/` son capturas reales del receptor, **sin ground truth**.
Por eso la validación combina:

1. **Datos sintéticos con la verdad conocida** (`tests/cpp/synth.h`): se inyectan
   el lag, el jitter, el QBER y el ruido, y se comprueba que se recuperan.
2. **Equivalencia con un modelo de referencia independiente** en Python
   (`tests/python/aurora_ref.py`) sobre los 101 batches reales.
3. **Plausibilidad física "en tiempo de vuelo"** sobre los batches reales:
   estabilidad del lag, anchura del pico, QBER, relación señal/accidentales,
   uso uniforme del batch.

## Cómo ejecutar

```bash
make test          # kernels nuevos (sintético) + batches reales vs Python
make test-legacy   # fallos del código anterior: deben salir como XFAIL
make pipeline      # sync -> sifting -> Cascade -> verificación -> PA en los 101 batches
```

Un test `KNOWN_BUG` / `expectedFailure` documenta un fallo real: **debe fallar**.
Si algún día pasa (XPASS), la suite falla para avisar de que hay que revisarlo.

## Fallos encontrados en la implementación anterior

| # | Fallo | Evidencia | Test que lo documenta | Solución |
|---|-------|-----------|-----------------------|----------|
| 1 | **Truncado a 4096 eventos en intervalos distintos.** Alice detecta ~1,4 MHz y Bob ~0,7 MHz: 4096 tags cubren ~3,3 ms en Alice y ~5,9 ms en Bob, de un batch de ~25 ms. | 3.932 bits cribados en 100 batches frente a 40.755 disponibles (~10 %). | `legacy_sift_covers_full_batch` | `sift_batch` recorre el batch entero en lotes de hasta 65.536 tags, troceando por tiempo de forma exacta (`sift_chunked_equals_single_pass`). |
| 2 | **SYNC no cuenta coincidencias.** `a_ptr` solo avanza al encontrar pareja y la búsqueda mira 64 posiciones: se atasca. | El máximo de la correlación es 0–9 pares cuando hay 75–95. | `legacy_sync_finds_coincidences` | Kernel `aurora_sync`: histograma de diferencias con dos punteros. |
| 3 | **Lag erróneo.** Consecuencia de (2) y de elegir el primer máximo de una meseta de ~700 ps. | Errores de hasta 360 ps frente a un pico con σ≈195 ps. | `legacy_sync_lag_is_accurate` | Paso grueso + fino y centroide con el fondo restado; error de ~5 ps en datos sintéticos. |
| 4 | **Desbordamiento `uint64_t`** en `target - window` con lag negativo cerca de t=0. | Se pierde 1 de 3 coincidencias. | `legacy_sift_no_unsigned_underflow` | Aritmética con signo (`int64_t`) en todos los kernels. |
| 5 | **PA insegura.** `lfsr >> (j % 64)` repite columnas cada 64 bits; no es una matriz Toeplitz. | Rango GF(2) = 32 para 1000→500 bits; colisiones triviales. | `legacy_pa_matrix_has_full_rank`, `legacy_pa_output_depends_on_all_input_bits` | Kernel `aurora_toeplitz` (Toeplitz real, 2-universal); rango completo verificado. |
| 6 | **El shuffle no es una permutación** para n = 901, 1802, 2703, 3604. | Solo 53 índices distintos con n=901. | `legacy_shuffle_is_permutation` | Fisher-Yates con generador compartido y muestreo sin sesgo. |
| 7 | **No hay clave de Bob ni corrección de errores.** En los tests, la clave de Bob era una copia de la de Alice, con un bit invertido a mano. | Lectura del código. | — (de diseño) | `aurora_sift` produce ambas claves; Cascade completo en el PS. |
| 8 | **Latencia de SYNC**: 2,16·10⁹ ciclos = 21,6 s por batch de 25 ms. | Informe de csynth. | `test_legacy_sync_meets_realtime` | O(N_A + N_B + pares): ~10⁵ ciclos estimados (pendiente de csynth). |
| 9 | **Margen de timing**: 9,62 ns estimados frente a 7,3 ns de presupuesto (10 − 2,7). | Informe de csynth. | `test_legacy_timing_has_margin` | Pendiente de verificar con csynth de los kernels nuevos. |
| 10 | Longitud de PA fija en 2:1 y semilla fija en el código. | Lectura del código. | — | `pa_output_length` a partir del QBER y de la información filtrada. |

## Observaciones sobre las capturas reales

- El desfase real Alice−Bob es ≈0 ps y el pico de coincidencias tiene σ≈195 ps:
  la ventana de ±350 ps es adecuada.
- **Cada batch tiene su propio desfase** (~77 ps rms entre batches, sin
  correlación entre batches consecutivos), muy por encima del error del
  centroide (~7 ps). Dentro de un batch el lag es estable (sin deriva de reloj
  apreciable). Conclusión: hay que sincronizar cada batch; no se puede
  reutilizar un lag global.
- QBER global 1,72 % (700 errores en 40.755 bits). Cascade lo corrige todo con
  eficiencia f≈1,17, y la clave final es ~72 % de la clave cribada.
- Los ficheros `*.sifted_key` no se han podido reproducir. Si vienen de un
  pipeline de referencia, sería útil saber cuál para usarlos como verdad.

## Autenticación del canal clásico (Wegman-Carter)

`sw/aurora_auth.{h,cpp}`. Sin autenticación, un atacante en medio puede
suplantar a Alice o a Bob y la QKD no protege nada.

- **MAC**: `tag = PolyHash_k(dirección ‖ secuencia ‖ mensaje) ⊕ r`. PolyHash es
  la evaluación polinómica en GF(2¹²⁸) con x¹²⁸ = x⁷ + x² + x + 1 y un bloque
  final de longitud (familia ε-AXU, ε ≤ (L+1)/2¹²⁸). `k` (128 bits) se toma una
  vez por sesión; `r` (128 bits) es de un solo uso y no se reutiliza nunca.
- **Reserva de clave** (`KeyPool`): empieza con una clave precompartida y se
  repone con clave QKD. Los bits consumidos se borran. Cada etiqueta lleva el
  offset de clave usado; el receptor rechaza si no coincide con el suyo
  (repetición o desincronización).
- **Protocolo**: se autentica la transcripción completa de cada fase
  (parámetros, todas las paridades de Cascade, verificación, semilla y
  longitud de PA), en ambos sentidos. **No se entrega clave hasta que todas
  las etiquetas verifican.**
- **Coste**: 1.152 bits por ronda (128 de `k` + 4 fases × 2 sentidos × 128).
  Sobre los 101 batches: 29.523 bits tras PA, 1.152 vuelven a la reserva y
  se entregan **28.371 bits netos**.
- Cascade tiene ahora una cota de trabajo: con paridades alteradas por un
  atacante podía no terminar y agotar la memoria.

Tests (`tests/cpp/test_auth.cpp`, `tests/python/test_auth_reference.py`):
axiomas de cuerpo de GF(2¹²⁸) e inversos; rechazo de cualquier bit alterado
en el mensaje o en la etiqueta, dirección, secuencia, offset y repetición;
detección de ataques sobre paridades de Cascade, mensajes y etiquetas de cada
fase; funcionamiento sostenido con una clave precompartida mínima; y contraste
del hash con una implementación independiente en Python.

**Pendiente**: cuando Alice y Bob funcionen como procesos separados, los
anuncios de sifting (tiempos y bases de Bob, máscara de Alice) también deben
entrar en la transcripción autenticada.

## TRNG de osciladores en anillo

RTL en `rtl/trng/` (ver su README para la integración en Vivado) y software en
`sw/aurora_trng.{h,cpp}` y `sw/aurora_trng_driver.{h,cpp}`.

- **Hardware**: N osciladores en anillo (LUTs con DONT_TOUCH) combinados con
  XOR, sincronizador de 2 FF y decimación. Tests de salud continuos de
  SP 800-90B (Repetition Count y Adaptive Proportion) con test de arranque;
  no sale ningún bit hasta superar el arranque ni después de una alarma.
  Inyección de fallos (bloqueo, sesgo, osciladores apagados) para el
  autodiagnóstico, modo bypass solo para capturas de evaluación, FIFO y
  AXI4-Lite.
- **Umbrales** calculados según SP 800-90B (para H_min = 0,5: RCT 41, APT 793;
  para H = 1 dan 21 y 589, los valores de la norma).
- **Software**: modelo exacto de los tests de salud, estimadores de
  min-entropía MCV y Markov, extractor Toeplitz y driver con autodiagnóstico
  al iniciar (si los tests de salud del hardware no funcionan, no arranca).
- **Validación**:
  - Simulación RTL con Icarus Verilog (`make test-trng`): 22 comprobaciones
    del testbench; el índice exacto de la muestra que dispara cada alarma
    coincide entre RTL, modelo C++ y modelo Python; la salida de la FIFO es
    exactamente el flujo bruto aceptado tras el arranque; la salida del
    extractor pasa la batería NIST SP 800-22 con criterio de proporción.
  - Batería NIST SP 800-22 propia (`tools/nist_sts.py`), validada con los
    ejemplos numéricos de la norma.
  - Tests del driver contra un modelo del periférico que simula averías.
  - Mutaciones del RTL (umbral desplazado en uno, orden de bits del
    empaquetador) detectadas.
- **Limitaciones**: el modelo de simulación de los osciladores solo valida la
  lógica digital; la entropía real hay que medirla en la placa (ver
  `rtl/trng/README.md`). Solo hay 2 de los estimadores de SP 800-90B; para
  una evaluación formal, usar la herramienta oficial de NIST. RCT y APT no
  detectan patrones como 0101… (documentado en un test); el estimador de
  Markov sí.

## Síntesis HLS (Vitis HLS 2022.2) y rediseño en streaming

Primera síntesis de los kernels (objetivo 10 ns, xczu7ev):

| Kernel | Estimado | Ciclos/iteración | Causa |
|---|---|---|---|
| `aurora_sync` (v1) | 17,2 ns ❌ | ~7 | lectura `m_axi` cuya dirección depende de la iteración anterior |
| `aurora_sift` (v1) | 17,6 ns ❌ | ~14 | ídem |
| `aurora_toeplitz` | 7,3 ns ✅ | 1 | — |

`sync` y `sift` se han reescrito como **DATAFLOW en streaming**: lectores
secuenciales (ráfagas) → núcleo con las cabezas en registros → escritor. En
`sift`, el núcleo es una mezcla de dos flujos ordenados; en `sync`, una ventana
deslizante en registros (32 tags; el máximo observado en los datos reales es 7)
y un histograma con reenvío de las dos últimas escrituras. Si la ventana se
desborda, `complete = 0` y el PS no acepta el lag. El resultado es idéntico al
de la versión anterior (mismos tests de fuerza bruta y mismas claves que el
modelo Python en los 101 batches). Pendiente: confirmar II = 1 y el timing con
una nueva síntesis.

## Primera prueba en placa (ZCU104, bare-metal, 13/11/2026)

`make baremetal` + `make board-run`, aplicacion `sw/baremetal/main.cpp`.
Resultado: **TODO OK (0 fallos)**.

| Prueba | Resultado |
|---|---|
| TRNG en bruto | 32.768 palabras (1 Mbit) en 671 ms = 1,56 Mmuestras/s (100 MHz / 64), sin perdidas ni alarmas |
| TRNG autodiagnostico | Los 3 fallos inyectados disparan alarma; arranque correcto |
| TRNG extraido | 100.000 bits en 615 ms (162 kbit/s), equilibrados (z = 0,31) |
| sync + sift (3 batches) | Lag, coincidencias y claves de Alice y Bob **identicos bit a bit** al PC; ~5,1 ms sync + ~2,7 ms sift por batch de 25 ms (incluye el trabajo del ARM) |
| Toeplitz | Salida identica a la definicion (2000 -> 1300 bits en 0,34 ms) |
| Cadena completa autenticada (semillas del TRNG) | 40.755 bits cribados -> 29.529 tras PA -> **28.377 netos**, claves iguales, 709 ms |

### Entropia del TRNG (captura de 1 Mbit en bruto, temperatura ambiente)

Herramienta oficial NIST `SP800-90B_EntropyAssessment` (`ea_non_iid`, 1 bit/muestra):

| Estimador | H (bit/muestra) |
|---|---|
| Most Common Value | 0,937 (p(0) = 0,521: sesgo del 2 %) |
| Collision | 0,910 |
| Markov | 0,940 |
| **Compression** | **0,785** (el minimo) |
| t-Tuple / LRS | 0,912 / 0,970 |
| MultiMCW / Lag / MultiMMC / LZ78Y | 0,944 / 0,992 / 0,938 / 0,938 |
| **H_original** | **0,785** |

- La salida en bruto falla los tests NIST SP 800-22 por el sesgo (esperado;
  `serial_2`, que cancela el sesgo de primer orden, pasa: no hay correlacion
  apreciable entre bits consecutivos).
- La salida **extraida** (522.240 bits con H = 0,5) pasa los 9 tests
  SP 800-22 (p entre 0,28 y 0,92).
- Se mantiene **H_min = 0,5**: margen de 1,57x sobre el peor estimador.

Pendiente para una evaluacion SP 800-90B completa: repetir la captura en frio
y en caliente y el test de reinicios (1000 arranques x 1000 muestras).

### Test de reinicios (SP 800-90B 3.1.4)

`tools/restart_test.py` + modo de la aplicacion bare-metal que se elige con un
buzon en DDR (`board::MAILBOX`, 0x7FFF0000) que escribe `run_board.tcl restart
<n> <descarte> <apagado_us>` antes de lanzarla:

- `soft`: una sola programacion; el TRNG se para (enable = 0 apaga los
  osciladores) y se arranca 1000 veces. ~30 s.
- `power`: corte de alimentacion real con el rele USB (`usbrelay`), una fila por
  ciclo; reanudable. ~10 h para 1000 ciclos.
- `--skip 1024` evalua lo que de verdad usa produccion (el TRNG no entrega bits
  hasta superar el test de arranque de 1024 muestras); `--skip 0`, la fuente
  desde el primer instante.

Salida: `matrix.bin` para `ea_restart` de NIST y la prueba de cordura de
3.1.4.3 (frecuencia maxima del valor mas comun en filas y columnas frente al
valor critico de Binomial(1000, 2^-H_I), alpha = 0.01/4000), que ademas indica
cuantas muestras hay que descartar si el arranque es predecible.

Resultados en placa (modo `soft`, 1000 x 1000, H_I = 0,785228; `ea_restart -n`):

| Descarte | Cordura (X_max / umbral) | H_r | H_c | min(H_r, H_c, H_I) | Validacion |
|---|---|---|---|---|---|
| 0 | 564 / 648 | 0,779 | 0,803 | **0,779** | superada |
| 1024 | 567 / 648 | 0,781 | 0,778 | **0,778** | superada |
| 0 (corte de alimentacion real, 1000 ciclos con rele, ~11 h) | 564 / 648 | 0,810 | 0,795 | **0,785** | superada |

No hay transitorio de arranque: ya en la primera muestra tras habilitar los
osciladores el valor mas comun aparece con frecuencia 0,50-0,55 entre
reinicios. La entropia tras reinicio coincide con la de funcionamiento
continuo, asi que H_min = 0,5 sigue teniendo un margen de ~1,55x.
El modo `power` (corte de alimentacion real con el rele `959BI_1`) da el
mismo resultado: el TRNG queda validado frente a reinicios.

### Temperatura

La aplicacion imprime `TEMP <momento> ps=.. pl=..` (SYSMON del PS y del PL,
via AMS, solo como termometro) y `uart_capture.py` la copia como comentario al
principio de la captura. Procedimiento: no hace falta cambiar la temperatura de
la sala, sino la del chip: captura en frio (placa apagada un buen rato), normal
(15 min encendida) y caliente (ventilador tapado / caja, 20-30 min, < 100 C);
`ea_non_iid` en cada una y se toma el minimo.

| Captura | T PL / PS (C) | MCV | Collision | Markov | Compression | t-Tuple | H_original |
|---|---|---|---|---|---|---|---|
| Ambiente (13/11, sin sensor) | - | 0,937 | 0,910 | 0,940 | 0,785 | 0,912 | 0,785 |
| Frio (14/11, recien encendida) | 28,8 / 29,9 | 0,938 | 1,000 | 0,966 | 0,764 | 0,901 | **0,764** |
| Normal (14/11, >15 min, ventilador) | 29,8 / 31,8 | 0,939 | 1,000 | 0,971 | 0,786 | 0,910 | **0,786** |
| *Bitstream con calefactor:* referencia, calefactor apagado | 24,3 / 25,6 | | | | 0,829 | | **0,805** |
| Caliente, recien apagado el calefactor | 60,5 -> 52,8 / 55,9 | | | | 0,788 | | **0,788** |
| Caliente, calefactor encendido (25/32 bancos) | 60,4 / 56,5 | | | | 0,791 | | **0,791** |

Rango evaluado: 29-30 C en la PL (funcionamiento con ventilador). Sin
evaluacion en caliente: la cubren el margen de 1,5x sobre H = 0,5 y los tests
de salud continuos (detienen la salida si la fuente se degrada).

### Calefactor de la PL (prueba en caliente a distancia)

`rtl/heater/heater_axi.v`: 32 bancos x 512 osciladores en anillo de 3 etapas
(~49.000 LUT) que se encienden por software; watchdog en hardware (1 s sin
refresco => se apaga y lo marca). Solo entra en el bitstream con
`make bitstream VIVADO_ARGS=heater`, en 0xA00F_0000. Modo "heat" de la
aplicacion bare-metal (`make board-heat TARGET=70 HOLD=60 LIMIT=85`): enciende
bancos de uno en uno cada 2 s, regula a +-1 C del objetivo, corta si se supera
el limite (<= 95 C) y, con el objetivo estable HOLD segundos, captura el TRNG
con el calefactor encendido (`caliente_on`) y justo despues de apagarlo
(`caliente_off`, la referencia: sin el ruido de alimentacion del calefactor).
Si con todos los bancos no llega en 5 minutos, captura a la temperatura
maxima alcanzada. Simulacion RTL (3 tests: actividad por banco, watchdog) y
del modo completo en la plataforma simulada con un modelo termico.
Medido (14/11/2026): de 27,5 a 60 C en ~80 s, estable con 25/32 bancos y
el ventilador funcionando; con 32 bancos seguia subiendo (~70 C alcanzable).
Al apagarlo la PL baja de 60,5 a 52,8 C en 0,7 s. Entre 24 y 60 C la
entropia queda en 0,76-0,81 sin tendencia, y el ruido del calefactor no la
afecta.

## Origen de las semillas

Todas las semillas públicas las genera **Alice con el TRNG** (`RandomSource`;
en la placa `TrngRandomSource`, en el PC `SimulatedRandomSource`, que solo
sirve para pruebas). Se sacan nuevas en cada ronda, justo antes de usarse, y
viajan a Bob por el canal autenticado:

| Semilla | Bits | Cuándo |
|---|---|---|
| Cascade (permutaciones) | 64 | Al empezar la ronda |
| Verificación | 64 | Después de Cascade |
| Toeplitz de la PA | n + m − 1 por bloque (~70.000) | Después de la verificación |

La semilla de Toeplitz es **completa** (uniforme), como exige el Leftover Hash
Lemma; antes se expandía desde 64 bits con un PRNG, lo que dejaba solo 2⁶⁴
matrices posibles.

Semillas que **no** salen del TRNG en funcionamiento:
- **Semilla pública del extractor del TRNG** (~98.000 bits para H_min = 0,5):
  se genera **una vez** en la instalación con
  `aurora_cli gen-random extractor extractor_seed.txt` (generador del sistema
  operativo) y se guarda. Es pública: solo elige la función Toeplitz; la
  aleatoriedad la aporta el jitter de los osciladores.
- **Clave precompartida de autenticación**: secreta. Se genera una vez
  (`aurora_cli gen-random 4096 psk.txt`) y se instala de forma segura en
  ambos equipos; después se repone con clave QKD.

Si la fuente aleatoria falla (alarma del TRNG), la ronda se aborta sin
entregar clave (test `random_source_failure_aborts_without_key`).

## Fallo corregido durante el desarrollo del TRNG

`toeplitz_hash` devolvía **una clave de ceros** cuando la entrada superaba el
límite del kernel (131.072 bits), porque el kernel ignora esas entradas sin
avisar. En la amplificación de privacidad, con más de ~330 batches
acumulados, Alice y Bob habrían obtenido la misma clave toda a ceros y la
habrían dado por buena. Ahora las entradas grandes se procesan por bloques
(misma proporción de compresión, semilla distinta por bloque), con un test de
regresión (`toeplitz_hash_handles_inputs_larger_than_kernel`).

## Limitaciones conocidas de la implementación nueva

- **Seguridad**: la longitud de PA usa la fórmula asintótica de BB84, sin
  correcciones de tamaño finito ni estimación separada del error de fase. No
  es una prueba de seguridad; hay que validarla con el equipo de UPM.
- **Fuente aleatoria y clave precompartida en el CLI del PC**: el pipeline
  usa `SimulatedRandomSource` y una clave precompartida simulada (ambas
  avisadas en la salida). En la placa hay que usar `TrngRandomSource` y la
  clave instalada. En el sistema real deben salir de un QRNG y compartirse por
  un canal clásico autenticado.
- **Síntesis**: hecha (Vitis HLS 2022.2: 7,3 ns e II = 1 en los tres kernels;
  Vivado: WNS +3,34 ns). Falta incorporar al informe la utilizacion final
  (`hw/scripts/vivado_reports.tcl` + `tools/report_data.py`).
- Cascade se ejecuta en el PS. Hay una alternativa LDPC (ver abajo); si el
  throughput lo exige, su decodificador min-sum es el candidato a la PL.

## Reconciliacion LDPC (alternativa a Cascade)

`sw/aurora_ldpc.*`, `aurora_cli pipeline ... --ec ldpc`, 12 tests
(`tests/cpp/test_ldpc.cpp`). Sindrome en un solo sentido por trama (Alice ->
Bob: plan + sindrome; Bob -> Alice: exito + errores), familia de codigos IRA
deterministas (n = 16384, R = 0,50..0,95, sin ciclos de 4 hasta R = 0,90),
perforado *untainted* + acortado, QBER adaptativo, BP por capas (sum-product o
min-sum normalizado), tramas fallidas descartadas.

| f objetivo (Q = 1,72 %) | 1,20 | 1,25 | 1,30 | 1,35 | 1,40 | 1,45 |
|---|---|---|---|---|---|---|
| FER | 13 % | 8 % | 8 % | 2,5 % | 0/124 | 0/120 |

Sobre los 40.755 bits reales (QBER estimado = real): LDPC f = 1,40 y 27.241
bits netos con 6 mensajes; Cascade f = 1,15 y 28.475 bits con ~6.000 consultas.
Por defecto f = 1,40 (maximiza la clave neta). Mejoraria con tramas mayores y
distribuciones de grados optimizadas por evolucion de densidad.

Hallazgo durante el ajuste: con perforado aleatorio la FER se quedaba en
~20 % para cualquier f (dos bits perforados en la escalera de grado 2
compartiendo comprobacion no se recuperan nunca). El perforado *untainted*
lo corrige y lo comprueba un test.

### Decodificador LDPC en la PL (`hw/aurora_ldpc_dec.cpp`)

Min-sum normalizado por capas en coma fija (LLR y mensajes int8, posteriores
int16, alfa = 0,6875 = 1/2 + 1/4 - 1/16), codigo en URAM/BRAM que solo se
recarga al cambiar, interfaz m_axi (gmem0 entradas, gmem1 salidas). El mismo
fuente es el modelo SW (`LdpcAlgo::FIXED_MIN_SUM`, `aurora_cli pipeline ...
--ec ldpc-fixed`, `sw/baremetal/ldpc_sw_model.cpp`), asi que la comparacion
PL/PS de la seccion 6 de la aplicacion bare-metal es bit a bit.

- Alfa ajustado por simulacion (Q = 1,72 %, f = 1,45): FER 4 % con 0,6875,
  12 % con 0,75, 14 % con 0,8125, 39 % con 0,875, 40 % con 0,625.
- Necesita f = 1,50 (sum-product: 1,40) y tiene un suelo de error de ~1 %
  (300 tramas: 1,7 % con 1,50; 0,7 % con 1,55; 1,0 % con 1,60).
- Estimacion: 1 arista/ciclo, ~30 ms por trama; del orden del A53. Para
  acelerar de verdad: QC-LDPC y varias filas en paralelo.
- **Medido en el A53 (14/11/2026, bitstream sin el kernel)**: 40.755 bits
  reales en 502 ms (min-sum coma fija, 4 tramas, 1 fallida tras 100
  iteraciones) y 617 ms (sum-product flotante, 3 tramas, 0 fallidas);
  construccion de codigos 3,2 s una vez. Se generan en ~2,5 s: el PS ya va
  ~4 veces sobrado (la estimacion previa de 2-3 s era pesimista). Con las
  tasas actuales el kernel en la PL no es necesario para el LDPC.
- **Sintetizado** (14/11/2026): 7,3 ns estimados, WNS del diseno +3,241 ns,
  direccion 0xA004_0000 (la supuesta). HLS: 7.670 LUT, 4.805 FF, 89 BRAM18,
  24 URAM; implementado: 3.227 LUT, 4.046 FF, 41/5 BRAM36/18. Por iteracion,
  ~(d+7) ciclos/fila en la comprobacion y ~(2d+13) en la actualizacion: ~3,1 ms
  por iteracion con R = 0,80 a 100 MHz (el A53 tarda ~3,9 ms).
- **Medido en placa (14/11/2026)**: PL 383,8 ms frente a 501,5 ms del A53
  con el mismo algoritmo (x1,3), **identicos bit a bit** (claves, fuga, tramas
  fallidas e iteraciones); cadena autenticada con LDPC en la PL: 25.845 bits
  netos. La primera ejecucion con este bitstream uso aun una aplicacion
  compilada sin el driver del kernel (`make baremetal` ahora lo avisa).

## Algoritmo de correlacion recibido (`nuevo_algoritmo.cpp`, 13/11/2026)

No incluye la API: lee los mismos ficheros `.detections` y calcula lag,
coincidencias y QBER (sin reconciliacion ni PA). Lag = argmax de coincidencias
con paso de 1 ps en [-10, +10] ns (suma de intervalos); emparejamiento voraz;
ventana +-250 ps. Comparado sobre los 101 batches con la misma ventana:

| | Coincidencias | Cribados | QBER |
|---|---|---|---|
| Recibido (w = 250 ps) | 67.062 | 34.813 | 1,476 % |
| AURORA (w = 250 ps) | 66.413 | 34.485 | 1,438 % |

Lag: diferencia media -2 ps, desviacion 20 ps, maximo 51 ps. Maximizar las
coincidencias da ~1 % mas de pares pero tambien mas errores: clave neta
practicamente igual. Detalles: rango de busqueda fijo de +-10 ns (si el
desfase es mayor no lo detecta y devuelve el maximo del rango); primer maximo
de la meseta; validacion de estados `state < 0 && state > 3` nunca se cumple
(deberia ser `||`); si faltan los ficheros sigue con vectores vacios; la rama
de lag negativo funciona por aritmetica modular de uint64.

Ventana (lo que mas influye; clave neta con Cascade, PA asintotica):

| +-w (ps) | 150 | 200 | 250 | 300 | 350 | 400 | 450 | 500 | 600 |
|---|---|---|---|---|---|---|---|---|---|
| Cribados | 22.935 | 29.288 | 34.485 | 38.338 | 40.755 | 42.142 | 42.802 | 43.116 | 43.412 |
| QBER | 1,25 % | 1,37 % | 1,44 % | 1,56 % | 1,72 % | 1,87 % | 2,04 % | 2,24 % | 2,56 % |
| Neta | 16.678 | 21.256 | 24.945 | 27.315 | 28.381 | 28.686 | 28.339 | 27.562 | 26.197 |

Optimo en ~400 ps; con 250 ps se pierde un 13 % de clave frente a 400.
**Desde el 14/11/2026 la ventana por defecto es +-400 ps**
(`DEFAULT_HALF_WINDOW_PS` en `sw/aurora_host.h`, unica fuente para el
software, la aplicacion bare-metal, los datos embebidos y los tests): 42.142
bits cribados y 28.686 netos. Los resultados en placa anteriores a esa fecha
se midieron con +-350 ps.

## Ventana adaptativa por batch

`choose_window()` (sw/aurora_host.*): elige el semiancho de cada batch solo con
datos de tiempo (publicos): el histograma fino de la sincronizacion da los
pares verdaderos S(w) y los accidentales A(w) = fondo * 2w; con el perfil de
QBER de los pares verdaderos en funcion de |d| (calibrado con bloques ya
corregidos) se maximiza (S + A)(1 - (1 + f) h(Q)). No mira los bits del batch
(sin post-seleccion; test `window_choice_ignores_key_bits`). Uso:
`aurora_cli pipeline batches 0 100 auto`, `aurora_cli stream <dir> --window auto`.
Sobre los 101 batches elige +-400 ps en 84 y +-375 en 15: 28.678 bits netos
frente a 28.686 con +-400 fijo (igual: el perfil esta calibrado con estos
mismos datos). Su valor es adaptarse si cambian el fondo o el jitter (tests
sinteticos: ventana menor con mas fondo, mayor con mas jitter). La ventana
fija de +-400 ps sigue siendo el valor por defecto.

## Hallazgo: los detectores no son iguales (tiempo y eficiencia) - SEGURIDAD

Los dos extremos tienen detectores (Alice tambien registra detecciones: el
sistema parece de pares entrelazados). Con las coincidencias de las 16
combinaciones de estados (incluidas las de base distinta) se separan los
retardos de cada detector (ajuste d(a, b) = dA[a] - dB[b], residuo < 9,2 ps):

| Detector | H | V | D | A |
|---|---|---|---|---|
| Alice (ps) | +35,9 | -21,3 | -58,2 | +60,1 |
| Bob (ps) | -29,8 | +18,4 | -46,6 | +58,0 |

Y las eficiencias tampoco son iguales (todos los eventos): Bob H 20,2 %, V
27,7 %, D 19,3 %, A 32,8 %; Alice H 19,9 %, V 28,0 %, D 22,8 %, A 29,3 %.

Consecuencias:

1. **Fuga por tiempo.** En la base H/V, la diferencia de tiempos d de cada par
   da **0,162 bits de informacion sobre el bit** (D/A: 0,008). Si las marcas
   de tiempo se intercambian por un canal publico con resolucion de ps (lo
   normal para encontrar coincidencias con dos equipos), un espia las ve. Hoy
   la amplificacion de privacidad no lo descuenta. Compensando los retardos de
   cada detector antes de publicar los tiempos baja a 0,033 bits (queda la
   diferencia de forma del jitter entre detectores).
2. **Clave sesgada.** P(bit 0) = 0,38 en H/V y 0,40 en D/A (entropia de Shannon
   0,960 y 0,973 por bit; min-entropia menor). La formula de PA supone bits
   uniformes.
3. **Desajuste de eficiencia entre detectores**: es la condicion que explotan
   ataques conocidos (efficiency mismatch / time-shift). La prueba de
   seguridad tiene que contemplarlo.
4. **Explica el perfil de errores**: los pares de las colas del pico tienen
   mas errores (0,5 % en el centro, 2 % a 350-400 ps, 5 % a 400-450 ps).

### Compensacion implementada (provisional hasta que lo haga el etiquetador)

`aurora_cli calibrate-delays batches 0 100 retardos.txt` y `--delays
retardos.txt` en `pipeline` y `stream` (sw/aurora_host: `DelayCalibrator`,
`calibrate_detector_delays`, `apply_detector_delays`). Solo usa tiempos.
La ventana de pares (+-300 ps) recorta las colas anchas de los detectores
reales y la media de cada combinacion solo responde ~55 % a un desplazamiento
(medido desplazando un detector 50 ps), asi que la calibracion es iterativa
(8 pasadas; el residuo cae a la mitad en cada una). Retardos convergidos con
los 101 batches (ps, H V D A): Alice +59,7 -41,2 -116,6 +108,8; Bob -54,4
+35,9 -90,6 +109,1 (los de la tabla de arriba son los de una sola pasada).

Resultado: la informacion que da el tiempo sobre el bit en H/V baja de 0,162
a **0,025 bits** (D/A: 0,008). Coste: ~1 % de clave (28.333 bits netos con
+-350 ps, que pasa a ser el optimo con compensacion, frente a 28.686 sin
compensar con +-400 ps). 3 tests nuevos (retardos inyectados en datos
sinteticos: todas las combinaciones quedan centradas; orden y eventos
conservados; fichero). La aplicacion bare-metal aun no la aplica.

Recomendaciones: (a) calibrar y compensar el retardo de cada canal, mejor en
el propio etiquetador de tiempos (preguntar al proveedor); (b) igualar las
eficiencias o incluir el desajuste y el sesgo en el analisis de seguridad
(UPM); (c) no publicar las marcas de tiempo con mas resolucion de la
necesaria. Scripts del analisis: estimacion por estado y de la informacion
mutua con los mismos datos (101 batches, |d| <= 300-400 ps).

## Integracion con el sistema de recepcion (preparada, sin la API)

Lo que se sabe (reunion): conexion al sistema de recepcion (REST u otra API)
que entrega lotes con el mismo formato que los ficheros actuales, tanto de
Alice como de Bob. Los datos lo confirman: **cada batch es independiente**
(los tags de Bob empiezan en 0 en los 101 batches; los de Alice en
~0,31 ms y terminan en ~25,31 ms, frente a 25,00 ms de Bob: en los
extremos de cada batch hay ~0,3 ms sin pareja). La sincronizacion por batch
de AURORA ya cubre este caso. Sobre la referencia de tiempo: el desfase por
batch es de decenas de ps y sin deriva dentro del batch, lo que indica que
los tags de Alice y Bob ya estan en una base de tiempo comun (mismo equipo
o ya alineados por el sistema); hay que confirmarlo con el fabricante antes
de separar Alice y Bob en equipos distintos.

`aurora_cli stream <dir>`: vigila una carpeta, procesa cada batch
(`<nombre>_alice.detections` + `<nombre>_bob.detections`) en cuanto esta
completo (mismo tamano en dos sondeos), en orden natural, descarta los
corruptos o sin sincronizacion, y cada `--block-bits` bits cribados ejecuta la
cadena autenticada y anade la clave a `--out`. El cliente de la API solo tiene
que escribir los ficheros en esa carpeta. Probado en vivo con los 101 batches
y con 3 tests (`tests/python/test_stream.py`). Con bloques de 10.000 bits sale
~60 % de clave neta (la autenticacion cuesta 1.152 bits por bloque); con
40.000, ~70 %.

## Informe

`docs/report/` (LaTeX, compila en Overleaf; PDF incluido).
