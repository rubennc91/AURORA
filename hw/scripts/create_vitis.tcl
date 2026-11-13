# =============================================================================
# AURORA - Plataforma y aplicacion bare-metal en Vitis (XSCT)
# =============================================================================
#   xsct hw/scripts/create_vitis.tcl        (o: make baremetal)
# Requiere build/vivado/aurora.xsa (make bitstream) y las fuentes preparadas en
# build/baremetal_src (make baremetal las prepara). Se lanza desde la RAIZ del
# repositorio. Genera build/vitis_ws (ignorado por git).
# =============================================================================
set root [pwd]
set ws   [file join $root build vitis_ws]
set xsa  [file join $root build vivado aurora.xsa]
set src  [file join $root build baremetal_src]

if {![file exists $xsa]} { error "No existe $xsa: ejecuta antes 'make bitstream'" }
if {![file exists [file join $src main.cpp]]} { error "Faltan las fuentes en $src: usa 'make baremetal'" }

file delete -force $ws
file mkdir $ws
setws $ws

# Plataforma: standalone en el A53 #0 (incluye FSBL y PMUFW).
platform create -name aurora_pf -hw $xsa -proc psu_cortexa53_0 -os standalone -out $ws
platform write
platform generate
set dom [domain active]
puts "AURORA: dominio $dom"

# Aplicacion C++ vacia + nuestras fuentes.
if {[catch {app create -name aurora_test -platform aurora_pf -domain $dom -template {Empty Application(C++)}} err]} {
    puts "AURORA: plantilla 'Empty Application(C++)' no disponible ($err); se prueba con otro nombre"
    app create -name aurora_test -platform aurora_pf -domain $dom -template {Empty Application (C++)}
}
importsources -name aurora_test -path $src

app config -name aurora_test build-config release
foreach inc [list [file join $root sw] [file join $root hw] [file join $root sw baremetal]] {
    app config -name aurora_test -add include-path $inc
}
# -ffp-contract=off: misma aritmetica de coma flotante que en el PC (sin FMA).
app config -name aurora_test -add compiler-misc {-std=c++14 -ffp-contract=off}
# Pila y monton grandes (por defecto son de 8 KB).
app config -name aurora_test -add linker-misc {-Wl,--defsym=_HEAP_SIZE=0x20000000 -Wl,--defsym=_STACK_SIZE=0x400000}

app build -name aurora_test

set elf [file join $ws aurora_test Release aurora_test.elf]
if {![file exists $elf]} { error "No se genero $elf" }
puts "AURORA: aplicacion en $elf"
