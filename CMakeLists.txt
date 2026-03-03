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

    add_executable(UzEngine WIN32
        src/main.cpp
        src/renderer/renderer.cpp
        src/renderer/fps_overlay.cpp
        src/encoder/jpeg_encoder.cpp
        src/network/ws_server.cpp
        src/common/ipc_registry.cpp
        src/common/ipc_poller.cpp
        src/common/stb_impl.cpp
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

# Core + Renderer Layer tests
add_executable(UzEngineTests
    src/engine/core/arena.cpp
    src/engine/core/string_id.cpp
    src/engine/renderer/rhi/rhi_common.c
    tests/core/test_arena.cpp
    tests/core/test_handle.cpp
    tests/core/test_string_id.cpp
    tests/core/test_result.cpp
    tests/core/test_math.cpp
    tests/core/test_main.cpp
    tests/renderer/test_rhi_common.cpp
)

target_include_directories(UzEngineTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(UzEngineTests PRIVATE
    GTest::gtest
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
