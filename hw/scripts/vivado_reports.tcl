# =============================================================================
# AURORA - Informes de recursos y potencia del diseno implementado
# =============================================================================
#   vivado -mode batch -nojournal -source hw/scripts/vivado_reports.tcl
# Requiere build/vivado/aurora.xpr con la implementacion hecha (make bitstream).
# Escribe en build/vivado/: utilization.rpt, utilization_hier.rpt, power.rpt.
# Despues: python3 tools/report_data.py  (genera docs/report/datos.tex)
# =============================================================================
set root [pwd]
open_project [file join $root build vivado aurora.xpr]
open_run impl_1
report_utilization -file [file join $root build vivado utilization.rpt]
report_utilization -hierarchical -hierarchical_depth 4 -file [file join $root build vivado utilization_hier.rpt]
report_power -file [file join $root build vivado power.rpt]
report_timing_summary -file [file join $root build vivado timing_summary.rpt]
puts "AURORA: informes en build/vivado/"
