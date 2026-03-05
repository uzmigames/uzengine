cmake_minimum_required(VERSION 3.20)
project(UzEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Ninja multi-config: prefer Ninja when available for fast incremental builds.
# Configure with:
#   cmake -B build -G "Ninja Multi-Config" -DCMAKE_TOOLCHAIN_FILE=...
# Build with:
#   cmake --build build --config Release --parallel
#   cmake --build build --config Debug   --parallel
# ---------------------------------------------------------------------------
if(CMAKE_GENERATOR MATCHES "Ninja")
    message(STATUS "Generator: Ninja (parallel build enabled)")
endif()

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
if(WIN32)
    message(STATUS "Platform: Windows — building UzEngine + UzEngineTests")
else()
    message(STATUS "Platform: ${CMAKE_SYSTEM_NAME} — building UzEngineTests only (no DX12/Win32)")
endif()

# ---------------------------------------------------------------------------
# Windows-only: UzEngine executable (DX12 renderer)
# ---------------------------------------------------------------------------
if(WIN32)
    # IXWebSocket via vcpkg
    find_package(ixwebsocket CONFIG REQUIRED)

    # nlohmann-json via vcpkg
    find_package(nlohmann_json CONFIG REQUIRED)

    # libjpeg-turbo via vcpkg
    find_package(libjpeg-turbo CONFIG REQUIRED)

    # -----------------------------------------------------------------------
    # Auto-discover source files (UE5-style: drop a .cpp and it compiles).
    # CONFIGURE_DEPENDS makes Ninja re-run CMake when files are added/removed.
    # -----------------------------------------------------------------------
    # Legacy renderer glob removed — all renderer files are now under
    # src/engine/renderer/ and discovered by UZENGINE_ENGINE.
    file(GLOB_RECURSE UZENGINE_ENGINE CONFIGURE_DEPENDS
        src/engine/scene/*.cpp
        src/engine/core/*.cpp
        src/engine/editor/*.cpp
        src/engine/debug/*.cpp
        src/engine/renderer/*.cpp
    )
    file(GLOB_RECURSE UZENGINE_COMMON CONFIGURE_DEPENDS
        src/common/*.cpp
    )
    file(GLOB_RECURSE UZENGINE_ENCODER CONFIGURE_DEPENDS
        src/encoder/*.cpp
    )
    file(GLOB_RECURSE UZENGINE_NETWORK CONFIGURE_DEPENDS
        src/network/*.cpp
    )
    file(GLOB_RECURSE UZENGINE_EDITOR_APP CONFIGURE_DEPENDS
        src/editor/*.cpp
    )
    # RHI C backend (DX12 for Windows)
    file(GLOB_RECURSE UZENGINE_RHI_C CONFIGURE_DEPENDS
        src/engine/renderer/rhi/*.c
    )

    add_executable(UzEngine WIN32
        src/main.cpp
        ${UZENGINE_ENGINE}
        ${UZENGINE_COMMON}
        ${UZENGINE_ENCODER}
        ${UZENGINE_NETWORK}
        ${UZENGINE_EDITOR_APP}
        ${UZENGINE_RHI_C}
    )

    target_include_directories(UzEngine PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/external/stb
        ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
    )

    target_link_libraries(UzEngine PRIVATE
        d3d12
        dxgi
        d3dcompiler
        dxguid
        ole32
        winmm
        ixwebsocket::ixwebsocket
        nlohmann_json::nlohmann_json
        $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
    )

    # Copy vcpkg DLLs to the output directory after build (Windows dynamic linking).
    add_custom_command(TARGET UzEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/turbojpeg.dll"
            "$<TARGET_FILE_DIR:UzEngine>"
        COMMENT "Copying turbojpeg.dll to output directory"
    )

    # Enable debug layer in Debug builds
    target_compile_definitions(UzEngine PRIVATE
        $<$<CONFIG:Debug>:UZENGINE_DEBUG=1>
        UNICODE
        _UNICODE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

# ============================================================
# Tests  (all platforms)
# ============================================================
enable_testing()
find_package(GTest CONFIG REQUIRED)

# nlohmann-json (needed by IPC handlers — available on all platforms via vcpkg)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

# -----------------------------------------------------------------------
# Auto-discover test sources (UE5-style: drop a .cpp and it compiles).
# CONFIGURE_DEPENDS re-runs CMake when files are added/removed.
# -----------------------------------------------------------------------
# Engine sources (no legacy renderer, no DX12 backend for tests).
file(GLOB_RECURSE TEST_ENGINE_SOURCES CONFIGURE_DEPENDS
    src/engine/core/*.cpp
    src/engine/scene/*.cpp
    src/engine/editor/*.cpp
    src/engine/debug/*.cpp
    src/engine/renderer/*.cpp        # ShaderManager, RenderGraph, ViewportRegistry, etc.
)
# Exclude DX12-dependent renderer files from test builds.
list(FILTER TEST_ENGINE_SOURCES EXCLUDE REGEX "engine/renderer/renderer\\.cpp$")
list(FILTER TEST_ENGINE_SOURCES EXCLUDE REGEX "engine/renderer/fps_overlay\\.cpp$")
list(FILTER TEST_ENGINE_SOURCES EXCLUDE REGEX "engine/renderer/components/")
list(FILTER TEST_ENGINE_SOURCES EXCLUDE REGEX "engine/renderer/gizmos/")
# Common sources — exclude stb_impl.cpp (needs external/stb) and ipc_poller.cpp (needs ixwebsocket)
file(GLOB_RECURSE TEST_COMMON_SOURCES CONFIGURE_DEPENDS
    src/common/*.cpp
)
list(FILTER TEST_COMMON_SOURCES EXCLUDE REGEX "stb_impl\\.cpp$")
list(FILTER TEST_COMMON_SOURCES EXCLUDE REGEX "ipc_poller\\.cpp$")

# RHI common (C) — platform-agnostic only, NOT the DX12 backend
set(TEST_RHI_C_SOURCES src/engine/renderer/rhi/rhi_common.c)

# Test sources
file(GLOB_RECURSE TEST_TEST_SOURCES CONFIGURE_DEPENDS
    tests/*.cpp
)

add_executable(UzEngineTests
    ${TEST_ENGINE_SOURCES}
    ${TEST_COMMON_SOURCES}
    ${TEST_RHI_C_SOURCES}
    ${TEST_TEST_SOURCES}
)

target_include_directories(UzEngineTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(UzEngineTests PRIVATE
    GTest::gtest
    nlohmann_json::nlohmann_json
    $<$<NOT:$<BOOL:${WIN32}>>:pthread>
    $<$<NOT:$<BOOL:${WIN32}>>:dl>
)

target_compile_definitions(UzEngineTests PRIVATE
    UZENGINE_DEBUG=1
    NOMINMAX
    $<$<BOOL:${WIN32}>:UNICODE _UNICODE WIN32_LEAN_AND_MEAN>
)

# Suppress -Wunused-result inside EXPECT_DEATH macros (nodiscard on value()/error()/take()
# is intentional in production code, but EXPECT_DEATH must call them as discarded exprs).
target_compile_options(UzEngineTests PRIVATE
    $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-unused-result>
)

include(GoogleTest)
gtest_discover_tests(UzEngineTests)
