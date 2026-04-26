cmake_minimum_required(VERSION 3.20)
project(TitanShift VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    add_compile_options(-O2 -Wall -Wno-unused-variable)
    add_link_options(-mwindows -static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    add_compile_options(/O2 /GL /W3 /WX- /MP)
    add_link_options(/LTCG /OPT:REF /SUBSYSTEM:WINDOWS)
endif()

# All files are flat in the same directory
set(SOURCES
    main.cpp
    TitanShiftApp.cpp
    FileEngine.cpp
)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(TitanShift WIN32 ${SOURCES} titanshift.rc)

target_link_libraries(TitanShift PRIVATE
    d2d1
    dwrite
    comctl32
    shlwapi
    shell32
    pdh
    psapi
    ole32
    oleaut32
    uuid
    dwmapi
)
