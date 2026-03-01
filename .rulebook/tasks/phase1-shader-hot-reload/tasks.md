## 1. DXC Integration

- [ ] 1.1 Add `directx-dxc` to `vcpkg.json` (or locate Windows SDK dxc.exe)
- [ ] 1.2 Verify DXC available: `dxc.exe --version` or DXC API via `dxcapi.h`
- [ ] 1.3 CMakeLists: link `dxcompiler.lib`, copy `dxcompiler.dll` to output

## 2. Shader Compiler (`src/renderer/shader_compiler.h/cpp`)

- [ ] 2.1 `ShaderCompiler::compile(path, stage, entry)` → `CompileResult{bytecode, errors}`
- [ ] 2.2 `CompileResult::errors` is a list of `{file, line, col, message}` structs
- [ ] 2.3 Support stages: `VS` (VSMain), `PS` (PSMain), `CS` (CSMain)
- [ ] 2.4 Debug build: compile with `-Od -Zi` (debug info for PIX/RenderDoc)
- [ ] 2.5 Release build: compile with `-O3`
- [ ] 2.6 Include path: `shaders/` directory added to DXC include search

## 3. Shader Manager (`src/renderer/shader_manager.h/cpp`)

- [ ] 3.1 `ShaderManager::load(name, vs_path, ps_path)` — compiles + creates PSO
- [ ] 3.2 `ShaderManager::reload(path)` — async recompile, atomic PSO swap on success
- [ ] 3.3 Reload failure: keep old PSO, return errors, log warning
- [ ] 3.4 `ShaderManager::get_errors(path)` — last compile errors for a shader file
- [ ] 3.5 `ShaderManager::list()` — returns all registered shader names/paths

## 4. File Watcher (`src/common/file_watcher.h/cpp`)

- [ ] 4.1 `FileWatcher::watch(directory, callback)` — uses `ReadDirectoryChangesW`
- [ ] 4.2 Callback receives `{path, event_type}` on modify/create/delete
- [ ] 4.3 Debounce: 200ms — avoid multiple events for a single save
- [ ] 4.4 Engine registers watcher for `shaders/` directory on startup
- [ ] 4.5 On `.hlsl` change: `ShaderManager::reload(changed_path)`

## 5. Built-in Shaders (External Files)

- [ ] 5.1 Create `shaders/builtin/unlit.hlsl` — albedo only, no lighting
- [ ] 5.2 Create `shaders/builtin/pbr.hlsl` — PBR GGX stub (fully implemented in Phase 2)
- [ ] 5.3 Create `shaders/builtin/wireframe.hlsl` — debug wireframe pass
- [ ] 5.4 Create `shaders/builtin/depth.hlsl` — shadow map / depth-only pass
- [ ] 5.5 Remove embedded `constexpr` HLSL strings from `renderer.cpp`

## 6. IPC Handlers (shader.*)

- [ ] 6.1 Register `shader.reload` — recompile by path, return `{ok, errors, elapsed_ms}`
- [ ] 6.2 Register `shader.getErrors` — return last compile errors for path
- [ ] 6.3 Register `shader.list` — return all project shader paths
- [ ] 6.4 Register `shader.getSource` — return file contents (for AI agents)
- [ ] 6.5 Register `shader.setSource` — write file + trigger reload (for AI agents)

## 7. VSCode Extension — Shader Integration

- [ ] 7.1 Create `src/shader-watcher.ts` — `onDidSaveTextDocument` for `.hlsl` / `.glsl` files
- [ ] 7.2 On save: call `shader.reload` IPC, parse error response
- [ ] 7.3 Create `vscode.DiagnosticCollection` `uzengine-shaders`
- [ ] 7.4 Map IPC errors to `vscode.Diagnostic` objects with correct file/line/col
- [ ] 7.5 On success: clear diagnostics for that file, update status bar "✓ Compiled"
- [ ] 7.6 Status bar item: shows compilation state for the active `.hlsl` file
- [ ] 7.7 Recommend "HLSL Tools for Visual Studio Code" in extension `extensionDependencies`

## 8. Testing

- [ ] 8.1 Edit `shaders/builtin/unlit.hlsl`, save → engine hot-reloads, viewport updates
- [ ] 8.2 Introduce a syntax error → red squiggle at correct line in VSCode
- [ ] 8.3 Fix the error → squiggle clears, shader reloads
- [ ] 8.4 `shader.reload` via MCP tool call works from AI agent
- [ ] 8.5 `shader.setSource` via MCP — AI writes shader, live update in viewport
