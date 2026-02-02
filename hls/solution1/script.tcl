############################################################
## This file is generated automatically by Vitis HLS.
## Please DO NOT edit it.
## Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
## Copyright 2022-2024 Advanced Micro Devices, Inc. All Rights Reserved.
############################################################
open_project hls
set_top compute_correlation_hls
add_files src/quantum_kernels.cpp -cflags "-std=c++17"
add_files -tb hls/src/main.cpp -cflags "-I../include -std=c++17 -Wno-unknown-pragmas"
add_files -tb include/quantum_kernels.h -cflags "-Wno-unknown-pragmas"
open_solution "solution1" -flow_target vivado
set_part {xczu7ev-ffvc1156-2-e}
create_clock -period 10 -name default
source "./hls/solution1/directives.tcl"
csim_design
csynth_design
cosim_design
export_design -format ip_catalog
