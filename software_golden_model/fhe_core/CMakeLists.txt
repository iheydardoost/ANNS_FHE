cmake_minimum_required(VERSION 3.12)
project(fhe_core_bin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure OpenFHE directory
set(OpenFHE_DIR "C:/OpenFHE/CMake")
find_package(OpenFHE REQUIRED)

# Configure OpenFHE include directories manually
set(OpenFHE_INCLUDE_DIRS
    "C:/OpenFHE/include"
    "C:/OpenFHE/include/openfhe"
    "C:/OpenFHE/include/openfhe/core"
    "C:/OpenFHE/include/openfhe/pke"
    "C:/OpenFHE/include/openfhe/binfhe"
    "C:/OpenFHE/include/openfhe/cereal"
)

# Include directories
include_directories(${OpenFHE_INCLUDE_DIRS})
include_directories(.)

# Enable OpenMP for parallel distance calculations
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    link_libraries(OpenMP::OpenMP_CXX)
endif()

# Source files
set(SOURCES
    fhe_config.cpp
    fhe_context_manager.cpp
    plaintext_searcher.cpp
    sign_approximator.cpp
    fhe_searcher.cpp
    main.cpp
)

# Headers (for IDE visibility)
set(HEADERS
    fhe_config.h
    fhe_context_manager.h
    plaintext_searcher.h
    sign_approximator.h
    fhe_searcher.h
)

# Executable target
add_executable(fhe_core_bin ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(fhe_core_bin PRIVATE
    OPENFHEpke
    OPENFHEcore
)

# Compilation flags
if(MSVC)
    target_compile_options(fhe_core_bin PRIVATE /W4 /WX- /Od /EHsc)
else()
    target_compile_options(fhe_core_bin PRIVATE -Wall -Wextra -O3 -pthread)
endif()
