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

# Kernels que trae el .xsa (el BSP genera un driver por kernel). Si falta
# aurora_ldpc_dec, la aplicacion usa el modelo SW del LDPC (y lo avisa).
proc find_all {dir pattern} {
    set out [glob -nocomplain -directory $dir $pattern]
    foreach d [glob -nocomplain -type d -directory $dir *] { set out [concat $out [find_all $d $pattern]] }
    return $out
}
foreach k {aurora_sync aurora_sift aurora_toeplitz aurora_ldpc_dec} {
    set h [find_all [file join $ws aurora_pf] "x${k}_hw.h"]
    puts [format "AURORA: kernel %-16s %s" $k [expr {[llength $h] ? "presente" : "NO presente en el .xsa"}]]
}
puts "AURORA: .xsa usado: $xsa ([clock format [file mtime $xsa] -format {%Y-%m-%d %H:%M}])"

# Aplicacion C++ vacia + nuestras fuentes. El nombre de la plantilla cambia
# entre versiones de Vitis: se prueban las variantes conocidas.
set candidates {
    {-template {Empty Application(C++)}}
    {-template {Empty Application (C++)}}
    {-template {Empty Application(C)} -lang c++}
    {-template {Empty Application (C)} -lang c++}
    {-template {Empty Application} -lang c++}
}
set created 0
foreach opts $candidates {
    if {![catch {app create -name aurora_test -platform aurora_pf -domain $dom {*}$opts} err]} {
        puts "AURORA: aplicacion creada con: $opts"
        set created 1
        break
    }
    puts "AURORA: '$opts' no valido"
    catch {app remove aurora_test}
}
if {!$created} {
    puts "AURORA: plantillas disponibles en esta instalacion:"
    catch {puts [repo -apps]}
    error "No se pudo crear la aplicacion: envia la lista de plantillas de arriba"
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
