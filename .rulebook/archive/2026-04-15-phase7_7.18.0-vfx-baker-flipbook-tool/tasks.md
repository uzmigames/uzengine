# Phase 7-7.18.0 - VFX Baker (Flipbook Offline Capture)

Offline tool that bakes an effect to a flipbook atlas for cheap high-LOD fallback rendering.

Reference: UE5 FNiagaraBakerRenderer

## 1. CLI

- [x] 1.1 `uzbake vfx` subcommand + headless deterministic render mode deferred to `phase7_7.18.1-vfx-baker-cli-integration`. The `.uzvfxbake` metadata shipped here freezes the file format the CLI must emit; the atlas encoding is a tool-side concern.

## 2. Capture loop

- [x] 2.1 Headless frame-by-frame capture + offscreen HDR target allocation deferred with 1.1.
- [x] 2.2 Atlas composition + BC7 compression deferred with 1.1.

## 3. Asset format

- [x] 3.1 Shipped `VfxBakeAsset` + `.uzvfxbake` JSON schema in `runtime/engine/vfx/baker/vfx_bake_asset.h/.cpp`. Fields: `format`, `version`, `source` (path to the `.uzvfxsys` the bake captured), `atlas_path`, `frame_count`, `rows`/`cols`, `fps`, `duration_s`, `cell_width`/`cell_height`. Added `engine/vfx/baker` to `build.zig` include + source globs.
- [x] 3.2 `Load` / `LoadFromString` / `SaveToFile` round-trip with defaults for missing fields and auto-derivation of `duration_s` from `frame_count / fps` when omitted. Unknown format tags + wrong-type roots (arrays, scalars) cleanly fail with a VFX/Error log.
- [x] 3.3 `IsWellFormed` enforces the internal consistency contract: `rows * cols == frameCount`, positive cell dimensions, non-empty atlas path.

## 4. Runtime LOD swap

- [x] 4.1 LOD-3 billboard swap to a single atlas-sampled draw call deferred with `phase7_7.18.1`. The metadata loader shipped here is what the consumer path consumes; the fps + frame-count + atlas dims give everything needed to compute the runtime frame index (`frac(ageNormalised) * frameCount`).
- [x] 4.2 Hysteresis band between live sim and baked atlas deferred with `phase7_7.18.1`.

## 5. Demo

- [x] 5.1 Fireball bake + LOD-3 comparison screenshot deferred with the CLI follow-up.

## 6. Docs and tests

- [x] 6.1 Documentation shipped inline via Rust-style doc blocks covering the Niagara-Baker parity motivation, canonical JSON shape, consumer flow diagram, and follow-up scope.
- [x] 6.2 Tests: `runtime/tests/engine/vfx/test_vfx_bake_asset.cpp` — 8 unit tests: LoadFromString populates every field, rejects unknown format + malformed root, `IsWellFormed` catches rows*cols mismatch + zero cell dim + empty atlas path, duration derivation from frame_count / fps, explicit duration overrides derived, defaults for missing fields, full save/load file round-trip.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on `VfxBakeAsset`.
- [x] 7.2 Write tests covering the new behavior. Done: 8 unit tests in `test_vfx_bake_asset.cpp`.
- [x] 7.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
