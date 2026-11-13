## =============================================================================
## AURORA TRNG - Restricciones de Vivado (plantilla)
## =============================================================================
## 1. Permitir los bucles combinacionales de los osciladores en anillo.
##    Ajustar la ruta jerarquica a la instancia real en el block design.
set_property ALLOW_COMBINATORIAL_LOOPS TRUE [get_nets -hierarchical -filter {NAME =~ *u_src/g_ro*u_ro/chain*}]

## 2. Los osciladores no son caminos de temporizacion: se excluyen del analisis
##    hasta el primer flip-flop del sincronizador.
set_false_path -to [get_cells -hierarchical -filter {NAME =~ *u_src/s1_reg}]

## 3. (Recomendado) Colocar cada oscilador en su propia region para que el
##    enrutado no cambie entre compilaciones y no se acoplen entre si.
##    Ejemplo para el oscilador 0 (ajustar SLICE_X/Y al dispositivo):
# create_pblock pb_ro0
# add_cells_to_pblock pb_ro0 [get_cells -hierarchical -filter {NAME =~ *u_src/g_ro?0?.u_ro/*}]
# resize_pblock pb_ro0 -add {SLICE_X40Y60:SLICE_X41Y61}
# set_property IS_SOFT FALSE [get_pblocks pb_ro0]
