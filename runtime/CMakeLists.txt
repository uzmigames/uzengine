# ─────────────────────────────────────────────────────────────────────────────
# UzEngine Runtime — CMake Build
# Mirrors the Zig build system. Supports MSVC and Clang on Windows.
# Uses file(GLOB) for automatic source discovery — no manual file lists.
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.21)
project(uzengine_runtime LANGUAGES C CXX)

# Enable Objective-C/C++ for macOS platform layer (.mm files)
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
endif()

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
# uzembed — Embed engine shaders as C++ constexpr headers
# ─────────────────────────────────────────────────────────────────────────────
add_executable(uzembed tools/uzembed.cpp)

set(UZEMBED_INPUT_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(UZEMBED_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)

file(GLOB_RECURSE ALL_SHADERS ${UZEMBED_INPUT_DIR}/*.hlsl)

add_custom_command(
    OUTPUT  ${UZEMBED_OUTPUT_DIR}/engine_shaders.gen.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UZEMBED_OUTPUT_DIR}
    COMMAND uzembed --input-dir ${UZEMBED_INPUT_DIR} --output-dir ${UZEMBED_OUTPUT_DIR}
    DEPENDS uzembed ${ALL_SHADERS}
    COMMENT "uzembed: embedding all engine shaders"
)

add_custom_target(embed_shaders DEPENDS ${UZEMBED_OUTPUT_DIR}/engine_shaders.gen.h)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Cross-Platform (auto-discovered via GLOB)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SRC_RUNTIME        engine/app/*.cpp)
file(GLOB SRC_LOGGING        engine/logging/*.cpp)
file(GLOB SRC_ASSERTIONS     engine/assertions/*.cpp)
file(GLOB SRC_PROFILING      engine/profiling/*.cpp)
file(GLOB SRC_CONFIG         engine/config/*.cpp)
file(GLOB SRC_MATH           engine/math/*.cpp)
file(GLOB SRC_ECS            engine/ecs/*.cpp)
file(GLOB SRC_RHI_COMMON     engine/rhi/*.cpp)
file(GLOB SRC_PATHS          engine/paths/*.cpp)
file(GLOB SRC_SHADER_COMMON  engine/shader/*.cpp)
file(GLOB SRC_RENDERER       engine/renderer/*.cpp)
file(GLOB SRC_SCRIPTING      engine/scripting/*.cpp)
file(GLOB SRC_SCRIPTING_API  engine/scripting/scene/*.cpp)
file(GLOB SRC_SCRIPTING_API_CORE engine/scripting/core/*.cpp
                                 engine/scripting/renderer/*.cpp)
file(GLOB SRC_ASSET          engine/asset/*.cpp)
file(GLOB SRC_RHI_VULKAN     engine/rhi/vulkan/*.cpp)
file(GLOB SRC_MATERIAL       engine/material/*.cpp)
file(GLOB SRC_MATERIAL_COMPILER engine/material/compiler/*.cpp)

# Metal backend (macOS/iOS only)
if(APPLE)
    file(GLOB SRC_RHI_METAL_CPP  engine/rhi/metal/*.cpp)
    file(GLOB SRC_RHI_METAL_MM   engine/rhi/metal/*.mm)
    set(SRC_RHI_METAL ${SRC_RHI_METAL_CPP} ${SRC_RHI_METAL_MM})
endif()

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
    ${SRC_ECS}
    ${SRC_RHI_COMMON}
    ${SRC_RHI_VULKAN}
    ${SRC_PATHS}
    ${SRC_SHADER_COMMON}
    ${SRC_RENDERER}
    ${SRC_SCRIPTING}
    ${SRC_SCRIPTING_API}
    ${SRC_SCRIPTING_API_CORE}
    ${SRC_ASSET}
    ${SRC_MATERIAL}
    ${SRC_MATERIAL_COMPILER}
    $<$<PLATFORM_ID:Darwin>:${SRC_RHI_METAL}>
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

set(VOLK_SOURCES
    thirdparty/volk/volk.c
)

# Image libraries
file(GLOB ASTCENC_SOURCES thirdparty/astcenc/Source/astcenc_*.cpp)

set(BASIS_UNIVERSAL_SOURCES
    thirdparty/basis_universal/transcoder/basisu_transcoder.cpp
)

set(MINIZ_SOURCES
    thirdparty/tinyexr/miniz.c
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Windows Only (auto-discovered via GLOB)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SRC_PLATFORM_WIN   platform/windows/*.cpp)
file(GLOB SRC_RHI_DX12       engine/rhi/dx12/*.cpp)
file(GLOB SRC_RHI_DX11       engine/rhi/dx11/*.cpp)

# Exclude dx12_agility.cpp — only linked into executables, not the library
list(FILTER SRC_RHI_DX12 EXCLUDE REGEX "dx12_agility\\.cpp$")

set(WINDOWS_SOURCES
    ${SRC_PLATFORM_WIN}
    ${SRC_RHI_DX12}
    ${SRC_RHI_DX11}
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — macOS Only (auto-discovered via GLOB)
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
    file(GLOB SRC_PLATFORM_MACOS_CPP  platform/macos/*.cpp)
    file(GLOB SRC_PLATFORM_MACOS_MM   platform/macos/*.mm)
    set(MACOS_PLATFORM_SOURCES ${SRC_PLATFORM_MACOS_CPP} ${SRC_PLATFORM_MACOS_MM})

    # Set .mm files as Objective-C++
    foreach(mm_src IN LISTS SRC_PLATFORM_MACOS_MM)
        set_source_files_properties(${mm_src}
            PROPERTIES LANGUAGE OBJCXX
        )
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Static Library
# ─────────────────────────────────────────────────────────────────────────────
add_library(uzengine_runtime STATIC
    ${RUNTIME_SOURCES}
    ${SPDLOG_SOURCES}
    ${TRACY_SOURCES}
    ${SPIRV_REFLECT_SOURCES}
    ${VOLK_SOURCES}
    ${ASTCENC_SOURCES}
    ${BASIS_UNIVERSAL_SOURCES}
    ${MINIZ_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_SOURCES}>
    $<$<PLATFORM_ID:Darwin>:${MACOS_PLATFORM_SOURCES}>
)

add_dependencies(uzengine_runtime embed_shaders)

target_compile_definitions(uzengine_runtime PUBLIC
    SPDLOG_COMPILED_LIB
    UZ_PROFILING_ENABLED
    TRACY_ENABLE
    TRACY_ON_DEMAND
)

# Per-file compiler flags for D3D12MA (suppresses upstream warnings)
if(WIN32)
    if(MSVC)
        set_source_files_properties(engine/rhi/dx12/dx12_d3d12ma.cpp
            PROPERTIES COMPILE_OPTIONS "/wd4267;/wd4244"
        )
    else()
        set_source_files_properties(engine/rhi/dx12/dx12_d3d12ma.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-switch;-Wno-unused-private-field"
        )
    endif()
endif()

# spirv_reflect.c is C, ensure it compiles as C11
set_source_files_properties(thirdparty/spirv-reflect/spirv_reflect.c
    PROPERTIES LANGUAGE C
)

# miniz.c is C
set_source_files_properties(thirdparty/tinyexr/miniz.c
    PROPERTIES LANGUAGE C
)

# volk.c is C, needs VK_NO_PROTOTYPES
set_source_files_properties(thirdparty/volk/volk.c
    PROPERTIES LANGUAGE C COMPILE_DEFINITIONS "VK_NO_PROTOTYPES"
)

# Metal backend — set .mm files as Objective-C++
if(APPLE)
    foreach(mm_src IN LISTS SRC_RHI_METAL_MM)
        set_source_files_properties(${mm_src}
            PROPERTIES LANGUAGE OBJCXX
        )
    endforeach()
endif()

# basis_universal needs BASISD_SUPPORT_KTX2_ZSTD=0 (no zstd dependency)
set_source_files_properties(thirdparty/basis_universal/transcoder/basisu_transcoder.cpp
    PROPERTIES COMPILE_DEFINITIONS "BASISD_SUPPORT_KTX2_ZSTD=0"
)

# Suppress warnings on thirdparty image library sources
if(NOT MSVC)
    foreach(src IN LISTS ASTCENC_SOURCES)
        set_source_files_properties(${src}
            PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-missing-field-initializers"
        )
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Include Directories
# ─────────────────────────────────────────────────────────────────────────────

# Engine includes
target_include_directories(uzengine_runtime PUBLIC
    engine/app
    engine/logging
    engine/assertions
    engine/profiling
    engine/memory
    engine/config
    engine/math
    engine/containers
    engine/random
    engine/rhi
    engine/shader
    engine/paths
    engine/renderer
    engine/scripting
    engine/scripting/scene
    engine/scripting/core
    engine/scripting/renderer
    engine/ecs
    engine/asset
    engine/material
    engine/material/compiler
    ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders
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
    thirdparty/stb
    thirdparty/tinyexr
    thirdparty/libjpeg-turbo
    thirdparty/basis_universal/transcoder
    thirdparty/astcenc/Source
    thirdparty/vulkan_headers/include
    thirdparty/volk
    thirdparty/vma/include
    thirdparty/luajit/src
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
        engine/rhi/dx12
        engine/rhi/dx11
    )
    target_include_directories(uzengine_runtime PRIVATE
        thirdparty/d3d12ma/src
    )
endif()

# macOS/iOS — Metal backend and platform includes
if(APPLE)
    target_include_directories(uzengine_runtime PUBLIC
        platform/macos
        engine/rhi/metal
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# System Libraries (Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    target_link_libraries(uzengine_runtime PUBLIC
        d3d12.lib
        d3d11.lib
        dxgi.lib
        dxguid.lib
        gdi32.lib
        user32.lib
        ws2_32.lib
        dbghelp.lib
        advapi32.lib
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit/src/lua51.lib
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Apple Frameworks (macOS/iOS — Metal backend)
# ─────────────────────────────────────────────────────────────────────────────
if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit)

    target_link_libraries(uzengine_runtime PUBLIC
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
    )
    if(APPKIT_FRAMEWORK)
        target_link_libraries(uzengine_runtime PUBLIC ${APPKIT_FRAMEWORK})
    endif()
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
    "test_dx11_"
    "platform/test_input\\.cpp"
)

set(APPLE_TEST_PATTERNS
    "test_metal_"
)

set(TEST_SOURCES "")
set(WINDOWS_TEST_SOURCES "")
set(APPLE_TEST_SOURCES "")

foreach(src IN LISTS ALL_TEST_SOURCES)
    set(_is_win_test FALSE)
    set(_is_apple_test FALSE)
    foreach(pat IN LISTS WINDOWS_TEST_PATTERNS)
        if(src MATCHES "${pat}")
            set(_is_win_test TRUE)
            break()
        endif()
    endforeach()
    if(NOT _is_win_test)
        foreach(pat IN LISTS APPLE_TEST_PATTERNS)
            if(src MATCHES "${pat}")
                set(_is_apple_test TRUE)
                break()
            endif()
        endforeach()
    endif()
    if(_is_win_test)
        list(APPEND WINDOWS_TEST_SOURCES ${src})
    elseif(_is_apple_test)
        list(APPEND APPLE_TEST_SOURCES ${src})
    else()
        list(APPEND TEST_SOURCES ${src})
    endif()
endforeach()

add_executable(test_runtime
    ${TEST_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_TEST_SOURCES}>
    $<$<PLATFORM_ID:Darwin>:${APPLE_TEST_SOURCES}>
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
# uzembed is built separately above — exclude from generic tool loop
list(FILTER TOOL_SOURCES EXCLUDE REGEX "uzembed\\.cpp$")
foreach(tool_src IN LISTS TOOL_SOURCES)
    get_filename_component(tool_name ${tool_src} NAME_WE)
    set(TOOL_EXE_SOURCES ${tool_src})
    if(WIN32)
        list(APPEND TOOL_EXE_SOURCES engine/rhi/dx12/dx12_agility.cpp)
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
        list(APPEND DEMO_SOURCES engine/rhi/dx12/dx12_agility.cpp)
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
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fxc/bin/x64/d3dcompiler_47.dll
                $<TARGET_FILE_DIR:${tgt}>/d3dcompiler_47.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit/src/lua51.dll
                $<TARGET_FILE_DIR:${tgt}>/lua51.dll
            COMMENT "Deploying runtime DLLs for ${tgt}"
        )
    endforeach()
endif()
