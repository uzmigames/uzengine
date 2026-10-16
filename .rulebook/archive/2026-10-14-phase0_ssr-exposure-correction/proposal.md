# SSR: no exposure ratio on a linear scene colour

## Why

The SSR trace multiplies the previous frame's colour by `currentExposure / m_ssrPrevExposure`
(`frame_gi_passes.cpp`, `ssr_trace_cs.hlsl`; the AMD FidelityFX SSSR pattern). FidelityFX assumes a
pre-exposed scene colour, which carries the exposure it was rendered at. This engine's HDR target is linear
and not pre-exposed: on 2026-10-09 the followup-11 study changed `r.Exposure` by 0.5x and 2x and the TAA
output did not move beyond process variation. So the ratio is 1 at a steady exposure and wrong while
auto-exposure adapts: each SSR hit is scaled by that frame's exposure change, a few per cent per frame at
the default adaptation speed (3 EV/s up, 1 EV/s down). Found by reading the code during the followup-11
close-out; not measured. SSR is off by default (`r.SSR` 0).

## What Changes

- The trace receives a ratio of 1 (or the constant and `m_ssrPrevExposure` go), so a hit reads the previous
  frame's linear colour as it is.
- Before the change, a measurement shows the effect: SSR on, auto-exposure adapting after an exposure step
  (a camera move from a dark to a bright view, or `r.AutoExposure.Bias` changed mid-run), the SSR layer's
  energy against the scene colour it reflects, per frame.

## Gate (registered before the change is built)

With SSR on and the exposure adapting, the ratio of the SSR hit colour to the previous frame's scene colour
at the hit stays within 1 +- 0.5 % on every frame of the adaptation; before the change it follows the
per-frame exposure ratio (negative control). At a steady exposure the frames are bit-identical to the build
before. Defaults (`r.SSR` 0) unchanged.

## Impact
- Affected specs: ssr-exposure (new)
- Affected code: runtime/engine/renderer/frame/frame_gi_passes.cpp, frame_postprocess.cpp,
  runtime/shaders/runtime/postprocess/ssr_trace_cs.hlsl
- Breaking change: NO (SSR off by default; with SSR on, only frames where the exposure changes)
- User benefit: SSR reflections that do not pulse while auto-exposure adapts.
