<!-- doc-meta -->
title: Risks and Open Questions
date: 2026-04-07
status: draft
<!-- end-doc-meta -->

# 7. Risks & Open Questions

## Risks

### R1 — TAA interaction (high)
Path tracer has its own temporal accumulation. If `r.TAA=1` stays enabled,
two accumulators stack and produce drifting ghost trails (same symptom as
the 2026-04-01 `TAA jitter trembling bug` in MEMORY.md).
**Mitigation**: `PathTracerPass::OnEnable` forces `r.TAA=0` and restores the
previous value on disable.

### R2 — DLSS/FSR/TSR interaction (high)
Upscalers expect sub-pixel jitter + motion vectors. Path tracer produces
neither in a form they understand. Running PT + upscaler = visual garbage.
**Mitigation**: when `r.PathTracer.Enable=1`, force `r.Upscaler=off`. Add a
warning log if user toggles an upscaler while PT is active.

### R3 — Auto-exposure feedback loop (medium)
Path-traced HDR is much noisier than rasterized output. Auto-exposure
readback averages over the whole image; per-frame noise can make exposure
oscillate. MEMORY.md already tracks an "auto-exposure readback stuck at 1.0"
bug in the rasterized path.
**Mitigation**: feed auto-exposure from the *accumulated* history texture,
not the single-frame trace output. Log the current exposure via `--diag`.

### R4 — CB ring stale buffer bug (medium)
MEMORY.md tracks a known issue: CB ring is triple-buffered but frame N data
can be overwritten before the GPU finishes reading it. Path tracer dispatches
compute reading from `FrameConstants` and will hit the same race.
**Mitigation**: either wait for the ring fix (already tracked as its own
issue) or allocate path-tracer CB outside the ring in a dedicated 4-frame
buffer until then.

### R5 — Firefly samples (medium)
Multi-bounce paths through tight specular lobes create fireflies (single
bright pixels that wreck accumulation means).
**Mitigation**: per-sample luminance clamp via `r.PathTracer.ClampFirefly`
(default 10.0). Introduces a tiny bias but standard in production PTs
(`@ref Jensen 2001`).

### R6 — Bindless descriptor heap pressure (low)
Accumulation ping-pong + trace target + scene color UAV + ~4 SRVs per frame
= ~8 bindless slots. Current heap has plenty of room but audit before
enabling.

### R7 — Build time regression (low)
Compute shader with 8 bounces + 200 lines of math compiles slow under DXC
in debug. Compile once at startup; avoid shader permutations early. Add
permutations (MaxBounces as a `-D` define) only after Step 8 ships.

## Open Questions

### Q1 — Scene authoring format for analytic primitives
Step 7 introduces a `PathTracerSceneComponent`. Open: do we reuse the
existing `SphereComponent`/`QuadComponent` (if any) or invent
`PtPrimitiveComponent`? Inventing is cleaner (no accidental interaction
with the rasterized draw path) but duplicates the math.
**Proposal**: dedicated `PtPrimitiveComponent`, resolved in 02-architecture.md
when Step 7 is scoped.

### Q2 — How do meshes ever work?
User scoped this as "path tracer, not ray tracing" → analytic scene only.
Long-term, rendering real triangle meshes requires *some* acceleration
structure. DXR BLAS is the obvious answer but explicitly off-limits here.
**Options**: (a) stay analytic forever, (b) add a simple BVH builder in a
separate milestone, (c) revisit DXR later with user approval.
**Decision needed before Step 7.**

### Q3 — Environment source
Step 8 cubemap source: existing sky atmosphere LUT cubemap? HDRI?
Procedural sky? The shadertoy reference uses a cubemap texture binding.
**Proposal**: reuse the existing `gSkyIrradianceCube` bindless slot the
rasterized path writes each frame, multiplied by `EnvIntensity`.

### Q4 — Denoising
Accumulation alone needs thousands of frames to converge. A real-time
preview would benefit from OIDN/OptiX denoiser or the existing SVGF pass.
**Scope decision**: denoising is OUT of this plan. Add as a follow-up
milestone after Step 8 ships.

### Q5 — Motion vectors for future reprojection
If we ever want real-time PT (<4 frames to convergence), we need SVGF-style
reprojection which needs motion vectors per analytic primitive. Not in
scope now — flag for future design doc.

### Q6 — Where does the demo live?
`demos/path_tracer/` is the proposal. Confirm the demo naming convention
matches existing demos (`demos/pbr_showcase/`, etc.) and that
`scripts/setup.ps1` doesn't need new deps. No new third-party libs required
— pure HLSL + existing RHI.

---

## Sign-off checklist

Before this plan is promoted from `status: draft` to `status: approved`:

- [ ] Q2 resolved (analytic-only vs future BVH)
- [ ] Q3 resolved (environment source)
- [ ] R1/R2 auto-disable logic reviewed by the renderer owner
- [ ] `.rulebook/tasks/phase*/path-tracer/` task file created with the 8 steps as a checklist
- [ ] Demo scene `path_tracer_showcase.uzproject` committed

---

End of plan. See README.md for navigation.
