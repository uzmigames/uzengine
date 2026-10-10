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

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(UZ_NATIVE_DEPS_ROOT "" CACHE PATH "Locked Windows profile produced by scripts/setup-native.ps1")
set(UZ_FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg")
set(UZ_LUAJIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit/src")
set(UZ_DXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/dxc")
set(UZ_FXC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fxc")
set(UZ_FASTGLTF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fastgltf")
set(UZ_MESHOPTIMIZER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/meshoptimizer")
set(UZ_ENTT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/entt")
set(UZ_SIMDJSON_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fastgltf/deps/simdjson")
if(UZ_NATIVE_DEPS_ROOT)
    if(NOT WIN32 OR NOT EXISTS "${UZ_NATIVE_DEPS_ROOT}/provision.json")
        message(FATAL_ERROR "UZ_NATIVE_DEPS_ROOT requires a provisioned Windows profile")
    endif()
    set(UZ_FFMPEG_ROOT "${UZ_NATIVE_DEPS_ROOT}/ffmpeg")
    set(UZ_LUAJIT_ROOT "${UZ_NATIVE_DEPS_ROOT}/luajit/src")
    set(UZ_DXC_ROOT "${UZ_NATIVE_DEPS_ROOT}/dxc")
    set(UZ_FXC_ROOT "${UZ_NATIVE_DEPS_ROOT}/fxc")
    set(UZ_FASTGLTF_ROOT "${UZ_NATIVE_DEPS_ROOT}/fastgltf")
    set(UZ_MESHOPTIMIZER_ROOT "${UZ_NATIVE_DEPS_ROOT}/meshoptimizer")
    set(UZ_ENTT_ROOT "${UZ_NATIVE_DEPS_ROOT}/entt")
    set(UZ_SIMDJSON_ROOT "${UZ_NATIVE_DEPS_ROOT}/simdjson/singleheader")
endif()

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
                                 engine/scripting/renderer/*.cpp)
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
file(GLOB SRC_VFX_EDITOR       engine/vfx/editor/*.cpp)
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
    ${SRC_VFX_EDITOR}
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
# Thirdparty — fastgltf (+ bundled simdjson) and meshoptimizer sources,
# mirroring build.zig (C++17 upstream code).
set(JPEGTURBO_SOURCES
    thirdparty/libjpeg-turbo/src/jaricom.c
    thirdparty/libjpeg-turbo/src/jcomapi.c
    thirdparty/libjpeg-turbo/src/jdapimin.c
    thirdparty/libjpeg-turbo/src/jdapistd.c
    thirdparty/libjpeg-turbo/src/jdarith.c
    thirdparty/libjpeg-turbo/src/jdatasrc.c
    thirdparty/libjpeg-turbo/src/jdcoefct.c
    thirdparty/libjpeg-turbo/src/jdcolor.c
    thirdparty/libjpeg-turbo/src/jddctmgr.c
    thirdparty/libjpeg-turbo/src/jddiffct.c
    thirdparty/libjpeg-turbo/src/jdhuff.c
    thirdparty/libjpeg-turbo/src/jdicc.c
    thirdparty/libjpeg-turbo/src/jdinput.c
    thirdparty/libjpeg-turbo/src/jdlhuff.c
    thirdparty/libjpeg-turbo/src/jdlossls.c
    thirdparty/libjpeg-turbo/src/jdmainct.c
    thirdparty/libjpeg-turbo/src/jdmarker.c
    thirdparty/libjpeg-turbo/src/jdmaster.c
    thirdparty/libjpeg-turbo/src/jdmerge.c
    thirdparty/libjpeg-turbo/src/jdphuff.c
    thirdparty/libjpeg-turbo/src/jdpostct.c
    thirdparty/libjpeg-turbo/src/jdsample.c
    thirdparty/libjpeg-turbo/src/jdtrans.c
    thirdparty/libjpeg-turbo/src/jerror.c
    thirdparty/libjpeg-turbo/src/jidctflt.c
    thirdparty/libjpeg-turbo/src/jidctfst.c
    thirdparty/libjpeg-turbo/src/jidctint.c
    thirdparty/libjpeg-turbo/src/jidctred.c
    thirdparty/libjpeg-turbo/src/jmemmgr.c
    thirdparty/libjpeg-turbo/src/jmemnobs.c
    thirdparty/libjpeg-turbo/src/jpeg_nbits.c
    thirdparty/libjpeg-turbo/src/jquant1.c
    thirdparty/libjpeg-turbo/src/jquant2.c
    thirdparty/libjpeg-turbo/src/jutils.c
    thirdparty/libjpeg-turbo/src/jcapimin.c
    thirdparty/libjpeg-turbo/src/jcapistd.c
    thirdparty/libjpeg-turbo/src/jcarith.c
    thirdparty/libjpeg-turbo/src/jccoefct.c
    thirdparty/libjpeg-turbo/src/jccolor.c
    thirdparty/libjpeg-turbo/src/jcdctmgr.c
    thirdparty/libjpeg-turbo/src/jcdiffct.c
    thirdparty/libjpeg-turbo/src/jchuff.c
    thirdparty/libjpeg-turbo/src/jcicc.c
    thirdparty/libjpeg-turbo/src/jcinit.c
    thirdparty/libjpeg-turbo/src/jclhuff.c
    thirdparty/libjpeg-turbo/src/jclossls.c
    thirdparty/libjpeg-turbo/src/jcmainct.c
    thirdparty/libjpeg-turbo/src/jcmarker.c
    thirdparty/libjpeg-turbo/src/jcmaster.c
    thirdparty/libjpeg-turbo/src/jcparam.c
    thirdparty/libjpeg-turbo/src/jcphuff.c
    thirdparty/libjpeg-turbo/src/jcprepct.c
    thirdparty/libjpeg-turbo/src/jcsample.c
    thirdparty/libjpeg-turbo/src/jctrans.c
    thirdparty/libjpeg-turbo/src/jdatadst.c
    thirdparty/libjpeg-turbo/src/jfdctflt.c
    thirdparty/libjpeg-turbo/src/jfdctfst.c
    thirdparty/libjpeg-turbo/src/jfdctint.c
    thirdparty/libjpeg-turbo/src/transupp.c
    thirdparty/libjpeg-turbo/src/wrapper/jdapistd-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdcoefct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdcolor-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jddctmgr-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jddiffct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdlossls-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdmainct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdmerge-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdpostct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdsample-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jidctflt-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jidctfst-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jidctint-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jidctred-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jquant1-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jquant2-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jutils-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcapistd-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jccoefct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jccolor-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcdctmgr-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcdiffct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jclossls-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcmainct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcprepct-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jcsample-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jfdctfst-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jfdctint-12.c
    thirdparty/libjpeg-turbo/src/wrapper/jdapistd-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jdcolor-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jddiffct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jdlossls-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jdmainct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jdpostct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jdsample-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jutils-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jcapistd-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jccolor-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jcdiffct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jclossls-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jcmainct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jcprepct-16.c
    thirdparty/libjpeg-turbo/src/wrapper/jcsample-16.c
    thirdparty/mikktspace/mikktspace.c
)
set(UFBX_SOURCES thirdparty/ufbx/ufbx.c)
# spng PNG decode (per build.zig: SPNG_USE_MINIZ=0 → links system zlib)
set(SPNG_SOURCES thirdparty/spng/spng/spng.c)
set_source_files_properties(${SPNG_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "SPNG_USE_MINIZ=0;SPNG_STATIC"
)
# xatlas lightmap UV generation (per build.zig)
set(XATLAS_SOURCES thirdparty/xatlas/source/xatlas/xatlas.cpp)
# libpng (PNG IO for msdfgen ext and texture tooling, per build.zig)
set(LIBPNG_SOURCES
    thirdparty/libpng/png.c thirdparty/libpng/pngerror.c thirdparty/libpng/pngget.c
    thirdparty/libpng/pngmem.c thirdparty/libpng/pngpread.c thirdparty/libpng/pngread.c
    thirdparty/libpng/pngrio.c thirdparty/libpng/pngrtran.c thirdparty/libpng/pngrutil.c
    thirdparty/libpng/pngset.c thirdparty/libpng/pngtrans.c thirdparty/libpng/pngwio.c
    thirdparty/libpng/pngwrite.c thirdparty/libpng/pngwtran.c thirdparty/libpng/pngwutil.c
)
set_source_files_properties(${LIBPNG_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "PNG_ARM_NEON_OPT=0;PNG_STATIC"
)
# msdfgen core + ext (multi-channel SDF font atlas, per build.zig)
set(MSDFGEN_SOURCES
    thirdparty/msdfgen/core/Contour.cpp thirdparty/msdfgen/core/DistanceMapping.cpp
    thirdparty/msdfgen/core/EdgeHolder.cpp thirdparty/msdfgen/core/MSDFErrorCorrection.cpp
    thirdparty/msdfgen/core/Projection.cpp thirdparty/msdfgen/core/Scanline.cpp
    thirdparty/msdfgen/core/Shape.cpp thirdparty/msdfgen/core/contour-combiners.cpp
    thirdparty/msdfgen/core/convergent-curve-ordering.cpp thirdparty/msdfgen/core/edge-coloring.cpp
    thirdparty/msdfgen/core/edge-segments.cpp thirdparty/msdfgen/core/edge-selectors.cpp
    thirdparty/msdfgen/core/equation-solver.cpp thirdparty/msdfgen/core/export-svg.cpp
    thirdparty/msdfgen/core/msdf-error-correction.cpp thirdparty/msdfgen/core/msdfgen.cpp
    thirdparty/msdfgen/core/rasterization.cpp thirdparty/msdfgen/core/render-sdf.cpp
    thirdparty/msdfgen/core/save-bmp.cpp thirdparty/msdfgen/core/save-fl32.cpp
    thirdparty/msdfgen/core/save-rgba.cpp thirdparty/msdfgen/core/save-tiff.cpp
    thirdparty/msdfgen/core/sdf-error-estimation.cpp thirdparty/msdfgen/core/shape-description.cpp
    thirdparty/msdfgen/ext/import-font.cpp thirdparty/msdfgen/ext/import-svg.cpp
    thirdparty/msdfgen/ext/resolve-shape-geometry.cpp thirdparty/msdfgen/ext/save-png.cpp
)
# MSDFGEN_PUBLIC= (empty) bypasses the CMake-generated msdfgen-config.h (build.zig trick)
set_source_files_properties(${MSDFGEN_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "MSDFGEN_PUBLIC="
)
# etcpak BC1/BC3/BC4/BC5/BC7 texture compression (per build.zig)
set(ETCPAK_SOURCES
    thirdparty/etcpak/ProcessDxtc.cpp
    thirdparty/etcpak/Tables.cpp
    thirdparty/etcpak/Dither.cpp
    thirdparty/etcpak/bc7enc.cpp
)
set(FASTGLTF_SOURCES
    ${UZ_SIMDJSON_ROOT}/simdjson.cpp
    ${UZ_FASTGLTF_ROOT}/src/fastgltf.cpp
    ${UZ_FASTGLTF_ROOT}/src/base64.cpp
    ${UZ_FASTGLTF_ROOT}/src/io.cpp
)
set(MESHOPT_SOURCES
    ${UZ_MESHOPTIMIZER_ROOT}/src/allocator.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/clusterizer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/indexcodec.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/indexgenerator.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/overdrawanalyzer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/overdrawoptimizer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/quantization.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/simplifier.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/spatialorder.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/stripifier.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vcacheanalyzer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vcacheoptimizer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vertexcodec.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vertexfilter.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vfetchanalyzer.cpp
    ${UZ_MESHOPTIMIZER_ROOT}/src/vfetchoptimizer.cpp
)


# Complete engine source dirs, mirroring build.zig addCppDir(runtime_mod, ...)
set(RUNTIME_EXTRA_DIRS
    engine/asset/formats
    engine/asset/mesh
    engine/asset/importers
    engine/renderer/core/shader
    engine/renderer/core/pipeline
    engine/renderer/material
    engine/renderer/upload
    engine/renderer/passes/geometry
    engine/renderer/passes/lighting
    engine/renderer/passes/atmosphere
    engine/renderer/passes/postprocess
    engine/renderer/passes/debug
    engine/renderer/passes/vfx
    engine/renderer/passes/ibl
    engine/renderer/passes/raytracing
    engine/renderer/passes/gi
    engine/renderer/passes/pathtracing
    engine/renderer/lighting
    engine/renderer/atmosphere
    engine/renderer/fog
    engine/renderer/ibl
    engine/renderer/gi
    engine/renderer/probes
    engine/renderer/raytracing
    engine/renderer/scene
    engine/renderer/frame
    engine/ecs/core
    engine/ecs/scene
    engine/ecs/lighting
    engine/ecs/mesh
    engine/ecs/update
    engine/ecs/loaders
    engine/scripting/editor
    engine/scripting/animation
    engine/ipc
    engine/vfx/material
    engine/animation
    engine/debug
)
set(RUNTIME_EXTRA_SOURCES)
foreach(d IN LISTS RUNTIME_EXTRA_DIRS)
    file(GLOB _srcs ${d}/*.cpp)
    list(APPEND RUNTIME_EXTRA_SOURCES ${_srcs})
endforeach()
# This CMake consumer does not configure the optional FidelityFX wrapper or
# UZ_BRIXELIZER_ENABLED. Match gi_pass's disabled-backend configuration; the
# SDK-backed loader is compiled by build.zig when that integration is enabled.
list(FILTER RUNTIME_EXTRA_SOURCES EXCLUDE REGEX "brixelizer_loader\\.cpp$")
# Particle simulation uses the Windows-only ShaderCompiler.
if(NOT WIN32)
    list(FILTER RUNTIME_EXTRA_SOURCES EXCLUDE REGEX "particle_simulation_pass\\.cpp$")
endif()

# LZ4 (pak compression) and FreeType (font rasterization for msdfgen/font_manager),
# mirroring build.zig.
set(LZ4_SOURCES
    thirdparty/lz4/lib/lz4.c
    thirdparty/lz4/lib/lz4frame.c
    thirdparty/lz4/lib/lz4hc.c
    thirdparty/lz4/lib/xxhash.c
)
set(FREETYPE_SOURCES
    thirdparty/freetype/src/base/ftsystem.c
    thirdparty/freetype/src/base/ftinit.c
    thirdparty/freetype/src/base/ftdebug.c
    thirdparty/freetype/src/base/ftbase.c
    thirdparty/freetype/src/base/ftbbox.c
    thirdparty/freetype/src/base/ftbitmap.c
    thirdparty/freetype/src/base/ftglyph.c
    thirdparty/freetype/src/base/ftmm.c
    thirdparty/freetype/src/truetype/truetype.c
    thirdparty/freetype/src/sfnt/sfnt.c
    thirdparty/freetype/src/smooth/smooth.c
    thirdparty/freetype/src/autofit/autofit.c
    thirdparty/freetype/src/psnames/psnames.c
    thirdparty/freetype/src/gzip/ftgzip.c
)
set_source_files_properties(${FREETYPE_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "FT2_BUILD_LIBRARY;FT_CONFIG_OPTION_SYSTEM_ZLIB=0"
)

add_library(uzengine_runtime STATIC
    ${RUNTIME_SOURCES}
    ${RUNTIME_EXTRA_SOURCES}
    ${SPDLOG_SOURCES}
    ${TRACY_SOURCES}
    ${SPIRV_REFLECT_SOURCES}
    ${VOLK_SOURCES}
    ${ASTCENC_SOURCES}
    ${BASIS_UNIVERSAL_SOURCES}
    ${FASTGLTF_SOURCES}
    ${UFBX_SOURCES}
    ${LZ4_SOURCES}
    ${ETCPAK_SOURCES}
    ${XATLAS_SOURCES}
    ${JPEGTURBO_SOURCES}
    ${SPNG_SOURCES}
    ${LIBPNG_SOURCES}
    ${MSDFGEN_SOURCES}
    ${FREETYPE_SOURCES}
    ${MESHOPT_SOURCES}
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

# Metal backend — all files compile as Objective-C++: even the .cpp files
# include <Metal/Metal.h> (ObjC syntax) per the Zig build.
if(APPLE)
    foreach(metal_src IN LISTS SRC_RHI_METAL)
        set_source_files_properties(${metal_src}
            PROPERTIES LANGUAGE OBJCXX
        )
    endforeach()
    # Tracy's Metal wrapper requires ARC (matches the Zig build).
    set_source_files_properties(engine/rhi/metal/metal_tracy_gpu.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
    # drag_drop.mm uses __weak — written for ARC.
    set_source_files_properties(platform/macos/drag_drop.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
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
    engine
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
    engine/rhi/optimization
    engine/rhi/vulkan
    engine/shader
    engine/paths
    engine/core
    engine/renderer
    engine/renderer/camera
    engine/renderer/core
    engine/renderer/core/params
    engine/renderer/core/gbuffer
    engine/renderer/core/shader
    engine/renderer/core/pipeline
    engine/renderer/core/memory
    engine/renderer/core/lod
    engine/renderer/core/lighting
    engine/renderer/material
    engine/material
    engine/material/compiler
    engine/renderer/memory
    engine/renderer/mesh
    engine/renderer/upload
    engine/renderer/render_graph
    engine/renderer/shadows
    engine/renderer/ibl
    engine/renderer/gi
    engine/renderer/probes
    engine/renderer/lighting
    engine/renderer/atmosphere
    engine/renderer/fog
    engine/renderer/scene
    engine/renderer/frame
    engine/renderer/passes
    engine/renderer/passes/geometry
    engine/renderer/passes/lighting
    engine/renderer/passes/atmosphere
    engine/renderer/passes/postprocess
    engine/renderer/postprocess
    engine/renderer/passes/debug
    engine/renderer/passes/ibl
    engine/renderer/passes/gi
    engine/renderer/passes/pathtracing
    engine/renderer/raytracing
    engine/asset
    engine/asset/formats
    engine/asset/mesh
    engine/asset/importers
    engine/job
    engine/ecs
    engine/ecs/core
    engine/ecs/scene
    engine/ecs/lighting
    engine/ecs/mesh
    engine/ecs/environment
    engine/ecs/metadata
    engine/ecs/update
    engine/ecs/loaders
    engine/scripting
    engine/scripting/scene
    engine/scripting/core
    engine/scripting/renderer
    engine/scripting/editor
    engine/scripting/animation
    engine/ipc
    engine/vfx
    engine/vfx/core
    engine/vfx/components
    engine/vfx/curves
    engine/vfx/modules
    engine/vfx/asset
    engine/vfx/parameters
    engine/vfx/events
    engine/renderer/passes/vfx
    engine/vfx/decals
    engine/vfx/trails
    engine/vfx/lod
    engine/vfx/budget
    engine/vfx/editor
    engine/vfx/material
    engine/animation
    engine/event
    engine/property
    engine/debug
    editor/grid
    editor/outline
    editor/gizmo
    editor/font
    editor/stats
    editor/screenshot
    editor/input
    editor/selection
    editor/toolbar
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
    thirdparty/lz4/lib
    thirdparty/stb
    thirdparty/spng/spng
    thirdparty/tinyexr
    thirdparty/libjpeg-turbo
    thirdparty/libjpeg-turbo/src
    thirdparty/basis_universal/transcoder
    thirdparty/astcenc/Source
    thirdparty/vulkan_headers/include
    thirdparty/volk
    thirdparty/vma/include
    ${UZ_FASTGLTF_ROOT}/include
    ${UZ_SIMDJSON_ROOT}
    ${UZ_MESHOPTIMIZER_ROOT}/src
    thirdparty/mikktspace
    ${UZ_ENTT_ROOT}/single_include
    ${UZ_LUAJIT_ROOT}
    thirdparty/ufbx
    thirdparty/etcpak
    thirdparty/xatlas/source/xatlas
    thirdparty/libpng
    thirdparty/msdfgen
    thirdparty/msdfgen/core
    thirdparty/msdfgen/ext
    thirdparty/freetype/include
    thirdparty/smaa
    ${UZ_FFMPEG_ROOT}/include
    thirdparty/slang/include
)
# Windows-only includes
if(WIN32)
    # Match build.zig's static zlib sources without depending on vcpkg's
    # revision-dependent extraction suffix. An ambiguous checkout must choose
    # its source explicitly instead of silently taking a different version.
    set(UZ_ZLIB_SOURCE_DIR "" CACHE PATH "Extracted zlib source directory for the Windows build")
    if(UZ_NATIVE_DEPS_ROOT AND NOT UZ_ZLIB_SOURCE_DIR)
        set(UZ_ZLIB_SOURCE_DIR "${UZ_NATIVE_DEPS_ROOT}/zlib")
    endif()
    if(NOT UZ_ZLIB_SOURCE_DIR)
        file(GLOB _zlib_candidates LIST_DIRECTORIES true
            "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/vcpkg/buildtrees/zlib/src/*.clean")
        set(_zlib_valid_sources)
        foreach(_candidate IN LISTS _zlib_candidates)
            if(EXISTS "${_candidate}/zlib.h" AND EXISTS "${_candidate}/zconf.h"
               AND EXISTS "${_candidate}/adler32.c")
                list(APPEND _zlib_valid_sources "${_candidate}")
            endif()
        endforeach()
        list(LENGTH _zlib_valid_sources _zlib_count)
        if(NOT _zlib_count EQUAL 1)
            message(FATAL_ERROR "Expected one extracted zlib source; run setup or set UZ_ZLIB_SOURCE_DIR explicitly")
        endif()
        list(GET _zlib_valid_sources 0 UZ_ZLIB_SOURCE_DIR)
    endif()
    foreach(_required IN ITEMS zlib.h zconf.h adler32.c)
        if(NOT EXISTS "${UZ_ZLIB_SOURCE_DIR}/${_required}")
            message(FATAL_ERROR "UZ_ZLIB_SOURCE_DIR is missing ${_required}")
        endif()
    endforeach()
    foreach(_source IN ITEMS
            adler32.c compress.c crc32.c deflate.c
            gzclose.c gzlib.c gzread.c gzwrite.c
            infback.c inffast.c inflate.c inftrees.c
            trees.c uncompr.c zutil.c)
        target_sources(uzengine_runtime PRIVATE "${UZ_ZLIB_SOURCE_DIR}/${_source}")
    endforeach()
    target_include_directories(uzengine_runtime PRIVATE "${UZ_ZLIB_SOURCE_DIR}")
    target_compile_definitions(uzengine_runtime PRIVATE ZLIB_STATIC)

    target_include_directories(uzengine_runtime PUBLIC
        platform/windows
    )
    target_include_directories(uzengine_runtime PUBLIC
        thirdparty/d3d12ma/include
        thirdparty/directx_headers/include
        thirdparty/directx_headers/include/directx
        ${UZ_DXC_ROOT}/inc
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
        xinput.lib
        shcore.lib
    )
    if(NOT MSVC)
        target_link_libraries(uzengine_runtime PUBLIC
            ${UZ_LUAJIT_ROOT}/lua51.lib
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
    # system_info.mm reads battery/hardware info through IOKit
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    # libpng links the system zlib
    find_library(ZLIB_LIBRARY z REQUIRED)
    # LuaJIT is built via its own Makefile (scripts or `make -C runtime/thirdparty/luajit/src`)
    set(LUAJIT_LIB ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/luajit/src/libluajit.a)
    if(NOT EXISTS ${LUAJIT_LIB})
        message(FATAL_ERROR "libluajit.a not found — run: make -C runtime/thirdparty/luajit/src MACOSX_DEPLOYMENT_TARGET=15.0")
    endif()

    target_link_libraries(uzengine_runtime PUBLIC
        ${METAL_FRAMEWORK}
        ${QUARTZCORE_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK}
        ${IOKIT_FRAMEWORK}
        ${ZLIB_LIBRARY}
        ${LUAJIT_LIB}
    )
    if(APPKIT_FRAMEWORK)
        target_link_libraries(uzengine_runtime PUBLIC ${APPKIT_FRAMEWORK})
    endif()
endif()

# Slang shader compiler runtime (dlopen'd by slang_compiler.cpp) — deploy the
# vendored dylibs next to executables under the wrapper's relative layout and
# add an rpath so its @rpath dependencies resolve.
if(APPLE)
    set(SLANG_DEPLOY_DIR ${CMAKE_CURRENT_BINARY_DIR}/thirdparty/slang/bin)
    file(GLOB SLANG_DYLIBS ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/slang/bin/*.dylib)
    file(MAKE_DIRECTORY ${SLANG_DEPLOY_DIR})
    add_custom_target(deploy_slang ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SLANG_DYLIBS} ${SLANG_DEPLOY_DIR}/
        DEPENDS ${SLANG_DYLIBS}
    )
    # Binaries link @rpath so libslang's own @rpath deps resolve from its dir
    add_link_options(-Wl,-rpath,@executable_path/thirdparty/slang/bin)
    add_dependencies(uzengine_runtime deploy_slang)
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


# engine/ecs/{environment,metadata} compile into executables directly (build.zig design)
file(GLOB SRC_ECS_ENV      engine/ecs/environment/*.cpp)
file(GLOB SRC_ECS_METADATA engine/ecs/metadata/*.cpp)
set(ECS_ENV_SOURCES ${SRC_ECS_ENV} ${SRC_ECS_METADATA})


# Editor static library (mirrors build.zig editor_mod)
set(EDITOR_DIRS
    editor/grid editor/outline editor/gizmo editor/font editor/stats
    editor/screenshot editor/input editor/selection editor/toolbar
)
set(EDITOR_SOURCES)
foreach(d IN LISTS EDITOR_DIRS)
    file(GLOB _esrcs ${d}/*.cpp)
    list(APPEND EDITOR_SOURCES ${_esrcs})
endforeach()
add_library(uzengine_editor STATIC ${EDITOR_SOURCES})
target_link_libraries(uzengine_editor PUBLIC uzengine_runtime)

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

# H.264 encoder tests need FFmpeg (Windows-only vendored libs)
if(NOT WIN32)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_h264_encoder\\.cpp$")
endif()

# debug_skin_reference is a standalone exe with its own main() (zig: dbg_exe)
list(FILTER TEST_SOURCES EXCLUDE REGEX "debug_skin_reference")

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
    ${ECS_ENV_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_TEST_SOURCES}>
    $<$<PLATFORM_ID:Darwin>:${APPLE_TEST_SOURCES}>
)

target_link_libraries(test_runtime PRIVATE
    uzengine_editor
    uzengine_runtime
    gtest
)

# Test-only include roots (mocks shared across suites)
target_include_directories(test_runtime PRIVATE
    tests
    tests/engine
    tests/engine/rhi
    tests/engine/vfx
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
    set(TOOL_EXE_SOURCES ${tool_src} ${ECS_ENV_SOURCES})
    add_executable(${tool_name} ${TOOL_EXE_SOURCES})
    target_link_libraries(${tool_name} PRIVATE uzengine_editor uzengine_runtime)
endforeach()

# --- Demo executables ---
file(GLOB DEMO_DIRS demos/*)
foreach(demo_dir IN LISTS DEMO_DIRS)
    if(NOT IS_DIRECTORY ${demo_dir})
        continue()
    endif()
    get_filename_component(demo_name ${demo_dir} NAME)
    # vfx_viewport demos drive the full engine VFX renderer, whose runtime
    # shader compilation is Windows-only today.
    if(NOT WIN32 AND demo_name MATCHES "^vfx_viewport")
        continue()
    endif()
    file(GLOB DEMO_SOURCES ${demo_dir}/*.cpp)
    # demos/common hosts the shared DemoApplication (used by non-triangle demos)
    file(GLOB DEMO_COMMON_SOURCES demos/common/demo_application*.cpp)
    add_executable(${demo_name}_demo ${DEMO_SOURCES} ${ECS_ENV_SOURCES})
    target_include_directories(${demo_name}_demo PRIVATE demos/common)
    if(demo_name MATCHES "^vfx_viewport")
        target_sources(${demo_name}_demo PRIVATE ${DEMO_COMMON_SOURCES})
    endif()
    target_link_libraries(${demo_name}_demo PRIVATE uzengine_editor uzengine_runtime)
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
                ${UZ_DXC_ROOT}/bin/x64/dxcompiler.dll
                $<TARGET_FILE_DIR:${tgt}>/dxcompiler.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${UZ_DXC_ROOT}/bin/x64/dxil.dll
                $<TARGET_FILE_DIR:${tgt}>/dxil.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${UZ_FXC_ROOT}/bin/x64/d3dcompiler_47.dll
                $<TARGET_FILE_DIR:${tgt}>/d3dcompiler_47.dll
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${UZ_LUAJIT_ROOT}/lua51.dll
                $<TARGET_FILE_DIR:${tgt}>/lua51.dll
            COMMENT "Deploying runtime DLLs for ${tgt}"
        )
    endforeach()
endif()
