open_project -reset fhe_accel_proj
set_top fhe_accel_top
add_files fhe_accel_top.cpp
add_files key_switch.cpp
add_files ntt.cpp
add_files poly_arith.cpp
add_files rescale.cpp
add_files automorphism.cpp

# Target Alveo U280
open_solution -reset "solution1" -flow_target vitis
set_part {xcu280-fsvh2892-2L-e}
create_clock -period 4.0 -name default

config_interface -m_axi_alignment_byte_size 64 -m_axi_latency 64 -m_axi_max_widen_bitwidth 512

csynth_design
close_project
exit
