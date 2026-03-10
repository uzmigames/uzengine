cmake_minimum_required(VERSION 3.15)
project(uzengine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# cmake-js provides: CMAKE_JS_INC, CMAKE_JS_LIB, CMAKE_JS_SRC
include_directories(${CMAKE_JS_INC})

# node-addon-api headers
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
include_directories(${NODE_ADDON_API_DIR})

add_definitions(-DNAPI_VERSION=10)
# NAPI_CPP_EXCEPTIONS crashes on Electron/Windows (electron#22799)
add_definitions(-DNAPI_DISABLE_CPP_EXCEPTIONS)

# Runtime headers
include_directories(${CMAKE_SOURCE_DIR}/../runtime/src)

add_library(${PROJECT_NAME} SHARED
  src/addon.cpp
  ${CMAKE_JS_SRC}  # delay-load hook: redirects node.exe -> Code.exe on Electron
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

# Link runtime static lib (built by zig)
find_library(UZENGINE_RUNTIME uzengine_runtime
  PATHS ${CMAKE_SOURCE_DIR}/../runtime/zig-out/lib
  NO_DEFAULT_PATH
)

target_link_libraries(${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  ${UZENGINE_RUNTIME}
)
