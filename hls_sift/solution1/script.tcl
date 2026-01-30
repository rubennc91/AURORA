############################################################
## This file is generated automatically by Vitis HLS.
## Please DO NOT edit it.
## Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
## Copyright 2022-2024 Advanced Micro Devices, Inc. All Rights Reserved.
############################################################
open_project hls_sift
add_files hls_sift/srcs/quantum_kernels.cpp
add_files hls_sift/srcs/quantum_kernels.h
add_files -tb hls_sift/srcs/main.cpp
open_solution "solution1" -flow_target vivado
set_part {xczu7ev-ffvc1156-2-e}
create_clock -period 10 -name default
#source "./hls_sift/solution1/directives.tcl"
csim_design
csynth_design
cosim_design
export_design -format ip_catalog
