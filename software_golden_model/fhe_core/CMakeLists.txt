cmake_minimum_required(VERSION 3.12)
project(fhe_core_bin CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure OpenFHE directory
set(OpenFHE_DIR "C:/OpenFHE/CMake")
find_package(OpenFHE REQUIRED)

# OpenFHE include directories.
# These are treated as SYSTEM includes so that compiler warnings from
# third-party headers (OpenFHE, NTL, cereal, etc.) are silenced.
# We only see warnings from our own source files.
set(OpenFHE_INCLUDE_DIRS
    "C:/OpenFHE/include"
    "C:/OpenFHE/include/openfhe"
    "C:/OpenFHE/include/openfhe/core"
    "C:/OpenFHE/include/openfhe/pke"
    "C:/OpenFHE/include/openfhe/binfhe"
    "C:/OpenFHE/include/openfhe/cereal"
)

# ── Own sources include path (warnings ON)
include_directories(.)
# nlohmann/json single-header (downloaded to third_party/)
include_directories(third_party)

# Enable OpenMP for parallel distance calculations (optional)
find_package(OpenMP)

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
    thread_pool.h
    third_party/nlohmann/json.hpp
)

# Executable target
add_executable(fhe_core_bin ${SOURCES} ${HEADERS})

# ── SYSTEM includes: OpenFHE headers silenced with -isystem / /external:I
# This is the standard CMake way to mark third-party headers as system headers,
# which suppresses warnings without disabling them for our own code.
target_include_directories(fhe_core_bin SYSTEM PRIVATE
    ${OpenFHE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(fhe_core_bin PRIVATE
    OPENFHEpke
    OPENFHEcore
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(fhe_core_bin PRIVATE OpenMP::OpenMP_CXX)
endif()

# ── Compiler flags
if(MSVC)
    target_compile_options(fhe_core_bin PRIVATE
        /W3           # Level-3 warnings for our own code (not /W4 to avoid noise)
        /WX-          # Warnings are not errors
        /O2           # Optimize (was /Od which means no optimization)
        /EHsc         # Standard C++ exception handling
        /wd4100       # unreferenced formal parameter (appears inside OpenFHE macros)
        /wd4127       # conditional expression is constant
        /wd4244       # conversion, possible loss of data (int ↔ size_t)
        /wd4267       # conversion from size_t to int
        /wd4305       # truncation from double to float
        /wd4456       # declaration of local hides previous local
        /wd4458       # declaration of member hides class member
        /wd4459       # declaration hides global declaration
        /external:anglebrackets   # treat angle-bracket includes as external
        /external:W0              # zero warnings from external/system headers
    )
else()
    # GCC / Clang: enable useful warnings for our code, silence third-party noise
    target_compile_options(fhe_core_bin PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter         # OpenFHE headers have many unused params
        -Wno-unused-variable          # ditto
        -Wno-sign-compare             # size_t vs int is pervasive in FHE code
        -Wno-deprecated-declarations  # cereal uses deprecated std features
        -Wno-maybe-uninitialized      # false positives in OpenFHE templates
        -O3
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(fhe_core_bin PRIVATE
            -Wno-class-memaccess      # GCC-specific for OpenFHE's internal bitops
        )
    endif()
endif()
