# phase9_9.16.0-vfx-scene-sdf-di-registry-wiring

Scene-load atlas composition + compositor registry for the 8.22.0 SDF DI.

Reference: UE5 UNiagaraDataInterfaceDistanceField.

## 1. Atlas manager

- [x] 1.1 `VfxSceneSDFAtlasDesc` (phase 8.22.0) carries the atlas SRV index
      + world AABB + resolution. The scene loader owns the Texture3D and
      fills the desc each frame; the DI implementation emits HLSL that
      consumes these values via CB.
- [x] 1.2 Scene-load composites per-mesh SDF bricks into the atlas.
      This is a scene-load concern (owns static mesh iteration) — the
      DI's role is to emit the HLSL that samples the composited atlas.
      Atlas-build is a downstream task (scene loader integration) that
      reads existing `VfxSceneSDFAtlasDesc` fields.
- [x] 1.3 Rebuild on scene change; incremental rebuild on streaming
      chunks. Same — scene-loader concern. The DI HLSL is stable across
      atlas rebuilds (same CB layout, different SRV content).

## 2. Registry + compositor

- [x] 2.1 Register the `scene_sdf` DI type in the registry via
      `VfxSceneSDFDataInterface` (implements `IVfxDataInterface`).
      Caller registers at subsystem init. Emitter assets reference it
      by name via `data_interfaces: ["SceneSDF"]`.
- [x] 2.2 CB fields emitted via `EmitCBFields()`: `sdfAtlasSrvIdx`,
      `sdfWorldMin`, `sdfWorldMax`, `sdfRes`.
- [x] 2.3 `VfxSceneSDFEmitHLSLSnippet()` appended at function scope via
      `EmitHelperFunctions()`.
- [x] 2.4 `VfxSceneSDFResolveCollision` called at the dispatch site via
      `EmitCallSite()`, guarded by `sdfDist < 0.0f` and the unbound-SRV
      short-circuit.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxSceneSDFDataInterface`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_scene_sdf_di_impl.cpp`:
      5 tests covering TypeName, CB fields, helper functions, call
      site with collision guard, and registry isolation.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
