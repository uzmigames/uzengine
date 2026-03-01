cmake_minimum_required(VERSION 3.20)
project(UzEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# DirectX 12 (Windows SDK)
find_library(D3D12_LIB d3d12)
find_library(DXGI_LIB dxgi)
find_library(D3DCOMPILER_LIB d3dcompiler)
find_library(DXGUID_LIB dxguid)

add_executable(UzEngine WIN32
    src/main.cpp
    src/renderer/renderer.cpp
    src/renderer/fps_overlay.cpp
    src/encoder/nvenc_encoder.cpp
    src/encoder/ffmpeg_encoder.cpp
    src/encoder/encoder_factory.cpp
    src/muxer/fmp4_muxer.cpp
)

target_include_directories(UzEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
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
)

# Enable debug layer in Debug builds
target_compile_definitions(UzEngine PRIVATE
    $<$<CONFIG:Debug>:UZENGINE_DEBUG=1>
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)
