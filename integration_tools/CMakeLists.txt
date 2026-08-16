cmake_minimum_required(VERSION 3.12)
project(test_vector_generator_bin CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure OpenFHE directory
if(NOT DEFINED OpenFHE_DIR)
    if(DEFINED ENV{OpenFHE_DIR})
        set(OpenFHE_DIR "$ENV{OpenFHE_DIR}")
    elseif(EXISTS "C:/OpenFHE/CMake")
        set(OpenFHE_DIR "C:/OpenFHE/CMake")
    endif()
endif()
find_package(OpenFHE REQUIRED)

if(DEFINED ENV{VITIS_HLS_INCLUDE})
    set(VITIS_INC "$ENV{VITIS_HLS_INCLUDE}")
elseif(DEFINED ENV{VITIS25})
    set(VITIS_INC "$ENV{VITIS25}/include")
elseif(EXISTS "D:/AMDDesignTools/2025.2/Vitis/include")
    set(VITIS_INC "D:/AMDDesignTools/2025.2/Vitis/include")
endif()

# We include the software_golden_model/fhe_core so we can use fhe_config.h if needed,
# though we'll likely generate vectors directly with specific HW params.
include_directories(../software_golden_model/fhe_core)
include_directories(.)
include_directories(${VITIS_INC})

set(SOURCES
    test_vector_generator.cpp
)

add_executable(test_vector_generator_bin ${SOURCES})
target_include_directories(test_vector_generator_bin SYSTEM PRIVATE ${OpenFHE_INCLUDE_DIRS})
target_link_libraries(test_vector_generator_bin PRIVATE OPENFHEpke OPENFHEcore)

add_executable(generate_interactive_test_vectors_bin generate_interactive_test_vectors.cpp)
target_include_directories(generate_interactive_test_vectors_bin SYSTEM PRIVATE ${OpenFHE_INCLUDE_DIRS})
target_link_libraries(generate_interactive_test_vectors_bin PRIVATE OPENFHEpke OPENFHEcore)

if(MSVC)
    target_compile_options(test_vector_generator_bin PRIVATE /W3 /WX- /O2 /EHsc /external:anglebrackets /external:W0)
    target_compile_options(generate_interactive_test_vectors_bin PRIVATE /W3 /WX- /O2 /EHsc /external:anglebrackets /external:W0)
else()
    target_compile_options(test_vector_generator_bin PRIVATE -Wall -Wextra -O3)
    target_compile_options(generate_interactive_test_vectors_bin PRIVATE -Wall -Wextra -O3)
endif()
