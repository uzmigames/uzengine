# ─────────────────────────────────────────────────────────────────────────────
# UzEngine Runtime — CMake Build
# Mirrors the Zig build system. Supports MSVC and Clang on Windows.
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
# Sources — Cross-Platform
# ─────────────────────────────────────────────────────────────────────────────
set(RUNTIME_SOURCES
    src/runtime.cpp
    core/logging/log.cpp
    core/assertions/engine_assert.cpp
    core/profiling/profiler.cpp
    core/config/config.cpp
    core/math/engine_math.cpp
    core/rhi/rhi_config.cpp
    core/paths/paths.cpp
    core/shader/shader_vfs.cpp
    core/shader/shader_source_manager.cpp
    core/shader/shader_dependency_graph.cpp
    core/shader/shader_cache.cpp
    core/shader/shader_permutation.cpp
    core/shader/shader_reflection.cpp
    core/shader/shader_hot_reload.cpp
    core/shader/shader_pipeline.cpp
    core/shader/shader_archive.cpp
    core/renderer/upload_context.cpp
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Thirdparty (always compiled)
# ─────────────────────────────────────────────────────────────────────────────
set(SPDLOG_SOURCES
    thirdparty/spdlog/src/spdlog.cpp
    thirdparty/spdlog/src/async.cpp
    thirdparty/spdlog/src/cfg.cpp
    thirdparty/spdlog/src/color_sinks.cpp
    thirdparty/spdlog/src/file_sinks.cpp
    thirdparty/spdlog/src/stdout_sinks.cpp
    thirdparty/spdlog/src/bundled_fmtlib_format.cpp
)

set(TRACY_SOURCES
    thirdparty/tracy/public/TracyClient.cpp
)

set(SPIRV_REFLECT_SOURCES
    thirdparty/spirv-reflect/spirv_reflect.c
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Windows Only
# ─────────────────────────────────────────────────────────────────────────────
set(WINDOWS_SOURCES
    platform/windows/platform_window.cpp
    platform/windows/input.cpp
    core/rhi/dx12/dx12_device.cpp
    core/rhi/dx12/dx12_swapchain.cpp
    core/rhi/dx12/dx12_fence.cpp
    core/rhi/dx12/dx12_command_list.cpp
    core/rhi/dx12/dx12_descriptor_allocator.cpp
    core/rhi/dx12/dx12_resource_creation.cpp
    core/rhi/dx12/dx12_gpu_profiler.cpp
    core/rhi/dx12/dx12_gpu_breadcrumbs.cpp
    core/rhi/dx12/dx12_d3d12ma.cpp
    core/shader/shader_compiler.cpp
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
# Test Executable
# ─────────────────────────────────────────────────────────────────────────────
set(TEST_SOURCES
    tests/test_main.cpp
    tests/core/test_runtime_api.cpp
    tests/core/test_logging.cpp
    tests/core/test_assertions.cpp
    tests/core/test_profiler.cpp
    tests/core/test_memory.cpp
    tests/core/test_config.cpp
    tests/core/test_allocators.cpp
    tests/core/math/test_vec.cpp
    tests/core/math/test_mat4.cpp
    tests/core/math/test_quat.cpp
    tests/core/math/test_transform.cpp
    tests/core/math/test_color.cpp
    tests/core/math/test_bounds.cpp
    tests/core/containers/test_ring_buffer.cpp
    tests/core/containers/test_free_list.cpp
    tests/core/containers/test_handle_map.cpp
    tests/core/containers/test_frame_array.cpp
    tests/core/random/test_random.cpp
    tests/core/paths/test_paths.cpp
    tests/core/shader/test_shader_vfs.cpp
    tests/core/shader/test_shader_source_manager.cpp
    tests/core/shader/test_shader_dependency_graph.cpp
    tests/core/shader/test_shader_cache.cpp
    tests/core/shader/test_shader_compiler.cpp
    tests/core/shader/test_shader_permutation.cpp
    tests/core/shader/test_shader_reflection.cpp
    tests/core/shader/test_shader_hot_reload.cpp
    tests/core/shader/test_shader_pipeline.cpp
    tests/core/shader/test_shader_archive.cpp
    tests/core/rhi/test_rhi_types.cpp
    tests/core/rhi/test_rhi_config.cpp
)

set(WINDOWS_TEST_SOURCES
    tests/core/rhi/test_dx12_resources.cpp
    tests/core/rhi/test_dx12_device.cpp
    tests/core/rhi/test_dx12_render_loop.cpp
    tests/platform/test_input.cpp
)

add_executable(test_runtime
    ${TEST_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_TEST_SOURCES}>
)

target_link_libraries(test_runtime PRIVATE
    uzengine_runtime
    gtest
)

# ─────────────────────────────────────────────────────────────────────────────
# Executables
# ─────────────────────────────────────────────────────────────────────────────

# --- uzengine ---
set(UZENGINE_SOURCES tools/uzengine.cpp)
if(WIN32)
    list(APPEND UZENGINE_SOURCES core/rhi/dx12/dx12_agility.cpp)
endif()

add_executable(uzengine ${UZENGINE_SOURCES})
target_link_libraries(uzengine PRIVATE uzengine_runtime)

# --- clear_screen_demo ---
set(CLEAR_SCREEN_SOURCES
    demos/clear_screen/main.cpp
    demos/clear_screen/dx12_clear_screen.cpp
)
if(WIN32)
    list(APPEND CLEAR_SCREEN_SOURCES core/rhi/dx12/dx12_agility.cpp)
endif()

add_executable(clear_screen_demo ${CLEAR_SCREEN_SOURCES})
target_link_libraries(clear_screen_demo PRIVATE uzengine_runtime)

# --- triangle_demo ---
set(TRIANGLE_SOURCES
    demos/triangle/main.cpp
    demos/triangle/triangle_demo.cpp
)
if(WIN32)
    list(APPEND TRIANGLE_SOURCES core/rhi/dx12/dx12_agility.cpp)
endif()

add_executable(triangle_demo ${TRIANGLE_SOURCES})
target_link_libraries(triangle_demo PRIVATE uzengine_runtime)

# ─────────────────────────────────────────────────────────────────────────────
# DLL Deployment (Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
    # Agility SDK DLLs → D3D12/ subdirectory
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/D3D12)

    set(DEPLOY_TARGETS uzengine clear_screen_demo triangle_demo test_runtime)

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
