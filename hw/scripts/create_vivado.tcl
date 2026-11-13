# =============================================================================
# AURORA - Crea el proyecto de Vivado con el block design completo
# =============================================================================
#   vivado -mode batch -source hw/scripts/create_vivado.tcl -tclargs [opciones]
#
# Opciones (en cualquier orden):
#   board=<board_part>   p. ej. board=xilinx.com:zcu104:part0:1.1 (por defecto:
#                        se busca una ZCU104 instalada; si no hay, solo el part)
#   part=<part>          por defecto xczu7ev-ffvc1156-2-e
#   bitstream            ademas lanza sintesis + implementacion + bitstream y
#                        exporta build/vivado/aurora.xsa (para Vitis/PetaLinux)
#   jobs=<n>             trabajos en paralelo (por defecto 4)
#
# Se lanza desde la RAIZ del repositorio. Genera build/vivado/ (ignorado por git).
# Contenido del block design:
#   * Zynq UltraScale+ (PS) con M_AXI_HPM0_FPD (registros) y S_AXI_HP0_FPD (DDR)
#   * TRNG (rtl/trng/trng_axi.v) como modulo RTL, esclavo AXI4-Lite
#   * Kernels HLS aurora_sync, aurora_sift, aurora_toeplitz y aurora_ldpc_dec si se han
#     exportado antes (build/hls/<kernel>/solution1/impl/ip); control por
#     AXI4-Lite y acceso a DDR por sus puertos m_axi a traves de S_AXI_HP0_FPD
#
# NOTA: este script no se ha podido probar en Vivado desde el entorno donde se
# escribio. Si algun paso falla, el mensaje de error de Vivado indica cual.
# =============================================================================

set part  xczu7ev-ffvc1156-2-e
set board ""
set do_bit 0
set jobs 4
foreach a $argv {
    if {[regexp {^board=(.*)$} $a -> v]} { set board $v }
    if {[regexp {^part=(.*)$}  $a -> v]} { set part $v }
    if {[regexp {^jobs=(.*)$}  $a -> v]} { set jobs $v }
    if {$a eq "bitstream"} { set do_bit 1 }
}

set root [pwd]
if {![file exists $root/rtl/trng/trng_axi.v]} {
    error "Lanza el script desde la raiz del repositorio AURORA"
}

# ------------------------------------------------------------- proyecto ---
create_project -force aurora $root/build/vivado -part $part

if {$board eq ""} {
    set cands [get_board_parts -quiet -latest_file_version {*zcu104*}]
    if {[llength $cands] > 0} { set board [lindex $cands 0] }
}
if {$board ne ""} {
    puts "AURORA: usando la placa $board"
    set_property board_part $board [current_project]
} else {
    puts "AURORA: AVISO - sin board files: el PS no tendra la configuracion de DDR/MIO de la placa."
    puts "AURORA:         Indica board=<board_part> o configura el PS a mano."
}

# RTL del TRNG + restricciones
add_files -norecurse [list \
    $root/rtl/trng/trng_ro.v \
    $root/rtl/trng/trng_source.v \
    $root/rtl/trng/trng_health.v \
    $root/rtl/trng/trng_axi.v ]
add_files -fileset constrs_1 -norecurse $root/rtl/trng/trng.xdc
update_compile_order -fileset sources_1

# IPs de los kernels HLS exportados
set hls_kernels {}
set ip_paths {}
foreach k {aurora_sync aurora_sift aurora_toeplitz aurora_ldpc_dec} {
    set ipdir $root/build/hls/$k/solution1/impl/ip
    if {[file exists $ipdir]} {
        lappend ip_paths $ipdir
        lappend hls_kernels $k
    } else {
        puts "AURORA: AVISO - no se encuentra el IP de $k ($ipdir); se omite."
        puts "AURORA:         Generalo con: vitis_hls -f hw/scripts/run_hls.tcl -tclargs $k csim csynth export"
    }
}
if {[llength $ip_paths] > 0} {
    set_property ip_repo_paths $ip_paths [current_project]
    update_ip_catalog
}

# --------------------------------------------------------- block design ---
create_bd_design aurora_bd

set ps_vlnv [lindex [lsort [get_ipdefs -all xilinx.com:ip:zynq_ultra_ps_e:*]] end]
if {$ps_vlnv eq ""} { error "No se encuentra el IP zynq_ultra_ps_e en el catalogo" }
set ps [create_bd_cell -type ip -vlnv $ps_vlnv zynq_ps]
if {$board ne ""} {
    apply_bd_automation -rule xilinx.com:bd_rule:zynq_ultra_ps_e -config {apply_board_preset "1"} $ps
}
# HPM0_FPD: registros de los perifericos. HPM1 apagado. HP0_FPD: DDR para los m_axi.
set_property -dict [list \
    CONFIG.PSU__USE__M_AXI_GP0 {1} \
    CONFIG.PSU__USE__M_AXI_GP1 {0} \
    CONFIG.PSU__USE__M_AXI_GP2 {0} \
    CONFIG.PSU__USE__S_AXI_GP2 {1} \
    CONFIG.PSU__FPGA_PL0_ENABLE {1} \
    CONFIG.PSU__CRL_APB__PL0_REF_CTRL__FREQMHZ {100} ] $ps

