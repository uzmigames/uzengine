# /// UzEngine N-API Native Addon Build Configuration
# ///
# /// MSVC cl.exe compiles .cpp (delay-load hook requires MSVC ABI).
# /// lld-link links .node (reads Zig/LLVM COMDAT sections that MSVC link.exe can't).
# /// Both runtime.lib and editor.lib from the Zig build are linked directly.
# ///
# /// # Build
# /// Use NMake generator from a VS Developer Command Prompt:
# ///   cmake-js compile -d . --runtime=electron --runtime-version=39.6.0 -G "NMake Makefiles"

cmake_minimum_required(VERSION 3.15)

# Force lld-link BEFORE project() — NMake generator respects CMAKE_LINKER.
find_program(LLD_LINK lld-link REQUIRED)
set(CMAKE_LINKER ${LLD_LINK})

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
  ${CMAKE_JS_SRC}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

# Runtime + Editor Libraries (Zig-built, LLVM archive format — lld-link reads them)
set(RUNTIME_LIB_DIR ${CMAKE_SOURCE_DIR}/../runtime/build/lib)

find_library(UZENGINE_RUNTIME NAMES runtime PATHS ${RUNTIME_LIB_DIR} NO_DEFAULT_PATH)
find_library(UZENGINE_EDITOR  NAMES editor  PATHS ${RUNTIME_LIB_DIR} NO_DEFAULT_PATH)

if(NOT UZENGINE_RUNTIME)
  message(FATAL_ERROR "runtime.lib not found in ${RUNTIME_LIB_DIR} — run 'cd runtime && zig build' first")
endif()
if(NOT UZENGINE_EDITOR)
  message(FATAL_ERROR "editor.lib not found in ${RUNTIME_LIB_DIR} — run 'cd runtime && zig build -Deditor=true' first")
endif()

target_link_libraries(${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  ${UZENGINE_RUNTIME}
  ${UZENGINE_EDITOR}
  # Win32 system libs required by the runtime
  d3d12 d3d11 dxgi dxguid gdi32 user32 ole32 shcore shell32 comdlg32 xinput
)

# Copy DLLs needed at runtime alongside uzengine.node
set(RUNTIME_BIN_DIR ${CMAKE_SOURCE_DIR}/../runtime/build/bin)

file(GLOB RUNTIME_DLLS "${RUNTIME_BIN_DIR}/*.dll")
file(GLOB AGILITY_DLLS "${RUNTIME_BIN_DIR}/D3D12/*.dll")

if(RUNTIME_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMENT "Copying runtime DLLs..."
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_DLLS} $<TARGET_FILE_DIR:${PROJECT_NAME}>/
  )
endif()
if(AGILITY_DLLS)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${AGILITY_DLLS} $<TARGET_FILE_DIR:${PROJECT_NAME}>/D3D12/
  )
endif()
