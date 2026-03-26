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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler Flags
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(NOMINMAX _CRT_DECLARE_NONSTDC_NAMES=1)
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
file(GLOB SRC_RHI_OPT        engine/rhi/optimization/*.cpp)
file(GLOB SRC_PATHS          engine/paths/*.cpp)
file(GLOB SRC_SHADER_COMMON  engine/shader/*.cpp)
file(GLOB SRC_RENDERER       engine/renderer/*.cpp)
file(GLOB SRC_RENDERER_CAMERA      engine/renderer/camera/*.cpp)
file(GLOB SRC_RENDERER_CORE        engine/renderer/core/*.cpp)
file(GLOB SRC_RENDERER_CORE_GBUF   engine/renderer/core/gbuffer/*.cpp)
file(GLOB SRC_RENDERER_CORE_PARAMS engine/renderer/core/params/*.cpp)
file(GLOB SRC_RENDERER_MEMORY      engine/renderer/memory/*.cpp)
file(GLOB SRC_RENDERER_MESH        engine/renderer/mesh/*.cpp)
file(GLOB SRC_RENDERER_POSTPROCESS engine/renderer/postprocess/*.cpp)
file(GLOB SRC_RENDERER_RG          engine/renderer/render_graph/*.cpp)
file(GLOB SRC_RENDERER_SHADOWS     engine/renderer/shadows/*.cpp)
file(GLOB SRC_SCRIPTING      engine/scripting/*.cpp)
file(GLOB SRC_SCRIPTING_API  engine/scripting/scene/*.cpp)
file(GLOB SRC_SCRIPTING_API_CORE engine/scripting/core/*.cpp
                                 engine/scripting/renderer/*.cpp
                                 engine/scripting/editor/*.cpp)
file(GLOB SRC_ASSET          engine/asset/*.cpp)
file(GLOB SRC_RHI_VULKAN     engine/rhi/vulkan/*.cpp)
file(GLOB SRC_MATERIAL       engine/material/*.cpp)
file(GLOB SRC_MATERIAL_COMPILER engine/material/compiler/*.cpp)
file(GLOB SRC_JOB            engine/job/*.cpp)
file(GLOB SRC_EVENT          engine/event/*.cpp)
file(GLOB SRC_CORE           engine/core/*.cpp)
file(GLOB SRC_PROPERTY       engine/property/*.cpp)
file(GLOB SRC_CONTAINERS     engine/containers/*.cpp)
file(GLOB SRC_VFX_CORE         engine/vfx/core/*.cpp)
file(GLOB SRC_VFX_COMPONENTS   engine/vfx/components/*.cpp)
file(GLOB SRC_VFX_CURVES       engine/vfx/curves/*.cpp)
file(GLOB SRC_VFX_MODULES      engine/vfx/modules/*.cpp)
file(GLOB SRC_VFX_PASSES       engine/vfx/passes/*.cpp)
file(GLOB SRC_VFX_ASSET        engine/vfx/asset/*.cpp)
file(GLOB SRC_VFX_PARAMETERS   engine/vfx/parameters/*.cpp)
file(GLOB SRC_VFX_EVENTS       engine/vfx/events/*.cpp)
file(GLOB SRC_VFX_SCRIPTING    engine/vfx/scripting/*.cpp)
file(GLOB SRC_VFX_DECALS       engine/vfx/decals/*.cpp)
file(GLOB SRC_VFX_TRAILS       engine/vfx/trails/*.cpp)
file(GLOB SRC_VFX_LOD          engine/vfx/lod/*.cpp)
file(GLOB SRC_VFX_BUDGET       engine/vfx/budget/*.cpp)
file(GLOB SRC_VFX_SYSTEM       engine/vfx/*.cpp)

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

# MSVC: exclude files with pre-existing build errors (missing headers, GCC builtins)
if(MSVC)
    list(FILTER SRC_PROFILING EXCLUDE REGEX "profile_capture\\.cpp$")
    list(FILTER SRC_RUNTIME EXCLUDE REGEX "application\\.cpp$")
    list(FILTER SRC_SCRIPTING EXCLUDE REGEX "script_system\\.cpp$")
    list(FILTER SRC_SCRIPTING_API EXCLUDE REGEX "lua_.*\\.cpp$")
    list(FILTER SRC_SCRIPTING_API_CORE EXCLUDE REGEX "lua_.*\\.cpp$")
    list(FILTER SRC_ASSET EXCLUDE REGEX "vfs\\.cpp$")
    set(SRC_RHI_VULKAN "")
    list(FILTER SRC_VFX_SCRIPTING EXCLUDE REGEX "vfx_lua_bindings\\.cpp$")
    # Exclude files needing missing thirdparty deps (freetype, entt)
    list(FILTER SRC_CORE EXCLUDE REGEX "font_manager\\.cpp$")
    list(FILTER SRC_CORE EXCLUDE REGEX "sdf_font_atlas\\.cpp$")
    # Exclude files using <thread> (MSVC 14.36 _beginthreadex bug)
    set(SRC_JOB "")
    # ECS files need entt (not available)
    set(SRC_ECS "")
    # Exclude files with cascading deps on excluded subsystems
    list(FILTER SRC_SHADER_COMMON EXCLUDE REGEX "shader_pipeline\\.cpp$")
    list(FILTER SRC_RHI_COMMON EXCLUDE REGEX "rhi_factory\\.cpp$")
    list(FILTER SRC_RENDERER_MESH EXCLUDE REGEX "gpu_mesh\\.cpp$")
    # runtime.cpp calls GetActiveRHIBackend() which is in rhi_factory.cpp (excluded)
    list(FILTER SRC_RUNTIME EXCLUDE REGEX "runtime\\.cpp$")
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
    ${SRC_RHI_OPT}
    ${SRC_RHI_VULKAN}
    ${SRC_PATHS}
    ${SRC_SHADER_COMMON}
    ${SRC_RENDERER}
    ${SRC_RENDERER_CAMERA}
    ${SRC_RENDERER_CORE}
    ${SRC_RENDERER_CORE_GBUF}
    ${SRC_RENDERER_CORE_PARAMS}
    ${SRC_RENDERER_MEMORY}
    ${SRC_RENDERER_MESH}
    ${SRC_RENDERER_POSTPROCESS}
    ${SRC_RENDERER_RG}
    ${SRC_RENDERER_SHADOWS}
    ${SRC_SCRIPTING}
    ${SRC_SCRIPTING_API}
    ${SRC_SCRIPTING_API_CORE}
    ${SRC_ASSET}
    ${SRC_MATERIAL}
    ${SRC_MATERIAL_COMPILER}
    ${SRC_JOB}
    ${SRC_EVENT}
    ${SRC_CORE}
    ${SRC_PROPERTY}
    ${SRC_CONTAINERS}
    ${SRC_VFX_CORE}
    ${SRC_VFX_COMPONENTS}
    ${SRC_VFX_CURVES}
    ${SRC_VFX_MODULES}
    ${SRC_VFX_PASSES}
    ${SRC_VFX_ASSET}
    ${SRC_VFX_PARAMETERS}
    ${SRC_VFX_EVENTS}
    ${SRC_VFX_SCRIPTING}
    ${SRC_VFX_DECALS}
    ${SRC_VFX_TRAILS}
    ${SRC_VFX_LOD}
    ${SRC_VFX_BUDGET}
    ${SRC_VFX_SYSTEM}
    $<$<PLATFORM_ID:Darwin>:${SRC_RHI_METAL}>
)

# ─────────────────────────────────────────────────────────────────────────────
# Sources — Thirdparty (always compiled)
# ─────────────────────────────────────────────────────────────────────────────
file(GLOB SPDLOG_SOURCES     thirdparty/spdlog/src/*.cpp)
if(MSVC)
    # MSVC 14.36 <thread> bug: _beginthreadex not found. Exclude spdlog async support.
    list(FILTER SPDLOG_SOURCES EXCLUDE REGEX "async\\.cpp$")
    list(FILTER SPDLOG_SOURCES EXCLUDE REGEX "color_sinks\\.cpp$")
endif()

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

# dx12_agility.cpp removed — Agility SDK no longer used

# MSVC test build: exclude RHI backends and files using <thread> (pre-existing incompatibilities)
if(MSVC)
    set(SRC_RHI_DX12 "")
    set(SRC_RHI_DX11 "")
    list(FILTER SRC_PLATFORM_WIN EXCLUDE REGEX "file_watcher\\.cpp$")
endif()

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
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:TRACY_ENABLE>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:TRACY_ON_DEMAND>
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
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    engine
    engine/renderer
    engine/renderer/core
    engine/renderer/core/gbuffer
    engine/renderer/core/lighting
    engine/renderer/core/lod
    engine/renderer/core/memory
    engine/renderer/core/params
    engine/renderer/core/pipeline
    engine/renderer/core/shader
    engine/renderer/atmosphere
    engine/renderer/camera
    engine/renderer/fog
    engine/renderer/frame
    engine/renderer/gi
    engine/renderer/ibl
    engine/renderer/lighting
    engine/renderer/material
    engine/renderer/memory
    engine/renderer/mesh
    engine/renderer/passes
    engine/renderer/passes/atmosphere
    engine/renderer/passes/debug
    engine/renderer/passes/geometry
    engine/renderer/passes/lighting
    engine/renderer/passes/postprocess
    engine/renderer/postprocess
    engine/renderer/probes
    engine/renderer/render_graph
    engine/renderer/scene
    engine/renderer/shadows
    engine/renderer/upload
    engine/scripting
    engine/scripting/scene
    engine/scripting/core
    engine/scripting/renderer
    engine/scripting/editor
    engine/ecs
    engine/asset
    engine/material
    engine/material/compiler
    engine/vfx/core
    engine/vfx/components
    engine/vfx/curves
    engine/vfx/modules
    engine/vfx/passes
    engine/vfx/asset
    engine/vfx/parameters
    engine/vfx/events
    engine/vfx/scripting
    engine/vfx/decals
    engine/vfx/trails
    engine/vfx/lod
    engine/vfx/budget
    engine/vfx
    engine/core
    engine/event
    engine/job
    engine/property
    engine/asset/formats
    engine/asset/importers
    engine/ecs/core
    engine/ecs/environment
    engine/ecs/loaders
    engine/ecs/scene
    engine/ecs/update
    engine/ecs/lighting
    engine/rhi/optimization
    editor/screenshot
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
        thirdparty/dxc/inc
    )
    target_include_directories(uzengine_runtime PUBLIC
        engine/rhi/dx12
        engine/rhi/dx11
        engine/rhi/vulkan
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
        xinput.lib
        shcore.lib
    )
    if(NOT MSVC)
        target_link_libraries(uzengine_runtime PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit/src/lua51.lib
        )
    endif()
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

# MSVC: exclude tests that depend on embedded shaders (C2026 string limit),
# <thread> (_beginthreadex), or Vulkan/scripting headers
if(MSVC)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_engine_shaders\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_shader_embed\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_shader_manager\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_file_watcher\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_lua_.*\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_script_.*\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_vulkan_.*\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_vfx_lua_.*\\.cpp$")
    # These tests require entt (thirdparty not populated)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_world\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_tick_system\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_scene_loader\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_components\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_rendering_parity\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_render_world\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_component\\.cpp$")
    # These tests require missing thirdparty (freetype, fastgltf, xatlas, etc.)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_font_manager\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_sdf_font_atlas\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_mesh_importer\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_lightmap_uv\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_png_codec\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_texture_importer\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_asset_registry\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_cook_pipeline\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_pak_format\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_uzmesh_format\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_uzshader_format\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_uztexture_format\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_process\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_vfs\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_fence\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_camera\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_camera_convenience\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_scene_lighting\\.cpp$")
    # Tests needing full renderer subsystems not compiled for MSVC
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_file_system\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_fsr2_upscaler\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_fsr_pass\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_gi_gpu\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_ibl_atmosphere\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_material_buffer\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_pipeline_library\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_security\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_shader_parameters\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_smaa_pass\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_texture_loader\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_upload_context\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_gpu_mesh\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_shader_pipeline\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_dx11_.*\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_dx12_.*\\.cpp$")
    # Tests using <thread> (excluded since job system not compiled)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_profiler\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_profile_capture\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_particle_budget\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_job_system\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_access_guard\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_counter\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_time_manager\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_shader_hot_reload\\.cpp$")
    # runtime.cpp is excluded (depends on rhi_factory) — exclude its test
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_runtime_api\\.cpp$")
    # DX11/DX12 backends not compiled for MSVC test build
    set(WINDOWS_TEST_SOURCES "")
endif()

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
    add_executable(${demo_name}_demo ${DEMO_SOURCES})
    target_link_libraries(${demo_name}_demo PRIVATE uzengine_runtime)
endforeach()

# ─────────────────────────────────────────────────────────────────────────────
# DLL Deployment (Windows)
# ─────────────────────────────────────────────────────────────────────────────
if(WIN32)
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
