# Proposal: phase0_falcor-gi-shadows-fixes

## Why
Falcor analysis + deep shader audit found quality gaps in GI, shadows, and atmosphere:
1. **SSGI uses linear screen-space march** — misses thin geometry. Hi-Z hierarchical traversal is standard (McGuire & Mara 2014).
2. **SSGI temporal uses AABB clamp** — causes ghosting. Variance-guided clipping (Schied 2017 SVGF) is superior.
3. **PCF shadow kernel is static** — produces visible banding. Per-pixel Poisson disk rotation eliminates this.
4. **Cloud raymarch has no detail noise** — clouds look plastic. Schneider & Vos 2015 uses detail noise erosion.
5. **Cloud output has no temporal reprojection** — half-res noise/shimmer visible. Standard technique reduces noise.
6. **God rays use single 64-sample radial blur** — incorrect light shaft shape. 3-pass progressive is correct.
7. **No standalone SVGF denoiser** — Falcor's SVGF (Schied 2017) is self-contained (no SDK dependency), 5-pass pipeline. Would significantly improve SSGI quality and is prerequisite for future path tracing.

## What Changes
- SSGI trace: Hi-Z hierarchical traversal replacing linear march
- SSGI temporal: variance-guided clipping (μ ± γ·σ)
- SSGI spatial: add luminance variance edge-stopping weight
- PCF: per-pixel Poisson disk rotation via Interleaved Gradient Noise
- PCSS: add area light source size for physically-based penumbra
- Shadow: add ESM (exponential shadow map) sampling mode
- Contact shadows: Hi-Z acceleration for ray march
- Cloud raymarch: implement detail noise erosion
- Cloud temporal: implement temporal reprojection with motion vectors
- Cloud composite: add aerial perspective on cloud color
- God rays: 3-pass progressive blur (16 samples × 3 passes)
- New SVGF denoiser: 5-pass pipeline (reproject → filter moments → 4× à-trous)

## Impact
- Affected specs: `docs/specs/rendering/gi-system.md`, `docs/specs/rendering/shadow-maps.md`, `docs/specs/rendering/atmosphere-lut-architecture.md`
- Affected code: `ssgi/` (4 shaders), `include/pcss.hlsli`, `include/contact_shadows.hlsli`, `include/shadow_common.hlsli`, `atmosphere/` (3 shaders), + 3 new SVGF shaders, `SVGFPass` C++ class
- Breaking change: NO (quality improvements — existing behavior preserved as defaults)
- User benefit: Cleaner indirect lighting, sharper contact shadows, better shadow edge quality, detailed cloud edges, stable cloud rendering, proper god ray shape
