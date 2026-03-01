cmake_minimum_required(VERSION 3.20)
project(UzEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# IXWebSocket via vcpkg
find_package(ixwebsocket CONFIG REQUIRED)

# nlohmann-json via vcpkg
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(UzEngine WIN32
    src/main.cpp
    src/renderer/renderer.cpp
    src/renderer/fps_overlay.cpp
    src/encoder/nvenc_encoder.cpp
    src/encoder/ffmpeg_encoder.cpp
    src/encoder/encoder_factory.cpp
    src/muxer/fmp4_muxer.cpp
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
    mf
    mfuuid
    mfplat
    ole32
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
)

# Enable debug layer in Debug builds
target_compile_definitions(UzEngine PRIVATE
    $<$<CONFIG:Debug>:UZENGINE_DEBUG=1>
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
