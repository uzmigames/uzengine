# UzEngine Brixelizer GI wrapper DLL.
#
# Builds `uz_brixelizer.dll` — a thin MSVC-built shared library that
# statically links the FFX SDK 1.1.4 backend / brixelizer / brixelizergi
# libraries and re-exports their public C entry points so the rest of
# UzEngine (compiled with Zig/mingw) can call them via LoadLibrary +
# GetProcAddress, the same way `dlss_upscaler.cpp` consumes `_nvngx.dll`.
#
# Build:
#   cmake -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# Output: build/bin/uz_brixelizer.dll → deployed by runtime/build.zig
#
# @ref AMD 2024 "FidelityFX Brixelizer GI"
# @ref UzEngine docs/analysis/dlss-integration-analysis.md — same MSVC/CRT
#                                                            workaround.

cmake_minimum_required(VERSION 3.20)
project(uz_brixelizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# All FFX SDK paths are relative to this wrapper directory.
set(FFX_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sdk")

if(NOT EXISTS "${FFX_SDK_ROOT}/include/FidelityFX/host/ffx_brixelizer.h")
    message(FATAL_ERROR
        "FFX SDK headers not found at ${FFX_SDK_ROOT}. "
        "Did you run setup.ps1?")
endif()

set(FFX_LIB_DIR "${FFX_SDK_ROOT}/bin/ffx_sdk")

if(NOT EXISTS "${FFX_LIB_DIR}/ffx_backend_dx12_x64.lib")
    message(FATAL_ERROR
        "Pre-built FFX SDK static libs not found at ${FFX_LIB_DIR}. "
        "Run the FFX SDK build first: "
        "cd ../sdk && cmake -B build -G \"Visual Studio 17 2022\" -A x64 "
        "-DFFX_API_BACKEND=DX12_X64 -DFFX_ALL=ON -DUZ_BRIXELIZER_OCT_NORMALS=ON && "
        "cmake --build build --config Release")
endif()

add_library(uz_brixelizer SHARED uz_brixelizer_dll.cpp)

target_include_directories(uz_brixelizer PRIVATE
    "${FFX_SDK_ROOT}/include"
    "${FFX_SDK_ROOT}/include/FidelityFX/host"
    "${FFX_SDK_ROOT}/include/FidelityFX/host/backends/dx12"
    "${FFX_SDK_ROOT}/include/FidelityFX/gpu"
    "${FFX_SDK_ROOT}/src/backends/shared"
    "${FFX_SDK_ROOT}/src/components/brixelizer"
    "${FFX_SDK_ROOT}/src/components/brixelizergi"
)

target_link_libraries(uz_brixelizer PRIVATE
    "${FFX_LIB_DIR}/ffx_backend_dx12_x64.lib"
    "${FFX_LIB_DIR}/ffx_brixelizer_x64.lib"
    "${FFX_LIB_DIR}/ffx_brixelizergi_x64.lib"
    d3d12.lib
    dxgi.lib
    dxguid.lib
)

# The pre-built FFX SDK 1.1.4 .libs were compiled with the dynamic MSVC
# runtime (`MD`), so the wrapper must match — otherwise the linker reports
# `LNK2038: 'RuntimeLibrary': mismatch detected, value 'MD_DynamicRelease'
# does not match value 'MT_StaticRelease'`. The resulting DLL depends on
# `msvcp140.dll` + `vcruntime140.dll`, which ship with every Windows 10/11
# install via the VC++ redistributable.
set_property(TARGET uz_brixelizer PROPERTY
             MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Output name (no Visual Studio config subdir on the file name itself).
set_target_properties(uz_brixelizer PROPERTIES
    OUTPUT_NAME "uz_brixelizer"
    PREFIX      ""
)

# Place the DLL in a predictable spot for setup.ps1 to deploy.
set_target_properties(uz_brixelizer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
