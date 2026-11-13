# =============================================================================
# AURORA - Programa la placa por JTAG y lanza la aplicacion bare-metal (XSCT)
# =============================================================================
#   xsct hw/scripts/run_board.tcl          (o: make board-run)
# Placa en modo de arranque JTAG, cable USB-JTAG conectado y alimentada.
# Antes, en otra terminal: python3 tools/uart_capture.py --port /dev/ttyUSB1
# =============================================================================
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
con
puts "AURORA: aplicacion lanzada; la salida va por la UART."
