# =============================================================================
# AURORA - Sintesis HLS de un kernel nuevo (Vitis HLS 2023.2)
#   vitis_hls -f hw/scripts/run_hls.tcl -tclargs aurora_sync
#   vitis_hls -f hw/scripts/run_hls.tcl -tclargs aurora_sift  [csim|csynth|cosim|export]
# Por defecto ejecuta csim + csynth. Se lanza desde la raiz del repositorio.
# Los proyectos se generan en build/hls/<kernel>/ (fuera de git).
# =============================================================================
set kernel [lindex $argv 0]
if {$kernel eq ""} { set kernel aurora_sync }
set steps [lrange $argv 1 end]
if {[llength $steps] == 0} { set steps {csim csynth} }

set root [pwd]
open_project -reset build/hls/$kernel
set_top $kernel
add_files hw/$kernel.cpp -cflags "-I$root/hw"
add_files hw/aurora_kernels.h
# Testbench: la bateria de tests en C++ (incluye los modelos de referencia).
# (los otros kernels se anaden como parte del testbench; el top ya esta en el diseno)
set tb_files {sw/aurora_host.cpp tests/cpp/test_kernels.cpp}
foreach k {aurora_sync aurora_sift aurora_toeplitz} {
    if {$k ne $kernel} { lappend tb_files hw/$k.cpp }
}
add_files -tb $tb_files \
    -cflags "-I$root/hw -I$root/sw -I$root/tests/cpp -std=c++17 -Wno-unknown-pragmas"

open_solution -reset solution1 -flow_target vivado
set_part {xczu7ev-ffvc1156-2-e}
create_clock -period 5 -name default

foreach s $steps {
    switch $s {
        csim   { csim_design }
        csynth { csynth_design }
        cosim  { cosim_design -trace_level none }
        export { export_design -format ip_catalog }
    }
}
exit
