############################################################
## This file is generated automatically by Vitis HLS.
## Please DO NOT edit it.
## Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
## Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
############################################################
open_project aurora_hls
set_top compute_correlation_hls
add_files aurora_hls/src/quantum_kernels.cpp -cflags "-I"
add_files -tb aurora_hls/src/main.cpp -cflags "-Iaurora_hls/.apc" -csimflags "-Iaurora_hls/.apc"
add_files -tb aurora_hls/src/quantum_kernels.h
open_solution "solution1" -flow_target vivado
set_part {xczu7ev-ffvc1156-2-e}
create_clock -period 10 -name default
#source "./aurora_hls/solution1/directives.tcl"
csim_design
csynth_design
cosim_design
export_design -format ip_catalog
