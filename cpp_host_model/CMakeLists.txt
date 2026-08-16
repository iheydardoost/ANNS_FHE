cmake_minimum_required(VERSION 3.12)
project(fhe_interactive_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(DEFINED ENV{VITIS_HLS_INCLUDE})
    set(VITIS_INC "$ENV{VITIS_HLS_INCLUDE}")
elseif(DEFINED ENV{VITIS25})
    set(VITIS_INC "$ENV{VITIS25}/include")
elseif(EXISTS "D:/AMDDesignTools/2025.2/Vitis/include")
    set(VITIS_INC "D:/AMDDesignTools/2025.2/Vitis/include")
endif()

include_directories(
    src
    ../hardware_fpga_model/hls/src
    ${VITIS_INC}
)

set(HW_SOURCES
    ../hardware_fpga_model/hls/src/mod_arith.cpp
    ../hardware_fpga_model/hls/src/ntt.cpp
    ../hardware_fpga_model/hls/src/poly_arith.cpp
    ../hardware_fpga_model/hls/src/automorphism.cpp
    ../hardware_fpga_model/hls/src/rescale.cpp
    ../hardware_fpga_model/hls/src/key_switch.cpp
    ../hardware_fpga_model/hls/src/fhe_accel_top.cpp
)

set(HOST_SOURCES
    src/sim_memory_bus.cpp
    src/fhe_host_driver.cpp
    src/interactive_query_manager.cpp
    src/main_sim.cpp
)

add_executable(fhe_interactive_sim ${HW_SOURCES} ${HOST_SOURCES})

if(MSVC)
    target_compile_options(fhe_interactive_sim PRIVATE /O2 /EHsc)
else()
    target_compile_options(fhe_interactive_sim PRIVATE -O3 -Wno-deprecated-copy -Wno-class-memaccess -Wno-unknown-pragmas -Wno-unused-parameter -Wno-comment)
endif()
