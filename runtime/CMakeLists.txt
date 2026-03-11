# ─────────────────────────────────────────────────────────────────────────────
# UzEngine Runtime — CMake Build
# Mirrors the Zig build system. Supports MSVC and Clang on Windows.
# Uses file(GLOB) for automatic source discovery — no manual file lists.
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.21)
project(uzengine_runtime LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler Flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Cross-Platform (auto-discovered via GLOB)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SRC_RUNTIME        src/*.cpp)
file(GLOB SRC_LOGGING        core/logging/*.cpp)
file(GLOB SRC_ASSERTIONS     core/assertions/*.cpp)
file(GLOB SRC_PROFILING      core/profiling/*.cpp)
file(GLOB SRC_CONFIG         core/config/*.cpp)
file(GLOB SRC_MATH           core/math/*.cpp)
file(GLOB SRC_RHI_COMMON     core/rhi/*.cpp)
file(GLOB SRC_PATHS          core/paths/*.cpp)
file(GLOB SRC_SHADER_COMMON  core/shader/*.cpp)
file(GLOB SRC_RENDERER       core/renderer/*.cpp)
file(GLOB SRC_ASSET          core/asset/*.cpp)

# Shader compiler and shader importer are Windows-only (require DXC)
if(NOT WIN32)
    list(FILTER SRC_SHADER_COMMON EXCLUDE REGEX "shader_compiler\\.cpp$")
    list(FILTER SRC_ASSET EXCLUDE REGEX "shader_importer\\.cpp$")
endif()

set(RUNTIME_SOURCES
    ${SRC_RUNTIME}
    ${SRC_LOGGING}
    ${SRC_ASSERTIONS}
    ${SRC_PROFILING}
    ${SRC_CONFIG}
    ${SRC_MATH}
    ${SRC_RHI_COMMON}
    ${SRC_PATHS}
    ${SRC_SHADER_COMMON}
    ${SRC_RENDERER}
    ${SRC_ASSET}
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Thirdparty (always compiled)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SPDLOG_SOURCES     thirdparty/spdlog/src/*.cpp)

set(TRACY_SOURCES
    thirdparty/tracy/public/TracyClient.cpp
)

set(SPIRV_REFLECT_SOURCES
    thirdparty/spirv-reflect/spirv_reflect.c
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Windows Only (auto-discovered via GLOB)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SRC_PLATFORM_WIN   platform/windows/*.cpp)
file(GLOB SRC_RHI_DX12       core/rhi/dx12/*.cpp)

# Exclude dx12_agility.cpp — only linked into executables, not the library
list(FILTER SRC_RHI_DX12 EXCLUDE REGEX "dx12_agility\\.cpp$")

set(WINDOWS_SOURCES
    ${SRC_PLATFORM_WIN}
    ${SRC_RHI_DX12}
)

# ─────────────────────────────────────────────────────────────────────────────
# Static Library
# ─────────────────────────────────────────────────────────────────────────────
add_library(uzengine_runtime STATIC
    ${RUNTIME_SOURCES}
    ${SPDLOG_SOURCES}
    ${TRACY_SOURCES}
    ${SPIRV_REFLECT_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_SOURCES}>
)

target_compile_definitions(uzengine_runtime PUBLIC
    SPDLOG_COMPILED_LIB
)

target_compile_definitions(uzengine_runtime PRIVATE
    TRACY_ENABLE
    TRACY_ON_DEMAND
)

# Per-file compiler flags for D3D12MA (suppresses upstream warnings)
if(WIN32)
    if(MSVC)
        set_source_files_properties(core/rhi/dx12/dx12_d3d12ma.cpp
            PROPERTIES COMPILE_OPTIONS "/wd4267;/wd4244"
        )
    else()
        set_source_files_properties(core/rhi/dx12/dx12_d3d12ma.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-switch;-Wno-unused-private-field"
        )
    endif()
endif()

# spirv_reflect.c is C, ensure it compiles as C11
set_source_files_properties(thirdparty/spirv-reflect/spirv_reflect.c
    PROPERTIES LANGUAGE C
)

# ─────────────────────────────────────────────────────────────────────────────
# Include Directories
# ─────────────────────────────────────────────────────────────────────────────

# Engine includes
target_include_directories(uzengine_runtime PUBLIC
    src
    core/logging
    core/assertions
    core/profiling
    core/memory
    core/config
    core/math
    core/containers
    core/random
    core/rhi
    core/shader
    core/paths
    core/renderer
    core/asset
)

# Thirdparty includes
target_include_directories(uzengine_runtime PUBLIC
    thirdparty/spdlog/include
    thirdparty/tracy/public
    thirdparty/mimalloc/include
    thirdparty/directxmath/Inc
    thirdparty/glm
    thirdparty/nlohmann
    thirdparty/atomic_queue/include
    thirdparty/spirv-reflect
    thirdparty/spirv-reflect/include
    thirdparty/xxhash
)

# Windows-only includes
if(WIN32)
    target_include_directories(uzengine_runtime PUBLIC
        platform/windows
    )
    target_include_directories(uzengine_runtime PUBLIC
        thirdparty/d3d12ma/include
        thirdparty/directx_headers/include
        thirdparty/directx_headers/include/directx
        thirdparty/agility/include
        thirdparty/dxc/inc
    )
    target_include_directories(uzengine_runtime PUBLIC
        core/rhi/dx12
    )
    target_include_directories(uzengine_runtime PRIVATE
        thirdparty/d3d12ma/src
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# System Libraries (Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(uzengine_runtime PUBLIC
        d3d12.lib
        dxgi.lib
        dxguid.lib
        gdi32.lib
        user32.lib
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# GoogleTest Library
# ─────────────────────────────────────────────────────────────────────────────
add_library(gtest STATIC
    thirdparty/googletest/googletest/src/gtest-all.cc
)

target_include_directories(gtest PUBLIC
    thirdparty/googletest/googletest/include
)

target_include_directories(gtest PRIVATE
    thirdparty/googletest/googletest
)

target_compile_definitions(gtest PUBLIC
    GTEST_HAS_PTHREAD=0
)

# ─────────────────────────────────────────────────────────────────────────────
# Test Executable (auto-discovered via GLOB_RECURSE)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB_RECURSE ALL_TEST_SOURCES tests/*.cpp)

# Separate Windows-only tests
set(WINDOWS_TEST_PATTERNS
    "test_dx12_"
    "test_input"
)

set(TEST_SOURCES "")
set(WINDOWS_TEST_SOURCES "")

foreach(src IN LISTS ALL_TEST_SOURCES)
    set(_is_win_test FALSE)
    foreach(pat IN LISTS WINDOWS_TEST_PATTERNS)
        if(src MATCHES "${pat}")
            set(_is_win_test TRUE)
            break()
        endif()
    endforeach()
    if(_is_win_test)
        list(APPEND WINDOWS_TEST_SOURCES ${src})
    else()
        list(APPEND TEST_SOURCES ${src})
    endif()
endforeach()

add_executable(test_runtime
    ${TEST_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_TEST_SOURCES}>
)

target_link_libraries(test_runtime PRIVATE
    uzengine_runtime
    gtest
)

# ─────────────────────────────────────────────────────────────────────────────
# Executables (tools and demos, auto-discovered)
# ─────────────────────────────────────────────────────────────────────────────

# --- Tool executables ---
file(GLOB TOOL_SOURCES tools/*.cpp)
foreach(tool_src IN LISTS TOOL_SOURCES)
    get_filename_component(tool_name ${tool_src} NAME_WE)
    set(TOOL_EXE_SOURCES ${tool_src})
    if(WIN32)
        list(APPEND TOOL_EXE_SOURCES core/rhi/dx12/dx12_agility.cpp)
    endif()
    add_executable(${tool_name} ${TOOL_EXE_SOURCES})
    target_link_libraries(${tool_name} PRIVATE uzengine_runtime)
endforeach()

# --- Demo executables ---
file(GLOB DEMO_DIRS demos/*)
foreach(demo_dir IN LISTS DEMO_DIRS)
    if(NOT IS_DIRECTORY ${demo_dir})
        continue()
    endif()
    get_filename_component(demo_name ${demo_dir} NAME)
    file(GLOB DEMO_SOURCES ${demo_dir}/*.cpp)
    if(WIN32)
        list(APPEND DEMO_SOURCES core/rhi/dx12/dx12_agility.cpp)
    endif()
    add_executable(${demo_name}_demo ${DEMO_SOURCES})
    target_link_libraries(${demo_name}_demo PRIVATE uzengine_runtime)
endforeach()

# ─────────────────────────────────────────────────────────────────────────────
# DLL Deployment (Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    # Agility SDK DLLs → D3D12/ subdirectory
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/D3D12)

    # Collect all deploy targets
    set(DEPLOY_TARGETS test_runtime)
    foreach(tool_src IN LISTS TOOL_SOURCES)
        get_filename_component(tool_name ${tool_src} NAME_WE)
        list(APPEND DEPLOY_TARGETS ${tool_name})
    endforeach()
    foreach(demo_dir IN LISTS DEMO_DIRS)
        if(IS_DIRECTORY ${demo_dir})
            get_filename_component(demo_name ${demo_dir} NAME)
            list(APPEND DEPLOY_TARGETS ${demo_name}_demo)
        endif()
    endforeach()

    foreach(tgt IN LISTS DEPLOY_TARGETS)
        add_custom_command(TARGET ${tgt} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/agility/bin/x64/D3D12Core.dll
                $<TARGET_FILE_DIR:${tgt}>/D3D12/D3D12Core.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/agility/bin/x64/d3d12SDKLayers.dll
                $<TARGET_FILE_DIR:${tgt}>/D3D12/d3d12SDKLayers.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc/bin/x64/dxcompiler.dll
                $<TARGET_FILE_DIR:${tgt}>/dxcompiler.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc/bin/x64/dxil.dll
                $<TARGET_FILE_DIR:${tgt}>/dxil.dll
            COMMENT "Deploying runtime DLLs for ${tgt}"
        )
    endforeach()
endif()
