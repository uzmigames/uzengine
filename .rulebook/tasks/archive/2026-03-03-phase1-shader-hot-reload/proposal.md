# Proposal: phase1-shader-hot-reload

## Why

UzEngine currently embeds all HLSL shader source as `constexpr` strings compiled directly into the C++ binary. This means any shader change requires a full C++ rebuild and engine restart — a cycle that takes tens of seconds and completely breaks the rapid iteration workflow that a game engine editor must provide. Shader authoring is one of the most iterative tasks in game development.

Moving shaders to external `.hlsl` files and hot-reloading them on save reduces the shader iteration cycle from a full rebuild (30-60 seconds) to an in-place PSO swap under 1 second. This is a fundamental quality-of-life improvement for every rendering feature that will be built on top of this engine. Without it, every frame graph pass and material experiment requires stopping work and waiting for a build.

VSCode diagnostics integration closes the loop: when DXC reports a compile error, the error is surfaced as a red squiggle with file/line/column in the editor, exactly where the developer is looking. Without this feedback channel, the developer must hunt through engine logs to find the compile error — which defeats the purpose of having an integrated editor.

## What Changes

- Extract all embedded HLSL source strings from C++ files and write them to external `.hlsl` files under `shaders/` in the project directory structure
- Implement a `ShaderCompiler` class that invokes DXC (from the Windows SDK or vcpkg `directx-dxc`) via `IDxcCompiler3`, returning either DXIL bytecode (`IDxcBlob`) or a structured error list with file, line, column, and message fields
- Implement a `ShaderManager` class responsible for loading, caching, and hot-swapping compiled shaders; on successful recompile it rebuilds the affected `ID3D12PipelineState` objects in-place without interrupting the render loop
- Add a `FileWatcher` class using `ReadDirectoryChangesW` on the `shaders/` directory that posts `ShaderFileChanged` events to the engine's main-thread queue when any `.hlsl` file is written
- Connect `FileWatcher` events to `ShaderManager::reload(path)` so that saving a `.hlsl` file in VSCode automatically triggers recompile and PSO swap within 1 second
- Register `shader.reload`, `shader.list`, and `shader.get_errors` IPC command handlers so the VSCode extension and AI agents can trigger and query shader compilation programmatically
- Implement the VSCode extension side: on `.hlsl` document save, send `shader.reload` IPC; on response parse the error list and publish each entry to the VS Code `DiagnosticCollection` with the correct URI, range, and severity
- Add `shader.reload` to the MCP tool surface so AI agents (e.g. `shader_assist`) can trigger compilation and inspect errors after proposing shader edits
- Support `#include` resolution in DXC by pointing the include handler at `shaders/include/` for shared `.hlsli` library files
- Add runtime fallback: if a shader fails to compile during hot-reload, the engine keeps the previous PSO active and surfaces the error without crashing

## Impact

- Affected specs: `docs/specs/shader-system.md`, `docs/specs/shader-library.md`, `docs/specs/shader-variants.md`, `docs/specs/ai-native-features.md`
- Affected code: `src/renderer/shader_compiler.cpp`, `src/renderer/shader_compiler.h`, `src/renderer/shader_manager.cpp`, `src/renderer/shader_manager.h`, `src/platform/file_watcher.cpp`, `src/ipc/shader_handlers.cpp`, `vscode-extension/src/diagnostics/ShaderDiagnostics.ts`, `shaders/`
- Breaking change: YES — embedded shader strings are removed from C++ source; the `shaders/` directory becomes a required runtime dependency
- User benefit: Shader changes are visible in the running viewport within 1 second of saving the file, with compile errors shown as inline VS Code diagnostics, reducing the shader iteration cycle from a full rebuild to a near-instant hot-swap
