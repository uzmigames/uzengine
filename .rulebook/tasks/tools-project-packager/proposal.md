# Proposal: tools-project-packager

## Why

Shipping a game build is one of the most error-prone steps in game production. The cook-stage-package pipeline involves multiple sequential tools, each with its own flags and failure modes. A shader compilation error during cook silently produces a black screen at runtime. A missing content pack causes assets to 404 in release. A wrong platform target bundles debug symbols into the shipping binary. Junior developers and external QA testers are blocked entirely if the process requires memorizing CLI incantations.

The `tools-project-packager` tool provides a VSCode WebView panel ("Project Packager") that exposes the full build pipeline as a GUI. A developer configures target platform, active content packs, DLC groups, build configuration (debug/development/shipping), and output directory from dropdowns and checkboxes. Clicking "Build" triggers the multi-step pipeline (validate → cook → compile shaders → pack → stage → package) and streams each step's stdout/stderr into a collapsible log section in the panel. Individual steps can be re-run without repeating the full pipeline. A progress bar tracks overall completion.

This tool is strategically important beyond its immediate UX benefit: it makes the build pipeline testable and auditable. Because all parameters are captured as a JSON build profile (`game.build.json`), builds become reproducible and commit-able. A CI system can consume the same profile file and produce bit-identical outputs to what the developer tested locally — eliminating the "works on my machine" class of shipping bugs.

## What Changes

- `ProjectPackager` VSCode WebView panel (`vscode-extension/src/panels/ProjectPackagerPanel.ts`): React-based UI with platform selector, content pack checkboxes, build profile JSON editor (advanced mode), progress bar, per-step log accordion
- `game.build.json` schema: versioned JSON format capturing all packaging parameters (platform, packs, dlc, configuration, output_dir, shader_compile_flags)
- Build profile loader/saver: `BuildProfileManager` reads/writes `game.build.json` from workspace root
- IPC handler `tools.build_project(profile)` triggers the full pipeline in engine process, emits `tools.build_progress` events per step with stdout lines
- Step-level re-run support: IPC handler `tools.build_step(step_name, profile)` runs one stage in isolation (cook | shader_compile | pack | stage | package)
- Error surfacing: cook errors are parsed and linked to source asset paths; shader errors link to .hlsl file + line; packaging errors include Windows error codes + remediation hints
- VSCode notification on completion: "Build succeeded — output at {path}" (click to open in Explorer) or "Build failed at {step}" (click to show log)
- `uzengine --build-from-profile game.build.json` CLI mode for CI integration without opening VSCode
- Unit tests: mock IPC tests for all build steps; integration test with a minimal project produces a valid output directory structure

## Impact

- Affected specs: `docs/specs/build-system.md` (add packager UI and build profile spec)
- Affected code: `vscode-extension/src/panels/ProjectPackagerPanel.ts` (new), `vscode-extension/src/commands/buildProject.ts` (new), `engine/src/ipc/handlers/build_handlers.cpp` (new), `tools/packager/build_runner.cpp` (new)
- Breaking change: NO (wraps existing CLI pipeline in a GUI layer)
- User benefit: Makes game shipping accessible to non-CLI-proficient developers; standardizes build configuration in a commit-able file; enables reproducible CI builds identical to local developer builds
