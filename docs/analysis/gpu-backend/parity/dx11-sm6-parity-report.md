<!-- doc-meta -->
<!-- analysis-type: backend parity -->
<!-- focus: DX11 + DX12 SM6.0 vs DX12 SM6.6 reference -->
<!-- date: 2026-03-26 -->

# Backend Parity Report — DX11 & SM6.0 vs DX12 SM6.6

## Executive Summary

**29 visual differences** identified between the reference DX12 SM6.6 (bindless)
and the DX11/SM6.0 fallback paths. Issues range from CRITICAL (missing passes,
wrong samplers) to minor (performance-only).

---

## SM6.0 (DX12 Non-Bindless) Issues — 14 Found

### CRITICAL (Broken/Missing)
| # | Issue | File | Impact |
|---|-------|------|--------|
| S1 | **SSR absent** — no register binding, bindless-only | deferred_lighting_ps.hlsl | No screen-space reflections |
| S2 | **SSGI absent** — no register binding, bindless-only | deferred_lighting_ps.hlsl | No indirect GI bounce |
| S3 | **Opacity texture never bound** — null SRV = alpha 0 | gbuffer_ps.hlsl / gbuffer_pass.cpp | Masked materials fully transparent |
| S4 | **GBuffer metallic sampler wrong** — `SampleLevel(clampSampler, 0)` vs `SampleBias(linearSampler, mipBias)` | gbuffer_ps.hlsl:134-136 | Metallic aliased at distance, clamp artifacts |
| S5 | **Lens flare absent** — no t-register | tonemap_ps.hlsl | No lens flare in final image |

### HIGH (Visible Difference)
| # | Issue | File | Impact |
|---|-------|------|--------|
| S6 | IBL null-guard missing — SM6.0 always samples t5-t7 | deferred_lighting_ps.hlsl | Black ambient on first frame or IBL not ready |
| S7 | Normal map applied unconditionally — SM6.6 gates on index>0 | gbuffer_ps.hlsl | Flat normals if no normal texture bound |
| S8 | Sky cubemap lacks multi-scattering — PS path uses transmittance LUT only | atmosphere_renderer.cpp | Sky slightly blue-shifted vs SM6.6 |
| S9 | IBL baker UAV→SRV barrier may be missed | ibl_baker.cpp:489 | Stale/black IBL on some frames |

### MODERATE
| # | Issue | File | Impact |
|---|-------|------|--------|
| S10 | HiZ occlusion culling disabled — frustum-only | gpu_cull_cs.hlsl | Extra draw calls, potential Z-fighting |
| S11 | Decal per-material textures broken — only albedo bound | decal_apply_cs.hlsl | Decal normal/roughness missing |
| S12 | Color grading LUT path may differ | tonemap_ps.hlsl | Minor tone differences |

### LOW (Performance Only)
| # | Issue | File | Impact |
|---|-------|------|--------|
| S13 | GTAO wave intrinsics skipped | gtao_cs.hlsl:386 | Redundant per-lane sincos |
| S14 | Sky cubemap uses 16-step ray march vs LUT | atmosphere_renderer.cpp | Slower but same quality |

---

## DX11 Issues — 15 Found

### CRITICAL (Entire Pass Missing)
| # | Issue | File | Impact |
|---|-------|------|--------|
| D1 | **TAA disabled** — `m_device->IsBindless()` required | frame_setup.cpp:220 | No temporal AA, aliased edges |
| D2 | **Bloom disabled** — compute bloom requires bindless | bloom_pass.cpp:375 | No glow/bloom effects |
| D3 | **SSGI disabled** — bindless-only | frame_gi_passes.cpp:38 | No indirect diffuse |
| D4 | **SDF GI / Radiance Cache disabled** | frame_gi_passes.cpp:82 | Primary GI system absent |
| D5 | **Color grading 3D LUT fails** — DX11 rejects 3D textures | dx11_resource_creation.cpp | Wrong color grading |
| D6 | **SSR absent** (same as SM6.0 S1) | deferred_lighting_ps.hlsl | No reflections |
| D7 | **SSGI absent** (same as SM6.0 S2) | deferred_lighting_ps.hlsl | No indirect GI in shader |

### HIGH (Sampler/Binding Bugs)
| # | Issue | File | Impact |
|---|-------|------|--------|
| D8 | **Samplers s2/s3 missing** — DX11 only creates s0+s1 | dx11_device.cpp | Wrong addressing for LUT/IBL lookups |
| D9 | **SRV table multi-bind broken** — only slot 0 bound | dx11_command_list.cpp | Multi-texture passes get wrong textures |
| D10 | **Opacity texture never bound** (same as S3) | gbuffer_ps.hlsl | Masked materials broken |
| D11 | **Metallic sampler wrong** (same as S4) | gbuffer_ps.hlsl | Metallic aliasing |

### MODERATE
| # | Issue | File | Impact |
|---|-------|------|--------|
| D12 | FSR/FSR2 disabled | fsr_pass.cpp, fsr2_upscaler.cpp | No upscaling |
| D13 | Lens flare disabled | lens_flare_pass.cpp:242 | No lens flare |
| D14 | Async compute absent | dx11_device.h | Performance only |
| D15 | GPU virtual addresses return 0 | dx11_device.h | CBV binding via VA broken |

---

## Priority-Ordered Fix Plan

### Phase 1: Shader Parity (SM6.0 + DX11) — HIGHEST IMPACT
1. Add `register(t12)` + `register(t13)` for SSR and SSGI in deferred_lighting_ps.hlsl
2. Bind SSR/SSGI textures via SetFragmentTexture in lighting_pass.cpp
3. Fix GBuffer metallic sampler: use `SampleBias(linearSampler, uv, mipBias)` in SM5 path
4. Add opacity texture binding (t6) in gbuffer_pass.cpp SM5 path
5. Gate normal mapping on texture validity in SM5 gbuffer path
6. Add null-guard for IBL textures in SM5 deferred lighting path

### Phase 2: DX11 Missing Passes
7. Enable TAA for DX11 (remove IsBindless() gate, use PS-based TAA fallback)
8. Enable bloom for DX11 (PS-based bloom fallback or remove bindless requirement)
9. Create s2 (bilinear clamp) and s3 (bilinear wrap) samplers in DX11 device
10. Fix DX11 SRV table multi-bind in dx11_command_list.cpp
11. Add DX11 3D texture support (for color grading LUT)

### Phase 3: Polish
12. Lens flare SM5 path
13. IBL baker barrier fix for SM6.0
14. Sky cubemap multi-scattering for SM6.0 PS path
15. HiZ culling SM6.0 fallback (single-mip)
