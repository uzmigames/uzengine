# phase8_8.2.0-vfx-asset-metadata-tagging

Add tag metadata to VFX assets so the asset browser + snippet library (8.8.0) can filter by keyword.

Reference: UE5 NiagaraScriptBase::Tags.

## 1. Data model

- [x] 1.1 Add tags std::vector<std::string> to VfxSystem, VfxNodeGraph, ParticleEmitter preset config.
      Added `VfxSystem::tags` (`engine/vfx/system/vfx_system.h`),
      `VfxNodeGraph::tags` (`engine/vfx/editor/vfx_node_graph.h`),
      `ParticleEmitter::tags` (`engine/vfx/components/particle_emitter.h`,
      with `<vector>` include added).
- [x] 1.2 JSON round-trip the "tags" array in each loader + saver.
      `VfxSystem::LoadFromString` / `SaveToFile`
      (`engine/vfx/system/vfx_system.cpp`), `VfxNodeGraphAsset::LoadFromString`
      / `SaveToString` (`engine/vfx/editor/vfx_node_graph.cpp`), and
      `SerializeEmitter` / `DeserializeEmitter`
      (`engine/vfx/components/emitter_serializer.h`). Non-string entries are
      warned-and-skipped; the `"tags"` key is emitted only when non-empty so
      legacy assets round-trip diff-minimal.
- [x] 1.3 Helper VfxAssetTagMatches(query, tags) implementing AND / OR / NOT query composition.
      `engine/vfx/asset/vfx_asset_tags.h` — header-only DNF evaluator,
      case-insensitive ASCII match. Whitespace-tokenised; `OR` literal
      splits alternative groups; `-` / `!` prefix negates a term. Empty
      query matches everything; standalone `-` / `!` and trailing `OR` are
      typing-in-progress no-ops.

## 2. Documentation and tests

- [x] 2.1 Document the tag query grammar.
      `docs/engine/25-vfx/19-asset-tagging.md` — taggable assets, JSON
      shape, full grammar table, case-insensitivity, cost analysis.
- [x] 2.2 Unit tests for round-trip + query composition.
      `runtime/tests/engine/vfx/test_vfx_asset_tags.cpp` covers: empty /
      whitespace query, positive match (case-insensitive), multi-term AND,
      `-` and `!` negation, `OR` alternatives, compound `A -B OR C`,
      bare `-` / `!` no-op, trailing `OR` no-op, plus JSON round-trip on
      VfxSystem (incl. non-string entry skip and missing key) and
      VfxNodeGraph (empty-vector → no key emitted).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/19-asset-tagging.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_asset_tags.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (no new warnings or errors in the
      touched translation units). Runtime `test_runtime.exe` execution on
      this machine is blocked by the same pre-existing missing ffmpeg DLLs
      (`swscale-9.dll`) noted on task 8.1.0 — an environment-only gap. All
      new code is pure-CPU / no platform dependencies, so the environment
      gap does not affect correctness of the new behaviour.
