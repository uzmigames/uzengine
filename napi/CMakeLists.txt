# /// UzEngine N-API Native Addon Build Configuration
# ///
# /// Configures CMake to build the UzEngine native addon (.node file) for Node.js.
# /// The addon bridges JavaScript/TypeScript with the native C++ runtime.
# ///
# /// # Key Features
# /// - Compiled as a shared library (.node) for direct Node.js loading
# /// - Uses Node-Addon-API (N-API) for stable ABI across Node.js versions
# /// - Includes delay-load hook for Electron process redirection
# /// - Links to Zig-built runtime static library
# /// - Disables C++ exceptions (NAPI compatibility on Electron)

cmake_minimum_required(VERSION 3.15)
project(uzengine LANGUAGES CXX)

# /// C++ Standard Configuration
# /// Requires C++17 for modern language features (optional, structured bindings, etc.)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /// cmake-js Integration
# /// Provides three critical variables:
# /// - CMAKE_JS_INC: Node.js headers directory
# /// - CMAKE_JS_LIB: Node.js libraries for linking
# /// - CMAKE_JS_SRC: Delay-load hook (mandatory for Electron support)
include_directories(${CMAKE_JS_INC})

# /// Node-Addon-API Header Discovery
# /// Dynamically queries npm package to locate node-addon-api headers.
# /// This ensures compatibility across different installations.
execute_process(
  COMMAND node -p "require('node-addon-api').include"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE NODE_ADDON_API_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
include_directories(${NODE_ADDON_API_DIR})

# /// N-API Configuration
# /// - Version 10: Stable ABI compatible with Node.js 14+
# /// - NAPI_DISABLE_CPP_EXCEPTIONS: CRITICAL for Electron
# ///   Using NAPI_CPP_EXCEPTIONS crashes on Windows/Electron (electron#22799)
# ///   All C++ exceptions must be caught and converted to N-API errors
add_definitions(-DNAPI_VERSION=10)
add_definitions(-DNAPI_DISABLE_CPP_EXCEPTIONS)

# /// Runtime Library Headers
# /// Include paths for the C++ runtime implementation
include_directories(${CMAKE_SOURCE_DIR}/../runtime/src)

# /// Build the N-API Addon
# /// Creates uzengine.node (shared library):
# /// - src/addon.cpp: N-API bindings and entry points
# /// - ${CMAKE_JS_SRC}: Delay-load hook for Electron process redirection
add_library(${PROJECT_NAME} SHARED
  src/addon.cpp
  ${CMAKE_JS_SRC}
)

# /// Output Configuration
# /// - PREFIX: Remove "lib" prefix (Windows/Unix convention)
# /// - SUFFIX: .node extension for Node.js native module
set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  SUFFIX ".node"
)

# /// Runtime Library Discovery
# /// Locates the Zig-built static library (uzengine_runtime.lib on Windows).
# /// NO_DEFAULT_PATH ensures we use only the specified directory.
find_library(UZENGINE_RUNTIME uzengine_runtime
  PATHS ${CMAKE_SOURCE_DIR}/../runtime/zig-out/lib
  NO_DEFAULT_PATH
)

# /// Link Dependencies
# /// - CMAKE_JS_LIB: Node.js runtime library
# /// - UZENGINE_RUNTIME: Zig-built C++ runtime static library
target_link_libraries(${PROJECT_NAME}
  ${CMAKE_JS_LIB}
  ${UZENGINE_RUNTIME}
)
