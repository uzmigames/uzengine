# Data Flow: Bruneton 2017 3D Scattering LUT Precomputation

## Reference
@ref Bruneton & Neyret 2008/2017 "Precomputed Atmospheric Scattering"

## Precomputation Pipeline (runs once, recomputes when atmosphere params change)

```
TransmittanceLUT (256x64, exists) ──┐
                                    ├─► SingleScatter3D (256x128x32) ─┐
                                    │                                  │
                                    │   ┌──────────────────────────────┘
                                    │   │
                                    │   ▼  (repeat 4 orders)
                                    ├─► ScatteringDensity3D (temp) ─► MultipleScatter3D (accumulate)
                                    │                                       │
                                    │   IndirectIrradiance2D ◄──────────────┘
                                    │        (64x16)
                                    │
                                    ▼
                              TotalScatter3D = SingleScatter + ΣMultipleScatter
```

## 4D → 3D Texture Packing (Bruneton 2017)

Parameters: (r, mu, mu_s, nu) where:
- r = distance from planet center (altitude)
- mu = cos(view zenith angle)
- mu_s = cos(sun zenith angle)
- nu = cos(angle between view and sun) = dot(viewDir, sunDir)

3D texture: 256 x 128 x 32 (width x height x depth)
- width  = nu_size(8) × mu_s_size(32) = 256
- height = mu_size(128)
- depth  = r_size(32)

## GPU Resources

| Resource | Dims | Format | Size | Lifetime |
|----------|------|--------|------|----------|
| Scattering 3D | 256x128x32 | RGBA16F | ~4MB | Persistent |
| DeltaRayleigh 3D | 256x128x32 | RGBA16F | ~4MB | Temp (precompute) |
| DeltaMie 3D | 256x128x32 | RGBA16F | ~4MB | Temp (precompute) |
| ScatteringDensity 3D | 256x128x32 | RGBA16F | ~4MB | Temp (precompute) |
| Irradiance 2D | 64x16 | RGBA16F | ~8KB | Persistent |
| DeltaIrradiance 2D | 64x16 | RGBA16F | ~8KB | Temp (precompute) |

## Compute Dispatch Sizes

| Pass | Threads | Groups | Total Texels |
|------|---------|--------|-------------|
| SingleScatter3D | [8,8,1] | [32,16,32] | 256x128x32 |
| ScatteringDensity3D | [8,8,1] | [32,16,32] | 256x128x32 |
| MultipleScatter3D | [8,8,1] | [32,16,32] | 256x128x32 |
| DirectIrradiance2D | [8,8,1] | [8,2,1] | 64x16 |
| IndirectIrradiance2D | [8,8,1] | [8,2,1] | 64x16 |

## Precompute Order (per dirty flag)

1. TransmittanceLUT (exists — 256x64)
2. SingleScatter3D: reads TransmittanceLUT → writes DeltaRayleigh + DeltaMie + Scattering
3. DirectIrradiance: reads TransmittanceLUT → writes DeltaIrradiance
4. For order = 2..4:
   a. ScatteringDensity: reads TransmittanceLUT + DeltaRayleigh + DeltaMie + DeltaIrradiance → writes ScatteringDensity
   b. IndirectIrradiance: reads DeltaRayleigh + DeltaMie + ScatteringDensity → accumulate into Irradiance
   c. MultipleScatter: reads TransmittanceLUT + ScatteringDensity → accumulate into Scattering, write DeltaRayleigh(=DeltaMie for order>2)
5. Final Scattering 3D = sum of all orders (accumulated in-place)

## UzEngine File Mapping

| Stage | File | Status |
|-------|------|--------|
| 4D↔3D mapping | `shaders/runtime/include/atmosphere_lut.hlsli` | NEW |
| SingleScatter CS | `shaders/runtime/atmosphere/single_scattering_lut_cs.hlsl` | NEW |
| ScatteringDensity CS | `shaders/runtime/atmosphere/scattering_density_cs.hlsl` | NEW |
| MultipleScatter CS | `shaders/runtime/atmosphere/multiple_scattering_lut_cs.hlsl` | MODIFY (Bruneton 3D replaces Hillaire 2D) |
| Irradiance CS | `shaders/runtime/atmosphere/irradiance_lut_cs.hlsl` | NEW |
| GPU resources | `engine/renderer/scene/scene_renderer_impl.h` | ADD fields |
| Resource creation | `engine/renderer/scene/scene_renderer_init.cpp` | ADD allocation |
| Precompute dispatch | `engine/renderer/atmosphere/atmosphere_renderer.cpp` | ADD passes |
| Context fields | `engine/renderer/atmosphere/atmosphere_renderer.h` | ADD fields |
| Pipeline getters | `engine/renderer/core/pipeline/pipeline_library.h` | ADD methods |
| Pipeline impl | `engine/renderer/core/pipeline/pipeline_atmosphere.cpp` | ADD methods |
| Sky PS lookup | `shaders/runtime/atmosphere/sky_atmosphere_ps.hlsl` | MODIFY (Phase 3) |

## Changes Required (Phase 2, dependency order)

1. `atmosphere_lut.hlsli` — 4D↔3D coordinate mapping functions
2. `single_scattering_lut_cs.hlsl` — single scatter precompute
3. `scattering_density_cs.hlsl` — per-order density
4. `irradiance_lut_cs.hlsl` — direct + indirect irradiance
5. `scene_renderer_impl.h` — add texture handles
6. `scene_renderer_init.cpp` — allocate 3D textures
7. `atmosphere_renderer.h` — add context fields
8. `pipeline_library.h/.cpp` — add pipeline getters
9. `atmosphere_renderer.cpp` — orchestrate precompute passes
