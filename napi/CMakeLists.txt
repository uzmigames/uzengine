# /// UzEngine N-API Native Addon Build Configuration
# ///
# /// MSVC compiles .cpp (Electron delay-load hook requires MSVC ABI).
# /// Links against uzengine_runtime.lib (import lib for the Zig-built DLL).
# /// The DLL carries its own libc++, avoiding C++ ABI incompatibility.
# ///
# /// # Prerequisites
# ///   cd runtime && zig build -Ddll=true
# ///
# /// # Build (from VS Developer Command Prompt)
# ///   cd napi && npx cmake-js compile -d . --runtime=electron --runtime-version=39.8.0

cmake_minimum_required(VERSION 3.15)
project(uzengine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cmake-js Integration
include_directories(${CMAKE_JS_INC})

# Node-Addon-API Headers
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
include_directories(${NODE_ADDON_API_DIR})

# N-API Configuration
add_definitions(-DNAPI_VERSION=10)
add_definitions(-DNAPI_DISABLE_CPP_EXCEPTIONS)
# Tell runtime.h to use __declspec(dllimport) for uz_* functions
add_definitions(-DUZ_USING_DLL)

# Runtime C API Header
include_directories(${CMAKE_SOURCE_DIR}/../runtime/engine/app)

# Source Files
add_library(${PROJECT_NAME} SHARED
  src/addon.cpp
  src/napi_lifecycle.cpp
  src/napi_viewport.cpp
  src/napi_scene.cpp
  src/napi_input.cpp
  src/napi_diagnostics.cpp
  src/render_thread.cpp
  ${CMAKE_JS_SRC}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

# Link against DLL import lib (MSVC-compatible, 6KB — all C++ internals are in the DLL)
find_library(UZENGINE_DLL_IMPORT
  NAMES uzengine_runtime
  PATHS ${CMAKE_SOURCE_DIR}/../runtime/build/lib
  NO_DEFAULT_PATH
)

if(NOT UZENGINE_DLL_IMPORT)
  message(FATAL_ERROR
    "uzengine_runtime.lib not found — run 'cd runtime && zig build -Ddll=true' first")
endif()

target_link_libraries(${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  ${UZENGINE_DLL_IMPORT}
  delayimp.lib
)

# Copy DLL + dependencies alongside uzengine.node
set(RUNTIME_BIN_DIR ${CMAKE_SOURCE_DIR}/../runtime/build/bin)
set(FFMPEG_BIN_DIR ${CMAKE_SOURCE_DIR}/../runtime/thirdparty/ffmpeg/bin)

file(GLOB RUNTIME_DLLS "${RUNTIME_BIN_DIR}/*.dll")
file(GLOB FFMPEG_DLLS "${FFMPEG_BIN_DIR}/*.dll")

# All runtime DLLs go flat into the build output dir.
if(RUNTIME_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Copying runtime DLLs..."
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_DLLS} $<TARGET_FILE_DIR:${PROJECT_NAME}>/
  )
endif()
if(FFMPEG_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Copying FFmpeg DLLs (H.264 encoding)..."
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} $<TARGET_FILE_DIR:${PROJECT_NAME}>/
  )
endif()
