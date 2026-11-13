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
- **Síntesis**: los kernels nuevos están escritos para HLS (sin memoria
  dinámica, bucles aplanados con II=1, acumulador con reenvío), pero **no se han
  sintetizado todavía**: hay que ejecutar `make hls KERNEL=...` y revisar
  latencia, II y recursos. Una mejora posible es mantener en el kernel de sync
  un buffer circular con los tags de Alice dentro de la ventana, para evitar
  relecturas por m_axi.
- Cascade se ejecuta en el PS. Si el throughput lo exige, el siguiente paso
  sería LDPC en la PL.
