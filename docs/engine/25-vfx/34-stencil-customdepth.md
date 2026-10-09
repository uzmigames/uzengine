# VFX stencil + CustomDepth output

Per-emitter stencil-write and CustomDepth rendering so particles can
participate in post-process masking (heat haze only where fire is) and
reveal effects (wallhack, outline, custom DoF).

Reference: UE5 `UNiagaraRendererProperties::bRenderCustomDepth` + stencil
controls.

## Config

```cpp
struct VfxStencilConfig {
    uint8_t             stencilRef          = 0;        // 0..255
    VfxStencilWriteMode writeMode           = Disabled;
    bool                customDepthEnabled  = false;
};
```

### Write modes

| Mode        | Stencil write behaviour                  | PP predicate           |
|-------------|-------------------------------------------|------------------------|
| `Disabled`  | No writes (ignore `stencilRef`).           | Never matches.         |
| `Replace`   | Stencil = `stencilRef` (needs ref > 0).    | `sample == stencilRef` |
| `Increment` | Stencil = clamp(stencil + 1, 255).         | `sample >= stencilRef` |

`Replace` with `stencilRef == 0` is flagged invalid by
`VfxStencilConfig::IsValid()` because the post-clear stencil is already 0
so the write is a no-op and no PP pass would ever match.

## Rendering integration

- **Stencil output.** The billboard PS permutation selected when
  `writeMode != Disabled` emits `SV_StencilRef = stencilRef` (Replace) or
  leaves stencil write-through enabled with the pipeline's
  `StencilOp::Increment` op (Increment). No extra RT — writes land on the
  existing depth-stencil target.
- **CustomDepth.** When `customDepthEnabled` is true, the emitter is
  enrolled in an extra depth-only prepass that targets the engine's
  `CustomDepth` RT. The prepass shader
  (`runtime/shaders/runtime/vfx/vfx_customdepth_ps.hlsl`) samples the
  sprite's alpha and discards pixels below the threshold so the RT
  captures the visible silhouette — not the full quad.

The two outputs are independent — authors can enable either, both, or
neither per emitter.

## Post-process consumption

Post-process passes that want to gate on VFX stencil sample the stencil
RT and compare against the emitter's configured ref:

```cpp
if (VfxStencilMaskMatches(sampledStencil, cfg)) {
    // run heat-haze / distort / bloom-glow / etc.
}
```

CustomDepth passes sample the `CustomDepth` RT directly (same API as the
opaque depth buffer). A common pattern:

```glsl
float cd = customDepthTex.Sample(sampler, uv);
if (cd > 0.0 && cd < opaqueDepth) {
    // Particle is in front of geometry and tagged — apply outline.
}
```

## Authoring examples

- **Heat haze limited to fire particles.** Set `writeMode = Replace`,
  `stencilRef = 4` on the fire emitter. The distortion PP checks
  `sampledStencil == 4` before applying the refraction sample.
- **Count-based fog density.** Set `writeMode = Increment`,
  `stencilRef = 3` on a smoke emitter. The fog PP densifies its output
  wherever the stencil sample is ≥ 3, producing progressively thicker
  fog in regions covered by multiple smoke layers.
- **X-ray outline.** Set `customDepthEnabled = true` on a character-
  attached spark emitter. The outline PP reads CustomDepth and renders
  a highlight wherever the particles drew.

## Tests

`runtime/tests/engine/vfx/test_vfx_stencil.cpp` — 16 cases:

- Default config disabled.
- `IsEnabled` true when write mode set or CustomDepth enabled.
- Validation: Replace + ref 0 rejected; Disabled with any ref allowed;
  Replace + non-zero ref allowed; Increment + ref 0 allowed.
- String round-trip for all three modes + unknown + null rejection.
- `VfxStencilMaskMatches`: Disabled always false; Replace requires exact
  match; Increment uses ≥; zero-ref increment matches every sample.
