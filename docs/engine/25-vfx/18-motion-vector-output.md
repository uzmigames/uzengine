# Particle motion-vector output

GPU particles emit per-pixel screen-space velocity into the scene velocity
buffer (MRT slot 1) so TAA, DLSS and FSR can reproject them without ghosting.

Reference: UE5 `NiagaraSpriteVertexFactory` motion-vector output, Karis 2014
"High Quality Temporal Supersampling" § per-object velocity.

## Output contract

| Slot | Format      | Content                                                        |
|------|-------------|----------------------------------------------------------------|
| RT0  | RGBA16F HDR | Particle color, blend-mode-aware (alpha / additive / premul).  |
| RT1  | RG16F       | `currentUV − prevUV` in [0,1] UV space, Y-flipped (DirectX).   |

The velocity RT is bound **blend-disabled**. Combined with the back-to-front
sort order of alpha-blended billboards, the foreground particle wins the
motion-vector write at each pixel, which matches what the viewer actually
sees through the stack of transparent sprites.

## Pipeline

1. `particle_billboard_vs.hlsl` computes `prevPositionCS = prevViewProj *
   prevWorldPos` and passes it via `TEXCOORD5`.
2. `particle_billboard_ps.hlsl` calls `ComputeMotionVector(input.positionCS,
   input.prevPositionCS)` from `vfx_motion_vectors.hlsli` and writes the
   result to `SV_Target1`.
3. `PipelineLibrary::GetParticleBillboard*()` pipelines declare
   `renderTargetCount = 2` with `RT[1] = RG16Float, blendEnable = false`.
4. `ParticleRenderPassDesc::velocityTarget` / `velocityTexture` route the
   render graph's velocity resource (imported from `m_gbuffer.velocity`)
   into MRT slot 1.
5. `AddParticleRenderPass` declares a Read + Write on the velocity resource
   so the graph inserts the `ShaderResource → RenderTarget → ShaderResource`
   barrier around the particle draw.

## Edge cases

- **First frame / identity prevViewProj.** `ComputeMotionVector` returns
  `(0, 0)` when `|prevCS.w| < 1e-6`, so TAA sees "stationary" and
  accumulates history correctly without NaN leakage.
- **Dead particles.** The VS writes `prevPositionCS = float4(0, 0, 0, 1)`
  for the early-exit path; those vertices produce degenerate triangles and
  do not reach the PS.
- **Additive blending.** Additive particles still write a valid motion
  vector: blend is disabled on RT1 so the additive RT0 blend state does not
  affect velocity.

## Scope — not covered by this task

Mesh and trail / ribbon particles currently keep their single-RT pipelines.
Emitting correct motion vectors for them requires a second GPU buffer holding
**previous-frame** instance transforms (mesh) or vertex positions (trail),
which is a separate follow-up. Until that lands, mesh and trail pixels fall
back to the opaque velocity written by the GBuffer pass, which is acceptable
because:

- Mesh particles tend to be additive (sparks, embers) — the underlying
  opaque motion is what the viewer perceives.
- Trails are thin and premultiplied; TAA reprojection of the background
  looks correct beneath them.

Filing a future task is appropriate when mesh or trail particles grow to
cover large screen regions.

## Tests

`runtime/tests/engine/vfx/test_motion_vector_math.cpp` mirrors
`ComputeMotionVector` in C++ and covers:

- degenerate `prevCS.w` → zero output (no NaN / Inf),
- stationary particle → zero motion,
- pure rightward / downward motion → correct sign per DirectX V-down UV,
- independent perspective divide on current vs previous w,
- typical per-frame magnitude fits RG16F range.
