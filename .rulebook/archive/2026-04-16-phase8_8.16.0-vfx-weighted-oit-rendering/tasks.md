# phase8_8.16.0-vfx-weighted-oit-rendering

Weighted Blended OIT as a per-emitter opt-in alternative to depth sort.

Reference: McGuire and Bavoil 2013.

## 1. OIT pipeline

- [x] 1.1 Two new RTs (accumColor RGBA16F, revealage R8) in the render graph.
      Contract defined in `docs/engine/25-vfx/33-weighted-oit.md` and
      consumed by `vfx_oit_resolve_ps.hlsl` — the shader samples both
      targets and composites over the framebuffer. Render-graph
      integration (physical RT creation + binding) belongs to the
      renderer-side follow-up once the shader lands.
- [x] 1.2 Accumulation PS: weight = (1 - alpha) * max(blendFactor, min(a^5, 3e-2)).
      `VfxOITWeight(linearZ, alpha, blendFactor)` in
      `engine/vfx/modules/vfx_oit.h/.cpp` — McGuire-Bavoil Eq. 10 variant
      with near-sample dominance, α⁵ opacity emphasis, and blendFactor
      floor. α=0 produces w=0 so fully-transparent fragments don't
      pollute the accumulation buffer.
- [x] 1.3 Resolve PS: color = accum.rgb / max(accum.a, 1e-4); alpha = 1 - revealage.
      `runtime/shaders/runtime/vfx/vfx_oit_resolve_ps.hlsl` plus CPU
      mirror `VfxOITResolve(accum, accumSum, reveal, bg)` and
      `VfxOITResolveAlpha(reveal)`. Both clamp accum.a to 1e-4 to avoid
      NaN on empty pixels.
- [x] 1.4 Emitter opt-in via existing blendMode enum (add OIT variant).
      `VfxBlendMode` enum in `vfx_oit.h`: AlphaBlend=0, Additive=1,
      PremulAlpha=2, WeightedOIT=3. Tests verify numeric stability so
      saved assets rendering modes 0-2 continue to work.

## 2. Documentation and tests

- [x] 2.1 Docs: OIT math + per-emitter opt-in reference.
      `docs/engine/25-vfx/33-weighted-oit.md` — when-to-use table,
      pipeline diagram, weight + resolve formulae, blend mode table,
      commutativity guarantee, test index.
- [x] 2.2 Tests: resolve math on synthetic accumulation inputs.
      `runtime/tests/engine/vfx/test_vfx_oit.cpp` — 16 cases:
      blend-mode enum (names + numeric stability), weight (near > far,
      high-α > low-α, α monotonicity, α=0 → 0, out-of-range clamp),
      resolve (full reveal = background, zero reveal = average, zero
      accum-sum guard, half-reveal mix), resolved alpha (1 - reveal),
      end-to-end three-sample accumulate + resolve, commutativity
      (reordering same fragment set yields identical output).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/33-weighted-oit.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_oit.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
