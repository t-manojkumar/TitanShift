cmake_minimum_required(VERSION 3.20)
project(TitanShift VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimisation flags
if(MSVC)
    add_compile_options(
        /O2          # Full optimisation
        /Ob2         # Inline any suitable
        /Oi          # Intrinsic functions
        /Ot          # Favour fast code
        /GL          # Whole-program optimisation
        /arch:AVX2   # AVX2 SIMD instructions
        /W3
        /WX-
        /MP          # Multi-processor compilation
        /GS-         # Disable buffer security checks (perf)
    )
    add_link_options(/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS)
endif()

# Source files
set(SOURCES
    src/main.cpp
    src/TitanShiftApp.cpp
    src/FileEngine.cpp
    resources/titanshift.rc
)

# Headers
include_directories(include)

# Windows subsystem (no console window)
add_executable(TitanShift WIN32 ${SOURCES})

# Link Windows system libraries
target_link_libraries(TitanShift PRIVATE
    d2d1
    dwrite
    comctl32
    shlwapi
    shell32
    pdh
    psapi
    ntdll
    dwmapi
    ole32
    oleaut32
    uuid
)

# Set output name
set_target_properties(TitanShift PROPERTIES
    OUTPUT_NAME "TitanShift"
    WIN32_EXECUTABLE TRUE
)

# Install
install(TARGETS TitanShift DESTINATION .)
