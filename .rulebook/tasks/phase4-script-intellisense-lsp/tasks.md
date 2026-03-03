## 1. Type Definition Generator
- [ ] 1.1 Create `src/scripting/api_export.cpp` that enumerates all registered `ctx.*` namespace entries (scene, ui, audio, physics, timers, saves) at build time and writes a `ctx.d.lua` Lua type definition file
- [ ] 1.2 Define the output format for `ctx.d.lua`: LuaLS-compatible `---@class`, `---@field`, `---@param`, and `---@return` annotation syntax for every public method and property
- [ ] 1.3 Add documentation strings as LuaLS `---` comment annotations above each function definition in `ctx.d.lua` (parameter names, types, brief description from source doc-comments)
- [ ] 1.4 Integrate the generator as a CMake `add_custom_command` post-build step that re-runs whenever any `src/scripting/*.cpp` file changes, outputting `build/<Config>/ctx.d.lua`
- [ ] 1.5 Copy the generated `ctx.d.lua` into the VSCode extension's bundled assets directory during build so it ships with the extension

## 2. LuaLS VSCode Extension Integration
- [ ] 2.1 Add `sumneko.lua` (LuaLS) as a required extension dependency in `vscode-uzengine/package.json` under `extensionDependencies`
- [ ] 2.2 Add activation logic in `vscode-uzengine/src/extension.ts` that runs when a `.uzproj` workspace is detected, ensuring LuaLS is active for that workspace
- [ ] 2.3 Write `vscode-uzengine/src/luaSetup.ts` with a `setupLuaLS(workspacePath: string)` function that configures LuaLS settings for the UzEngine sandbox

## 3. Workspace Configuration
- [ ] 3.1 Generate a `.luarc.json` file in the project root when a UzEngine project is opened; the file points LuaLS at the bundled `ctx.d.lua` via the `workspace.library` field
- [ ] 3.2 Set `runtime.version = "Lua 5.4"` and `diagnostics.globals = ["ctx", "vec3", "quat", "color"]` in the generated `.luarc.json` to match the engine sandbox
- [ ] 3.3 Set `workspace.ignoreDir` to exclude `build/`, `vscode-uzengine/node_modules/`, and `.rulebook/` from LuaLS indexing
- [ ] 3.4 Add the `.luarc.json` path to `.gitignore` if not already present, since it is a generated file

## 4. Go-To-Definition Support
- [ ] 4.1 Verify that LuaLS resolves go-to-definition for `ctx.*` function calls into the generated `ctx.d.lua` type file (manual test with a sample Lua script)
- [ ] 4.2 Ensure function definitions in `ctx.d.lua` include `---@source` annotations pointing to the originating C++ file and line number to make go-to-definition maximally useful

## 5. Hover Tooltip Validation
- [ ] 5.1 Verify hover tooltips appear for `ctx.scene:spawn()`, `ctx.ui:show()`, `ctx.audio:play()`, and `ctx.physics:raycast()` showing parameter names and return types
- [ ] 5.2 Verify that incorrect argument counts trigger a LuaLS diagnostic in the Problems panel

## 6. Testing
- [ ] 6.1 Create `vscode-uzengine/src/__tests__/luaSetup.test.ts` with Jest test suite `LuaSetupTests`
- [ ] 6.2 Write test `GeneratesLuarcJson`: call `setupLuaLS(mockWorkspacePath)` and verify the generated `.luarc.json` contains `workspace.library` pointing to `ctx.d.lua`, `runtime.version = "Lua 5.4"`, and the correct `diagnostics.globals` array
- [ ] 6.3 Write test `LuarcJsonNotOverwrittenIfUnchanged`: call `setupLuaLS` twice with identical config; verify the file is not written a second time (no unnecessary disk writes)
- [ ] 6.4 Write test `CtxDLuaContainsAllNamespaces`: parse the generated `ctx.d.lua` content and assert it contains `ctx.scene`, `ctx.ui`, `ctx.audio`, `ctx.physics`, `ctx.timers`, and `ctx.saves` class definitions
- [ ] 6.5 Write test `CtxDLuaFunctionHasAnnotations`: assert that each function entry in `ctx.d.lua` has at least one `---@param` or `---@return` annotation
- [ ] 6.6 Write test `ExtensionDependencyDeclared`: parse `package.json` and assert `sumneko.lua` appears in `extensionDependencies`
- [ ] 6.7 Verify Jest line coverage >= 95% and branch coverage >= 95% for `luaSetup.ts` and `extension.ts` activation paths
- [ ] 6.8 Write test `SetupHandlesWorkspaceWithNoLuaFiles`: call `setupLuaLS` on an empty directory; verify it completes without throwing

## 7. Documentation
- [ ] 7.1 Update `docs/specs/lua-scripting.md` with a section describing the LuaLS integration, how `ctx.d.lua` is generated, and how to regenerate it after adding a new API
- [ ] 7.2 Update `docs/specs/editor-panels.md` with the LuaLS configuration flow and the `.luarc.json` generated file description
