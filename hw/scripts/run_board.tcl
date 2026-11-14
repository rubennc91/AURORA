# =============================================================================
# AURORA - Programa la placa por JTAG y lanza la aplicacion bare-metal (XSCT)
# =============================================================================
#   xsct hw/scripts/run_board.tcl          (o: make board-run)
# Placa en modo de arranque JTAG, cable USB-JTAG conectado y alimentada.
# Antes, en otra terminal: python3 tools/uart_capture.py --port /dev/ttyUSB1
#
# Test de reinicios del TRNG (lo lanza tools/restart_test.py):
#   xsct hw/scripts/run_board.tcl restart <n_reinicios> <descarte> <apagado_us>
# Calefactor (bitstream con VIVADO_ARGS=heater):
#   xsct hw/scripts/run_board.tcl heat <objetivo_C> <estable_s> <limite_C>
# =============================================================================
set mode "full"
if {[llength $argv] >= 1 && [lindex $argv 0] eq "restart"} {
    if {[llength $argv] != 4} { error "uso: run_board.tcl restart <n> <descarte> <apagado_us>" }
    set mode "restart"
    foreach {r_n r_skip r_off} [lrange $argv 1 3] {}
    foreach v [list $r_n $r_skip $r_off] {
        if {![string is integer -strict $v] || $v < 0} { error "parametro no valido: $v" }
    }
}
if {[llength $argv] >= 1 && [lindex $argv 0] eq "heat"} {
    if {[llength $argv] != 4} { error "uso: run_board.tcl heat <objetivo_C> <estable_s> <limite_C>" }
    set mode "heat"
    foreach {h_target h_hold h_limit} [lrange $argv 1 3] {}
    foreach v [list $h_target $h_hold $h_limit] {
        if {![string is double -strict $v] || $v < 0} { error "parametro no valido: $v" }
    }
    if {$h_limit > 95 || $h_target >= $h_limit} { error "limite <= 95 C y objetivo < limite" }
    # Solo si el bitstream lleva el calefactor (si no, el acceso a su direccion falla).
    set amap [file join [pwd] build vivado address_map.txt]
    if {![file exists $amap] || [catch {exec grep -qi heater $amap}]} {
        error "El bitstream no incluye el calefactor: make bitstream VIVADO_ARGS=heater && make baremetal"
    }
}
# Buzon en DDR (sw/baremetal/aurora_board.h: board::MAILBOX)
set mailbox 0x7FFF0000
set root [pwd]
set ws   [file join $root build vitis_ws]
set elf  [file join $ws aurora_test Release aurora_test.elf]
if {![file exists $elf]} { error "No existe $elf: ejecuta antes 'make baremetal'" }

proc find_file {dir pattern} {
    foreach f [glob -nocomplain -directory $dir $pattern] { return $f }
    foreach d [glob -nocomplain -type d -directory $dir *] {
        set r [find_file $d $pattern]
        if {$r ne ""} { return $r }
    }
    return ""
}
set bit     [find_file [file join $ws aurora_pf] *.bit]
set psuinit [find_file [file join $ws aurora_pf] psu_init.tcl]
if {$bit eq "" || $psuinit eq ""} { error "No se encuentran el .bit o psu_init.tcl en la plataforma" }
puts "AURORA: bitstream $bit"

connect
targets -set -nocase -filter {name =~ "*PSU*"}
rst -system
after 1500

# Inicializacion del PS (relojes, DDR, MIO) y programacion de la PL.
targets -set -nocase -filter {name =~ "*PSU*"}
source $psuinit
psu_init
after 1000
fpga -file $bit
after 500
psu_ps_pl_isolation_removal
psu_ps_pl_reset_config

# Aplicacion en el A53 #0.
targets -set -nocase -filter {name =~ "*A53*#0"}
rst -processor
dow $elf
if {$mode eq "restart"} {
    mwr $mailbox 0x52535452
    mwr [expr {$mailbox + 4}] $r_n
    mwr [expr {$mailbox + 8}] $r_skip
    mwr [expr {$mailbox + 12}] $r_off
    puts "AURORA: modo test de reinicios ($r_n reinicios, descarte $r_skip, apagado $r_off us)"
} elseif {$mode eq "heat"} {
    mwr $mailbox 0x48454154
    mwr [expr {$mailbox + 4}] [expr {int(round($h_target * 10))}]
    mwr [expr {$mailbox + 8}] [expr {int($h_hold)}]
    mwr [expr {$mailbox + 12}] [expr {int(round($h_limit * 10))}]
    mwr [expr {$mailbox + 16}] 0
    puts "AURORA: modo calefactor (objetivo $h_target C, estable $h_hold s, limite $h_limit C)"
} else {
    mwr $mailbox 0
}
con
puts "AURORA: aplicacion lanzada; la salida va por la UART."
