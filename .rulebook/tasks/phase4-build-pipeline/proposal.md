# Proposal: phase4-build-pipeline

## Why

There is no shipping build system. Games built with UzEngine currently consist of loose source assets, uncompressed textures, and uncompiled HLSL shaders that cannot be distributed to end users. Without a cook-pack-stage-package pipeline, developers must manually copy files, pre-compile shaders externally, and distribute raw assets — a workflow that is error-prone, slow, and produces builds an order of magnitude larger than necessary. Incremental cooking (only re-processing changed assets), .uzpak asset bundling, DXC offline shader compilation, and NSIS installer generation are all required before a game built with UzEngine can ship.

## What Changes

- **Cook step** — `UzEngine.exe --cook --project=./` processes all source assets: BC7 texture compression via DirectXTex, mesh optimization via meshoptimizer (vertex cache, overdraw, quantize), HLSL → DXIL offline compilation via DXC with `-Od` stripped and `-Zi` PDB optional; outputs written to `build/cooked/`; incremental via content-hash manifest so unchanged assets are skipped
- **Pack step** — `UzEngine.exe --pack --project=./` bundles cooked assets into `.uzpak` archives keyed by logical asset group (level, shared, audio); `.uzpak` is a flat binary format: 4-byte magic `UZPK`, u32 version, u32 entry count, then per-entry `{path_hash u64, offset u64, size u32, flags u16}` table followed by raw compressed data (LZ4 frame); random access by hash lookup
- **Stage step** — assembles the final output directory: `UzEngine_Shipping.exe` (engine binary built without editor code, `UZENGINE_SHIPPING` define), `game.dll` compiled by the user's C++ game project, `.uzpak` files, and a `manifest.json` listing pak hashes for update delta detection
- **Package step** — invokes NSIS via CPack to produce a Windows installer `.exe`; installer embeds the staged directory, creates Start Menu shortcuts, registers the uninstaller in Add/Remove Programs; package metadata (product name, version, icon) read from `.uzproj` `[package]` section
- **VSCode integration** — "Build & Package" command in the extension command palette triggers the full pipeline via IPC; progress reported per-step with asset counts; errors surfaced as VSCode diagnostic messages with the offending asset path
- **Platform abstraction** — cook and pack steps are platform-tagged so the same pipeline can target Linux (Vulkan) and Android (GLES3) when those backends are added; platform selected via `--platform=windows|linux|android` flag

## Impact

- Affected specs: `docs/specs/build-system.md`, `docs/specs/asset-pipeline.md`, `docs/specs/shader-system.md`
- Affected code: `src/tools/cook/`, `src/tools/pack/`, `src/tools/stage/`, `CMakeLists.txt` (CPack config), `vscode-extension/src/buildPipelineController.ts`, `src/ipc/ipc_handlers.cpp`
- Breaking change: NO — development mode (loose assets, JIT shader compile) is unchanged; the build pipeline is an additive export path
- User benefit: A single VSCode command produces a distributable Windows installer containing an optimized, self-contained game with compressed textures, pre-compiled shaders, and no development tooling or source assets exposed to end users.
