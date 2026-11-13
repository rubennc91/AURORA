# Informe técnico AURORA (LaTeX)

- `main.tex`: informe (pdflatex; paquetes estándar, compila en Overleaf sin cambios).
- `datos.tex`: recursos, potencia y timing. Lo genera `tools/report_data.py`
  a partir de los informes de Vivado y Vitis HLS; lo que falte sale como
  *pendiente* en el PDF.
- `AURORA_informe.pdf`: versión compilada.

## Actualizar los datos de la implementación

```bash
vivado -mode batch -nojournal -source hw/scripts/vivado_reports.tcl   # utilizacion, potencia, timing
python3 tools/report_data.py                                         # -> docs/report/datos.tex
cd docs/report && pdflatex main.tex && pdflatex main.tex
```

## Overleaf

New Project → Upload Project → un .zip con `main.tex` y `datos.tex`
(`cd docs/report && zip informe.zip main.tex datos.tex`). Compilador: pdfLaTeX.
