# =============================================================================
# AURORA - Sintesis HLS de un kernel nuevo (Vitis HLS 2022.2 / 2023.x)
#   vitis_hls -f hw/scripts/run_hls.tcl -tclargs aurora_sync
#   vitis_hls -f hw/scripts/run_hls.tcl -tclargs aurora_sift  [csim|csynth|cosim|export]
# Por defecto ejecuta csim + csynth. Se lanza desde la raiz del repositorio.
# Los proyectos se generan en build/hls/<kernel>/ (fuera de git).
# =============================================================================
# Vitis HLS pasa en $argv tambien sus propios argumentos (-f <script> ...),
# no solo los de -tclargs: se buscan los nombres conocidos y se ignora el resto.
set kernel ""
set steps {}
foreach a $argv {
    if {$a in {aurora_sync aurora_sift aurora_toeplitz}} { set kernel $a }
    if {$a in {csim csynth cosim export}} { lappend steps $a }
}
if {$kernel eq ""} { error "Indica el kernel: -tclargs aurora_sync|aurora_sift|aurora_toeplitz \[csim csynth cosim export\]" }
if {[llength $steps] == 0} { set steps {csim csynth} }
puts "AURORA: kernel $kernel, pasos: $steps"

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
    -cflags "-I$root/hw -I$root/sw -I$root/tests/cpp -std=c++14 -Wno-unknown-pragmas"

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