# TRNG (modulo RTL)
set trng [create_bd_cell -type module -reference trng_axi trng_0]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config [list \
    Clk_master {/zynq_ps/pl_clk0 (100 MHz)} Clk_slave {Auto} Clk_xbar {Auto} \
    Master {/zynq_ps/M_AXI_HPM0_FPD} Slave {/trng_0/s_axi} ddr_seg {Auto} \
    intc_ip {New AXI SmartConnect} master_apm {0}] [get_bd_intf_pins trng_0/s_axi]

# Kernels HLS: control por AXI4-Lite desde el PS (la automatizacion conecta
# tambien el reloj y el reset de cada kernel)
set hls_masters {}
foreach k $hls_kernels {
    set vlnv [lindex [get_ipdefs -filter "NAME == $k"] 0]
    if {$vlnv eq ""} { puts "AURORA: AVISO - $k no aparece en el catalogo de IP"; continue }
    set cell [create_bd_cell -type ip -vlnv $vlnv ${k}_0]
    apply_bd_automation -rule xilinx.com:bd_rule:axi4 -config [list \
        Clk_master {/zynq_ps/pl_clk0 (100 MHz)} Clk_slave {Auto} Clk_xbar {Auto} \
        Master {/zynq_ps/M_AXI_HPM0_FPD} Slave "/${k}_0/s_axi_control" ddr_seg {Auto} \
        intc_ip {Auto} master_apm {0}] [get_bd_intf_pins ${k}_0/s_axi_control]
    foreach m [get_bd_intf_pins -of_objects $cell -filter {MODE == Master}] { lappend hls_masters $m }
}

# Acceso a DDR: TODOS los puertos m_axi de los kernels entran en UNA SmartConnect
# conectada a S_AXI_HP0_FPD. (La automatizacion creaba una SmartConnect por
# puerto y solo conectaba la primera a la DDR.)
if {[llength $hls_masters] > 0} {
    if {[llength $hls_masters] > 16} { error "Demasiados puertos m_axi para una SmartConnect (max 16)" }
    set smc_vlnv [lindex [lsort [get_ipdefs -all xilinx.com:ip:smartconnect:*]] end]
    set smc [create_bd_cell -type ip -vlnv $smc_vlnv smc_ddr]
    set_property -dict [list CONFIG.NUM_SI [llength $hls_masters] CONFIG.NUM_MI {1}] $smc
    set i 0
    foreach m $hls_masters {
        connect_bd_intf_net $m [get_bd_intf_pins smc_ddr/[format "S%02d_AXI" $i]]
        incr i
    }
    connect_bd_intf_net [get_bd_intf_pins smc_ddr/M00_AXI] [get_bd_intf_pins zynq_ps/S_AXI_HP0_FPD]
    # Reloj y reset: los mismos que usan los kernels (pl_clk0 y el reset de perifericos)
    connect_bd_net [get_bd_pins zynq_ps/pl_clk0] [get_bd_pins smc_ddr/aclk]
    connect_bd_net [get_bd_pins zynq_ps/pl_clk0] [get_bd_pins zynq_ps/saxihp0_fpd_aclk]
    set rst [lindex [get_bd_cells -filter {VLNV =~ "*proc_sys_reset*"}] 0]
    if {$rst eq ""} { error "No se encuentra el proc_sys_reset creado por la automatizacion" }
    connect_bd_net [get_bd_pins $rst/peripheral_aresetn] [get_bd_pins smc_ddr/aresetn]
}

assign_bd_address
regenerate_bd_layout
validate_bd_design
save_bd_design

# Tabla de direcciones (para el software del PS)
set fp [open $root/build/vivado/address_map.txt w]
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces zynq_ps/Data]] {
    puts $fp [format "%-50s 0x%016X  %s" [get_property NAME $seg] [get_property OFFSET $seg] [get_property RANGE $seg]]
}
close $fp
puts "AURORA: mapa de direcciones en build/vivado/address_map.txt"

# Wrapper HDL y top
set bd_file [get_files aurora_bd.bd]
set wrapper [make_wrapper -files $bd_file -top]
if {$wrapper eq "" || ![file exists $wrapper]} {
    set wrapper [lindex [glob -nocomplain $root/build/vivado/aurora.gen/sources_1/bd/aurora_bd/hdl/aurora_bd_wrapper.v] 0]
}
if {$wrapper eq ""} { error "No se encuentra aurora_bd_wrapper.v" }
add_files -norecurse $wrapper
set_property top aurora_bd_wrapper [current_fileset]
update_compile_order -fileset sources_1

# ------------------------------------------------------------ bitstream ---
if {$do_bit} {
    launch_runs impl_1 -to_step write_bitstream -jobs $jobs
    wait_on_run impl_1
    if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} { error "La implementacion no termino" }
    write_hw_platform -fixed -include_bit -force $root/build/vivado/aurora.xsa
    puts "AURORA: bitstream y plataforma en build/vivado/aurora.xsa"
    # Resumen de temporizacion y recursos
    open_run impl_1
    report_timing_summary -file $root/build/vivado/timing_summary.rpt
    report_utilization    -file $root/build/vivado/utilization.rpt
    report_drc            -file $root/build/vivado/drc.rpt
}

puts "AURORA: proyecto creado en build/vivado/aurora.xpr"
