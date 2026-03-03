## 1. luacheck Integration in VSCode Extension
- [ ] 1.1 Bundle the luacheck binary (or invoke it via npx/PATH) in `vscode-uzengine/src/luacheck.ts`; implement `runLuacheck(filePath: string): Promise<LuacheckDiagnostic[]>` that spawns the process and parses stdout
- [ ] 1.2 Define `LuacheckDiagnostic` interface in `vscode-uzengine/src/luacheck.ts` with fields `file`, `line`, `column`, `code`, `message`, `severity: "error" | "warning"`
- [ ] 1.3 Implement `LuacheckDiagnosticsProvider` class in `vscode-uzengine/src/luacheckProvider.ts` implementing `vscode.Disposable`; register it as a `vscode.languages.createDiagnosticCollection("luacheck")` provider
- [ ] 1.4 Wire the provider to activate on every `onDidSaveTextDocument` and `onDidOpenTextDocument` event for files matching `**/*.lua` glob pattern
- [ ] 1.5 Map luacheck output codes to VSCode `vscode.DiagnosticSeverity.Error` or `Warning` and push diagnostics to the Problems panel via `DiagnosticCollection.set(uri, diagnostics)`

## 2. .luacheckrc Project Configuration
- [ ] 2.1 Create a default `.luacheckrc` template file in `vscode-uzengine/templates/.luacheckrc` that declares UzEngine sandbox globals: `ctx`, `vec3`, `quat`, `color`, `EventBus`
- [ ] 2.2 Add `std = "lua54"` and `max_line_length = 120` baseline settings to the template `.luacheckrc`
- [ ] 2.3 Add forbidden standard library function rules: flag `os.execute`, `os.exit`, `io.open`, `io.popen`, `debug.getinfo`, `load`, `dofile`, `loadfile` as errors (not warnings)
- [ ] 2.4 Add a rule flagging direct `require()` calls that do not go through `ctx.module:require()` as a warning with message "Use ctx.module:require() instead of raw require()"
- [ ] 2.5 Add a rule flagging direct `coroutine.*` API usage outside `ctx.async.*` wrappers as a warning
- [ ] 2.6 Add a rule detecting missing `return` in node lifecycle callbacks (`on_start`, `on_update`, `on_destroy`) that have a declared return type annotation
- [ ] 2.7 Implement `copyLuacheckrc(workspacePath: string)` in `vscode-uzengine/src/luacheckProvider.ts` that copies the template `.luacheckrc` to the project root if one does not already exist

## 3. CI Lint Step
- [ ] 3.1 Add a `uzengine-lint-scripts` CMake custom target in `CMakeLists.txt` that invokes luacheck over `${PROJECT_SOURCE_DIR}/scripts/**/*.lua` and exits non-zero on any warning or error
- [ ] 3.2 Ensure the lint target is added to the `ALL` build group in CI mode (detected via `UZENGINE_CI` CMake variable) so it runs during `cmake --build`
- [ ] 3.3 Document the lint target invocation: `cmake --build build --target uzengine-lint-scripts`

## 4. Testing
- [ ] 4.1 Create `vscode-uzengine/src/__tests__/luacheck.test.ts` with Jest test suite `LuacheckDiagnosticsTests`
- [ ] 4.2 Write test `ParsesErrorOutput`: feed a known luacheck stdout string with one error and one warning; assert the returned `LuacheckDiagnostic[]` array has length 2 with correct `line`, `column`, `code`, and `severity` fields
- [ ] 4.3 Write test `ForbiddenOsExecuteFlagged`: run `runLuacheck` on a temp Lua file containing `os.execute("ls")`; assert the result contains a diagnostic with severity `error`
- [ ] 4.4 Write test `ForbiddenIoOpenFlagged`: run `runLuacheck` on a temp Lua file containing `io.open("file", "r")`; assert the result contains an error diagnostic
- [ ] 4.5 Write test `UndefinedGlobalFlagged`: run `runLuacheck` on a temp Lua file accessing `undefined_global_xyz`; assert an error diagnostic is produced
- [ ] 4.6 Write test `SandboxGlobalsNotFlagged`: run `runLuacheck` on a Lua file that uses `ctx.scene:spawn()` and `vec3(1,0,0)`; assert zero diagnostics are produced
- [ ] 4.7 Write test `LuarcheckrcCopiedIfMissing`: call `copyLuacheckrc` on a temp directory without `.luacheckrc`; assert the file is created and contains `ctx` in the globals list
- [ ] 4.8 Write test `LuarcheckrcNotOverwrittenIfPresent`: call `copyLuacheckrc` twice; assert the file content from the first call is preserved on the second call
- [ ] 4.9 Write test `EmptyLuaFileProducesNoDiagnostics`: run `runLuacheck` on an empty `.lua` file; assert zero diagnostics
- [ ] 4.10 Verify Jest line coverage >= 95% and branch coverage >= 95% for `luacheck.ts` and `luacheckProvider.ts`

## 5. Documentation
- [ ] 5.1 Update `docs/specs/lua-scripting.md` with a section describing the luacheck integration, the `.luacheckrc` format, and the list of forbidden functions
- [ ] 5.2 Update `docs/specs/crash-reporting.md` with a note that forbidden sandbox calls are caught by luacheck at edit time before they can cause runtime crashes
