## AURORA - Calefactor (solo se anade al proyecto con VIVADO_ARGS=heater)
## Bucles combinacionales permitidos: los osciladores en anillo de los bancos.
set_property ALLOW_COMBINATORIAL_LOOPS TRUE [get_nets -hierarchical -filter {NAME =~ *g_bank*u_ro/chain*}]
## Muestreo de actividad: asincrono, no es un camino de timing.
set_false_path -to [get_cells -hierarchical -filter {NAME =~ *heater*hs1_reg*}]
