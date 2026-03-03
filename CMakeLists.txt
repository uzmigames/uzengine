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

# nlohmann-json (needed by IPC handlers — available on all platforms via vcpkg)
if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()

# Core + Renderer Layer tests
add_executable(UzEngineTests
    src/engine/core/arena.cpp
    src/engine/core/string_id.cpp
    src/engine/core/timer_system.cpp
    src/engine/core/event_bus.cpp
    src/engine/renderer/rhi/rhi_common.c
    src/common/ipc_registry.cpp
    src/engine/scene/scene_manager.cpp
    src/engine/scene/scene_loader.cpp
    src/engine/scene/scene_saver.cpp
    src/engine/scene/scene_handlers.cpp
    src/engine/editor/undo_redo.cpp
    src/engine/editor/asset_registry.cpp
    src/engine/editor/ipc_asset_handlers.cpp
    src/engine/editor/ipc_playmode_handlers.cpp
    src/engine/debug/console.cpp
    src/engine/core/platform.cpp
    src/engine/core/file_watcher.cpp
    src/engine/renderer/shader_manager.cpp
    src/common/ipc_shader_handlers.cpp
    src/engine/renderer/viewport_registry.cpp
    src/common/ipc_viewport_handlers.cpp
    src/engine/core/crash_reporter.cpp
    src/engine/renderer/render_graph.cpp
    tests/core/test_arena.cpp
    tests/core/test_handle.cpp
    tests/core/test_string_id.cpp
    tests/core/test_result.cpp
    tests/core/test_math.cpp
    tests/core/test_scene.cpp
    tests/core/test_scene_io.cpp
    tests/core/test_scene_handlers.cpp
    tests/core/test_timer_system.cpp
    tests/core/test_event_bus.cpp
    tests/editor/test_undo_redo.cpp
    tests/editor/test_asset_registry.cpp
    tests/debug/test_console.cpp
    tests/core/test_assert.cpp
    tests/core/test_profiler.cpp
    tests/core/test_platform.cpp
    tests/core/test_main.cpp
    tests/renderer/test_rhi_common.cpp
    tests/core/test_file_watcher.cpp
    tests/renderer/test_shader_manager.cpp
    tests/renderer/test_viewport_registry.cpp
    tests/core/test_crash_reporter.cpp
    tests/renderer/test_render_graph.cpp
    tests/core/test_engine_mode.cpp
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
