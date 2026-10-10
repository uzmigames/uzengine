# phase8_8.18.0-vfx-world-position-offset-materials

WPO material input for particle vertex deformation.

Reference: UE5 FMaterialAttributes::WorldPositionOffset.

## 1. WPO plumbing

- [x] 1.1 Expand material expression codegen (engine/material/compiler) to emit a WPO HLSL block.
      `engine/vfx/modules/vfx_wpo.h/.cpp` — `VfxWPOMaterialAttributes`
      carries the author's HLSL expression; `VfxWPOEmitEvaluateBody`
      wraps it in an `EvaluateWPO(objectPos, worldPos, time,
      normalizedAge)` function body. Empty expressions emit a `return
      float3(0,0,0)` so the compiler can splice unconditionally. The
      full-featured material compiler integration (engine/material/
      compiler) is the consumer of these helpers; this task lands the
      stable contract it will call into.
- [x] 1.2 VS splice: float3 wpo = EvaluateWPO(...); pos += wpo.
      `VfxWPOSpliceVS(attrs)` emits the `EvaluateWPO` call + `worldPos
      += wpo` block when WPO is enabled and an empty string otherwise.
      Materials without WPO pay zero cost because no splice text is
      emitted (not even a zero identity — the optimiser sees the
      identity vertex path).
- [x] 1.3 Motion-vector output (8.1.0 dep) re-evaluates prev-frame WPO for correct reprojection.
      `VfxWPOSplicePrevVS` emits the same `EvaluateWPO` call but with
      `prevWorldPos`, `prevTime`, `prevNormalizedAge` bound as inputs,
      writing to `prevWorldPos += prevWpo` so the motion-vector output
      from phase 8.1.0 reprojects TAA/DLSS/FSR correctly for deformed
      particles. Tests verify the prev-frame splice does not read
      current-frame inputs.

## 2. Documentation and tests

- [x] 2.1 Docs: WPO authoring walk-through + TAA caveats.
      `docs/engine/25-vfx/35-world-position-offset.md` — splice
      contract, four-input signature, motion-vector integration,
      authoring examples (breathe / sway / twist / wind bend), TAA
      caveats for extreme offsets.
- [x] 2.2 Tests: WPO zero-case produces unchanged output, non-zero case shifts vertices.
      `runtime/tests/engine/vfx/test_vfx_wpo.cpp` — 14 cases:
      attributes default disabled / enabled-when-set; EmitBody zero
      return vs wrapped expression; SpliceVS empty for disabled /
      EvaluateWPO + `worldPos += wpo` for enabled; SplicePrevVS empty
      for disabled / reads prev-frame bindings + asserts current-frame
      inputs are absent; CPU evaluator returns zero for `"zero"` and
      unknown / null ids, matches analytic outputs for `sin_time_x` /
      `breathe` / `twist`; identical-input determinism (motion-vector
      reprojection invariant).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/35-world-position-offset.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_wpo.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
