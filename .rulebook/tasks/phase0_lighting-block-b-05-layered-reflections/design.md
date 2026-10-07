# Design: layered reflections

## Why the SSR was dead
`ssr_trace_cs.hlsl` marches the Hi-Z pyramid (mip 1 coarse, mip 0 fine) and accepts a hit only
where the sampled depth is not sky (`sceneDepth > 0.0001`). The pyramid (`hiz_pyramid_cs.hlsl`,
min of 2 × 2) was dispatched right after the depth prepass; a trace diagnostic that wrote the mip
samples showed them zero in every pixel, with the full-resolution depth correct, and a pyramid that
added a constant proved the UAV writes and the trace's SRV reads work. The GBuffer pass clears the
depth-stencil before drawing, so the prepass depth is not the frame's depth; what the pyramid read at
its position was zero. Dispatched after the GBuffer pass the mips equal the depth. The same mips feed
GTAO (optional input), SSGI and the two-phase GPU culling.

## Why the SSR was still dead with the pyramid fixed
The render graph orders passes only through the resources they declare (a chain through each
resource's successive accessors, then Kahn's sort with a FIFO queue). `SSR_Temporal` declared only
its velocity read, so it was scheduled right after the velocity pass, before the trace and the resolve:
it read last frame's resolved texture, the spatial denoise (chained on depth, after the resolve) read
the temporal's stale history and wrote zeros into the resolved texture, which the next frame's temporal
read back. With the denoise off the lighting saw the raw resolve; with it on, nothing, from frame 2 on.
Fixed by declaring the depth read the temporal pass already performs (and the prepare-indirect pass's,
which had no edge at all).

## Why the floor was still black with the chain in order
With the chain in order the trace hit the boxes (the CPU replica placed the hits on them) and the
lighting blended 29.6 k pixels, yet the floor stayed black: every hit colour was near zero. The trace
samples last frame's colour from `m_ssrPrevSceneColor`, filled by `SSR_CopyPrevColor` after TAA; that
pass declared no graph resource either, so it ran first in the frame and copied the TAA history before
the frame touched it, with barriers assuming a state it was not in: the copy carried nothing usable.
Declaring its read of the HDR colour (and the legacy composite's) chains both behind TAA.

## The environment-MIS path
On smooth metals (metallic > 0.3, roughness < 0.7) the deferred lighting replaces the split-sum specular
with the environment-MIS estimate (`EnvMIS_Temporal`, block A.3), so a blend before the BRDF never
reached those pixels: the chrome floor stayed black with the SSR data right and the lighting's own
`ssr_blended` flag set on 29.6 k pixels. The SSR is now sampled once; the split-sum path blends the raw
hit radiance before the DFG terms, and the MIS path blends the hit times the single-scatter Fresnel term
over the MIS specular, both by confidence.

Side finding: the environment-MIS specular is black on smooth metals in every scene without an HDRI
(the chrome sphere of `chrome_test` renders as a black disc; the mirror floor shows nothing of the sky
with SSR off) although the env-map sampler builds its alias table from a sky cubemap with a positive
luminance; its history, cubemap or temporal path is wrong. Own item.

## Composite
Before: with `r.GI.Reflections 1` the deferred lighting does `radiance = lerp(sdfRadiance,
ssr.rgb, ssr.a)` before the BRDF; otherwise the IBL radiance ignores the SSR and `SSR_Composite`
adds `ssr.rgb × confidence² × intensity` onto the TAA output (no BRDF weight, no Fresnel, added to
the IBL specular already in the pixel). After: the lerp applies to whichever layer is active
(SDF gather or prefiltered cubemap), then the DFG / Fresnel / multi-scatter terms apply to the
blended radiance, so SSR → SDF → IBL is a confidence-weighted cascade (the SDF gather already
falls back to its sky on a miss; the IBL is the layer under it when the SDF is off). The post-TAA
composite stays in the code under `r.SSR.Composite` (default 0) as the control for the
double-count gate. The SSR's own temporal and spatial passes stay before the lighting; TAA sees the
composed specular, as in UE.

## Reference and harness
- `scripts/reflection_reference.py`: a 30 × 30 m chrome floor (metallic 1, roughness 0.02, base
  colour 0.95), three rough boxes and a pillar, sun at 40°, camera at (0, −900, 180) looking at
  (0, 250, 0); a second scene without the floor with the camera mirrored through z = 0. Planar
  reflection identity: the floor's reflection at pixel p is what the mirrored camera sees through the
  same pixel beyond the plane; with the engine's up-fixed camera that image is rotated by 180°
  (`mirror_pixel`), verified per pixel through the dumps' world positions (R0).
- `scripts/test_layered_reflections.py`: captures mirror / SSR off / SSR on / legacy composite /
  SSR on + SDF layer (frames 90-99, frame-wide `components` dump), classifies floor pixels by whether
  the reflected hit is on screen and unoccluded (visible), off screen or occluded, or sky; compares
  the final floor colour with F × reference (Schlick F0 0.95); temporal stability from the 10-frame
  sequences; Sponza SSR off / on at Camera A; R4 against the committed build's capture.

## Rejected / deferred
- Keeping the pyramid after the prepass and making the GBuffer keep the prepass depth: that is the
  depth prepass item (the prepass is currently discarded: a performance finding of its own).
- A denoiser for the SDF reflections, reflection capture probes, DXR: phase2 SSR task 4.x.
- Enabling SSR in Sponza's project here: a look decision for the user, evidence attached.
