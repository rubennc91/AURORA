# =============================================================================
# AURORA - Programa la ZCU104 por JTAG con ficheros ya compilados y lanza la
# aplicacion bare-metal (sin Vitis: vale con xsdb de Vivado Lab Edition 2022.2).
#
#   xsdb run_board.tcl <aurora.bit> <psu_init.tcl> <aurora_test.elf>
#
# Placa en modo de arranque JTAG, cable USB-JTAG conectado y alimentada.
# Es el mismo procedimiento que hw/scripts/run_board.tcl (modo normal).
# =============================================================================
if {[llength $argv] != 3} { error "uso: xsdb run_board.tcl <aurora.bit> <psu_init.tcl> <aurora_test.elf>" }
foreach {bit psuinit elf} $argv {}
foreach f [list $bit $psuinit $elf] {
    if {![file exists $f]} { error "no existe $f" }
}
set mailbox 0x7FFF0000   ;# buzon en DDR (sw/baremetal/aurora_board.h): 0 = prueba normal
puts "AURORA: bitstream $bit"
puts "AURORA: aplicacion $elf"

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
mwr $mailbox 0
con
puts "AURORA: aplicacion lanzada; la salida va por la UART."
disconnect
