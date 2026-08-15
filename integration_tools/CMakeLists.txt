cmake_minimum_required(VERSION 3.12)
project(test_vector_generator_bin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure OpenFHE directory
set(OpenFHE_DIR "C:/OpenFHE/CMake")
find_package(OpenFHE REQUIRED)

set(OpenFHE_INCLUDE_DIRS
    "C:/OpenFHE/include"
    "C:/OpenFHE/include/openfhe"
    "C:/OpenFHE/include/openfhe/core"
    "C:/OpenFHE/include/openfhe/pke"
    "C:/OpenFHE/include/openfhe/binfhe"
    "C:/OpenFHE/include/openfhe/cereal"
)

# We include the software_golden_model/fhe_core so we can use fhe_config.h if needed,
# though we'll likely generate vectors directly with specific HW params.
include_directories(../software_golden_model/fhe_core)
include_directories(.)

set(SOURCES
    test_vector_generator.cpp
)

add_executable(test_vector_generator_bin ${SOURCES})

target_include_directories(test_vector_generator_bin SYSTEM PRIVATE
    ${OpenFHE_INCLUDE_DIRS}
)

target_link_libraries(test_vector_generator_bin PRIVATE
    OPENFHEpke
    OPENFHEcore
)

if(MSVC)
    target_compile_options(test_vector_generator_bin PRIVATE
        /W3 /WX- /O2 /EHsc
        /external:anglebrackets /external:W0
    )
else()
    target_compile_options(test_vector_generator_bin PRIVATE
        -Wall -Wextra -O3
    )
endif()
