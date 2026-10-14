# Layered reflections: a live SSR, blended before the BRDF over the SDF / IBL layers

## Why

The parecer's block-B item "SSR em camadas": reflections composed as SSR → SDF probe reflections →
IBL, replacing the additive post-TAA composite of the SSR (no BRDF weight, no Fresnel, added on top
of the IBL specular) and reducing the footprint of the un-denoised SDF reflections.

Measured before any change (2026-10-07, build 8aeb8917): the screen-space reflections produce
nothing at all. `r.SSR 1` and `r.SSR 0` render bit-identical frames in `lighting_reflection_test`
(a chrome floor under spheres) and the pass-inspector dump of `ssr.trace` is zero in every pixel,
in that scene and in Sponza. A diagnostic build that made the trace write what it samples showed
the Hi-Z pyramid's mips reading as zero for every pixel while the full-resolution depth was right;
the pyramid's own UAV writes and the trace's reads were verified (a constant added in the pyramid
shader came back through the trace). The pyramid is built right after the depth prepass, and the
GBuffer pass clears the depth-stencil before drawing (`gbuffer_pass.cpp`, `ClearDepthStencil`):
what the pyramid reads at that point is not the frame's depth. Built after the GBuffer pass, the
mips carry the depth. A CPU replica of the march on a frame dump confirms the algorithm hits with
correct inputs. With the pyramid fixed the trace hit (29.6 k pixels of the mirror floor) but the
lighting still saw nothing: The render graph orders passes only through the resources they declare (a chain through each
resource's successive accessors, then Kahn's sort with a FIFO queue). `SSR_Temporal` declared only
its velocity read, so it was scheduled right after the velocity pass, before the trace and the resolve:
it read last frame's resolved texture, the spatial denoise (chained on depth, after the resolve) read
the temporal's stale history and wrote zeros into the resolved texture, which the next frame's temporal
read back. With the denoise off the lighting saw the raw resolve; with it on, nothing, from frame 2 on.
Fixed by declaring the depth read the temporal pass already performs (and the prepare-indirect pass's,
which had no edge at all). With the chain in order the trace hit the boxes (the CPU replica placed the hits on them) and the
lighting blended 29.6 k pixels, yet the floor stayed black: every hit colour was near zero. The trace
samples last frame's colour from `m_ssrPrevSceneColor`, filled by `SSR_CopyPrevColor` after TAA; that
pass declared no graph resource either, so it ran first in the frame and copied the TAA history before
the frame touched it, with barriers assuming a state it was not in: the copy carried nothing usable.
Declaring its read of the HDR colour (and the legacy composite's) chains both behind TAA.

The layering itself is already half-wired: with `r.GI.Reflections 1` the deferred lighting lerps
the SSR into the SDF-traced radiance before the BRDF; without it, the IBL path ignores the SSR and
the post-TAA composite adds it. `r.SSR` is off by default and Sponza's project sets `r.SSR 0`.

## What changes

- The Hi-Z pyramid is built after the GBuffer pass (the complete opaque depth). GTAO, SSGI and the
  GPU culling read the same mips.
- The SSR temporal and prepare-indirect passes declare their depth read, so the graph keeps the chain
  trace → resolve → temporal → denoise in order; the previous-colour copy and the legacy composite declare
  their HDR read, so they run after TAA.
- Deferred lighting: the SSR is blended before the BRDF over whichever layer is active (SDF
  reflections or the prefiltered IBL), so Fresnel / DFG / multi-scatter apply to all layers, and over
  the environment-MIS specular on smooth metals (hit × single-scatter Fresnel, by confidence); the
  additive post-TAA composite is retired (`r.SSR.Composite` default 0, kept for the A/B control).
- Evidence on a mirror floor against the planar-reflection identity (the camera mirrored through
  the floor renders what the floor reflects), plus Sponza montages; the march's hit test is reworked
  only if the mirror gate shows it does not cover what is on screen.

## Gates (pre-registered)

R0 the mirrored view maps onto the reflected rays (≥ 99 % within 0.5°); R1 SSR layer: floor pixels
whose reflected hit is on screen match F × reference within a median relative error of 0.15, and
the SSR-off error is ≥ 2 × that; R2 no double count: the legacy composite over-brightens (≥ 1.5 ×)
while the default lies within [0.85, 1.15]; R3 temporal stability on the floor ≤ 3 × SSR-off +
0.002; R4 Sponza with SSR off reproduces the committed frame (direct_light and taa_output
max |diff| ≤ 1e-6; the GTAO's optional Hi-Z input is the one thing that could move it); R5 SSR
passes ≤ 3 ms at 1080p in the reference scene and in Sponza.

## Out of scope

Side finding: the environment-MIS specular is black on smooth metals in every scene without an HDRI
(the chrome sphere of `chrome_test` renders as a black disc; the mirror floor shows nothing of the sky
with SSR off) although the env-map sampler builds its alias table from a sky cubemap with a positive
luminance; its history, cubemap or temporal path is wrong. Own item.

Reflection capture probes (phase2_ssr-stochastic-refactor 4.2), a denoiser for the SDF reflections,
DXR, enabling SSR in Sponza's project (reported with a montage; the user decides), the depth
prepass being discarded by the GBuffer clear (side finding, own item), the duplicate `r.SSR.*`
CVar registrations (block A.8 list).
