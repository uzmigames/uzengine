# Rendering Analysis Documentation

This directory contains deep architectural analyses of rendering systems from UE5 and other engines, with guidance for implementing equivalent systems in UzEngine.

## Documents

### Core Analyses

- **[ue5-indoor-lighting-and-shadows.md](ue5-indoor-lighting-and-shadows.md)**
  - Comprehensive analysis of UE5's interior lighting systems
  - Covers: CSM, VSM, Lumen GI, Sky Light, DFAO, Lightmass
  - Problem space: Why interiors are dark with only sun shadows
  - **Status**: Complete reference, ready for implementation planning

- **[ue5-csm-vsm-indoor-comparison.md](ue5-csm-vsm-indoor-comparison.md)**
  - Direct comparison of Cascaded Shadow Maps vs Virtual Shadow Maps
  - Strengths/weaknesses for interior vs outdoor
  - Implementation recommendation for UzEngine
  - **Status**: Decision support for shadow architecture

### Implementation Guides

- **[indoor-lighting-implementation-guide.md](indoor-lighting-implementation-guide.md)**
  - Step-by-step roadmap for implementing interior lighting in UzEngine
  - Phase 1 (Foundation): Sky Light + DFAO (2-3 weeks)
  - Phase 2 (Real-Time GI): Lumen radiosity (4-6 weeks)
  - Phase 3 (Baked GI): Lightmass integration (2-3 weeks)
  - **Status**: Ready for development, includes code specifications

### Sponza Parity Evidence

Ten reports ported verbatim from `test/windows-sponza-parity @ 25cd0c5f` (each carries an origin header; their `scripts/*_artifacts` paths are evidence not kept in Git). Engine-side descriptions: `docs/engine/12-rendering/31..33-*.md`, `docs/engine/09-application-lifecycle/07-scene-readiness-and-preload.md`.

- **[sponza-lighting-rootcause.md](sponza-lighting-rootcause.md)** — root cause of the dark/flat Sponza (Euler order, π sky, SSR filter, shadow bias, specular) — **Status**: Evidence record
- **[sponza-gi-correction-2026-09-06.md](sponza-gi-correction-2026-09-06.md)** — SDF GI: empty shader stubs, mesh SDF bake, global distance field, radiance cache — **Status**: Evidence record
- **[sponza-sdf-orientation-evidence.md](sponza-sdf-orientation-evidence.md)** — SDF sign/orientation and two-sided bake evidence — **Status**: Evidence record
- **[sponza-sun-rotation-evidence.md](sponza-sun-rotation-evidence.md)** — quaternion Euler composition and sun direction — **Status**: Evidence record
- **[sponza-graph-material-evidence.md](sponza-graph-material-evidence.md)** — graph material draw metadata (blend mode, two-sided, specular, ORM) — **Status**: Evidence record
- **[sponza-surface-lighting-2026-09-07.md](sponza-surface-lighting-2026-09-07.md)** — filtered final gather and surface lighting — **Status**: Evidence record
- **[sponza-local-reflections-2026-09-07.md](sponza-local-reflections-2026-09-07.md)** — SDF reflections, SSR lerp and reflection selection — **Status**: Evidence record
- **[sponza-ddgi-cbuffer-layout-2026-09-07.md](sponza-ddgi-cbuffer-layout-2026-09-07.md)** — `RadianceCacheTraceConstants` layout (176 bytes there, 192 here with the VFX emissive tail) — **Status**: Evidence record
- **[sponza-loading-exposure-2026-09-07.md](sponza-loading-exposure-2026-09-07.md)** — incomplete-load presentation and opening exposure: readiness counters, no-present preload, exposure history reset — **Status**: Evidence record
- **[sponza-test-handoff.md](sponza-test-handoff.md)** — SponzaTest's own limits: no Lumen parity, no Surface Cache, Falcor PSNR unverified, DXR unvalidated on Tier 1.0, 23 pre-existing `test_runtime` errors — **Status**: Handoff record

---

## Key Findings

### Interior Lighting is Layered, Not Special-Cased

UE5 does **NOT** have an "interior mode". Instead:

1. **Direct Lighting** (Sun): CSM/VSM renders sun shadows
   - Interior surfaces correctly receive shadow=1 (fully shadowed from sun)
   
2. **Ambient Fill** (Sky Light): Irradiance SH coefficients
   - Provides baseline illumination from visible sky
   - Cheap: 9 float3 values per scene
   
3. **Occlusion Detail** (DFAO): Distance field ray-marching
   - Darkens contact shadows and corners
   - Spatial variation from direct sun
   
