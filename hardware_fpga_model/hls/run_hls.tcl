open_project -reset fhe_accel_prj
set_top fhe_accel_top
add_files src/fhe_accel_top.cpp -cflags "-std=c++17"
add_files src/key_switch.cpp -cflags "-std=c++17"
add_files src/rescale.cpp -cflags "-std=c++17"
add_files src/ntt.cpp -cflags "-std=c++17"
add_files src/mod_arith.cpp -cflags "-std=c++17"
add_files src/poly_arith.cpp -cflags "-std=c++17"
add_files src/automorphism.cpp -cflags "-std=c++17"
add_files -tb src/tb_fhe_accel.cpp -cflags "-std=c++17 -Wno-unknown-pragmas"
open_solution "solution1" -reset
set_part {xcvu37p-fsvh2892-2-e}
create_clock -period 5.0 -name default
csynth_design
exit
