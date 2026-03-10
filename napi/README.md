# @uzmigames/napi

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![N-API](https://img.shields.io/badge/N--API-10-green.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

Thin N-API bridge between the UzEngine runtime and VSCode/Electron. Wraps the runtime's C API into a `.node` addon that the extension can `require()`.

**This module does NOT contain engine logic.** It only marshals calls between JavaScript and the runtime static library.

## Build

```bash
# Install dependencies
npm install

# Build (compiles runtime first, then napi addon)
npm run build

# Debug build
npm run build:debug
```

### Requirements

- [`runtime/`](../runtime/) must be built first (`zig build`)
- Visual Studio Build Tools 2022 (MSVC compiler required for delay-load support)
- cmake-js handles Electron headers, `node.lib`, and `/DELAYLOAD:NODE.EXE` automatically

## How It Works

1. cmake-js downloads Electron 39.6.0 headers and `node.lib`
2. MSVC compiles `addon.cpp` + `win_delay_load_hook.cc` (from cmake-js)
3. Links against `uzengine_runtime.lib` (from runtime/zig-out/) and `node.lib`
4. `/DELAYLOAD:NODE.EXE` ensures napi symbols resolve from `Code.exe` (Electron) at runtime
5. Output: `build/Release/uzengine.node`

## Directory Structure

```
napi/
├── CMakeLists.txt     # cmake-js build (MSVC, delay-load, Electron headers)
├── package.json       # @uzmigames/napi — node-addon-api + cmake-js
└── src/
    └── addon.cpp      # N-API bindings wrapping runtime C API
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