4. **Color Bounces** (Lumen): Real-time radiosity probes
   - Interior walls bounce light from sunlit exteriors
   - Adds color variation and perceptual correctness
   
5. **Post-FX**: SSAO, SSR, tone mapping
   - High-frequency detail and refinement

**Result**: Interior automatically looks correct when all layers are enabled.

### Virtual Shadow Maps Are Worth It (Eventually)

- **CSM**: Good for current generation, proven
- **VSM**: Better for mixed indoor-outdoor, requires 6-8 weeks dev
- **Recommendation**: Improve CSM now, plan VSM migration for Phase 2

### Lightmass Portals Are Underrated

For **static interiors**, Lightmass + portals provides:
- Correct multi-bounce light transport (offline computed)
- Zero runtime cost (baked)
- High quality (can wait for convergence)

Development cost: 2-3 weeks after SkyLight/Lumen exist.

---

## Parameter Reference

### Sky Light (Interior Tuning)

| Parameter | Indoor Value | Outdoor Value |
|-----------|--------------|---------------|
| `intensity` | 1.0-1.5 | 0.5-0.8 |
| `min_occlusion` | 0.15-0.3 | 0.05-0.1 |
| `occlusion_exponent` | 0.80-0.95 | 1.0-1.2 |
| `contrast` | 0.2-0.5 | 0.5-0.8 |

### Lumen (Interior Tuning)

| Parameter | Indoor Value | Outdoor Value |
|-----------|--------------|---------------|
| `probe_spacing_cm` | 48-64 | 96-128 |
| `max_trace_distance_cm` | 300-500 | 1000-2000 |
| `hemisphere_rays_per_probe` | 32-48 | 16-32 |

---

## File Organization

```
docs/analysis/rendering/
├── README.md  (this file)
├── ue5-indoor-lighting-and-shadows.md
├── ue5-csm-vsm-indoor-comparison.md
└── indoor-lighting-implementation-guide.md
```

## Related Specs

- `docs/specs/rendering/renderer.md` — Overall rendering pipeline
- `docs/specs/rendering/shadow-maps.md` — Shadow system (if exists)
- `docs/specs/lighting/sky-atmosphere.md` — Sky rendering (if exists)

## How to Use These Documents

### For Architecture Decisions
→ Read **ue5-csm-vsm-indoor-comparison.md** to decide between CSM and VSM

### For Implementation Planning
→ Read **indoor-lighting-implementation-guide.md** for phase breakdowns and code structure

### For Deep Understanding
→ Read **ue5-indoor-lighting-and-shadows.md** for complete system description with file references

---

## Integration Checklist

- [ ] **Phase 1**: Implement SkyLight + DFAO (foundation)
  - [ ] SkyLightComponent (ECS)
  - [ ] SH evaluation functions
  - [ ] Cubemap capture → SH convolution
  - [ ] Integration with deferred lighting
  
- [ ] **Phase 2**: Implement Lumen (real-time GI)
  - [ ] Mesh cards (GPU-resident virtual lightmaps)
  - [ ] Software ray-tracing (compute shader)
  - [ ] Screen-space probe gather
  - [ ] Temporal accumulation & feedback
  
- [ ] **Phase 3**: Implement Lightmass (baked GI, optional)
  - [ ] Portal system for offline baking
  - [ ] Light probe irradiance volumes
  - [ ] Integration with SkyLight (fallback for Lumen)

---

## Cross-References to UE5 Source

All analyses include specific file paths from `W:\UE_5.6_Source`:

| System | Key Files |
|--------|-----------|
| **CSM** | `Engine/Source/Runtime/Renderer/Private/ShadowRendering.h` |
| **VSM** | `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.h` |
| **SkyLight** | `Engine/Classes/Components/SkyLightComponent.h` |
| **Lumen** | `Engine/Source/Runtime/Renderer/Private/Lumen/Lumen.h` |
| **DFAO** | `Engine/Source/Runtime/Renderer/Private/DistanceFieldLightingShared.h` |
| **Lightmass** | `Engine/Classes/Lightmass/LightmassPortal.h` |

When questions arise, consult the source directly.

---

## Status

- **ue5-indoor-lighting-and-shadows.md**: ✅ Complete, reference-ready
- **ue5-csm-vsm-indoor-comparison.md**: ✅ Complete, decision-ready
- **indoor-lighting-implementation-guide.md**: ✅ Complete, actionable
- **Code samples**: ✅ Included (pseudocode, compilable patterns)

**Next Step**: Begin Phase 1 implementation (SkyLight + DFAO) with these documents as specification.

---

Generated: 2026-04-06  
Updated: [date of next analysis]  
Author: Claude (UE5 Rendering Architecture Analysis)
