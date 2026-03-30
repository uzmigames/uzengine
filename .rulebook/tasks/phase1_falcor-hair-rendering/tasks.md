## Session 1: Hair Strand Geometry

- [ ] 1.1 Create `hair_strand_asset.h/.cpp` — `HairStrandAsset` class: `Load(path)` reads `.hair` binary format (header: strandCount, pointsPerStrand; data: float3 positions, float widths, float2 UVs). `GetVertexBuffer() → RhiBufferHandle`. `GetStrandCount() → uint`. `GetLODLevel(float distanceCm) → uint` (returns target strand count fraction: 1.0 at <500cm, 0.5 at 1000cm, 0.25 at 2000cm).
- [ ] 1.2 GPU upload: positions as `R32G32B32_FLOAT`, widths as `R32_FLOAT`, UVs as `R32G32_FLOAT`. Topology: `D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ` (adjacency needed for consistent tangent computation in VS).
- [ ] 1.3 Create `hair_vs.hlsl` — expand line strips to camera-facing quads: compute strand tangent from adjacent control points, compute billboard right vector as `normalize(cross(tangent, cameraForward))`, expand each line segment to 4 vertices offset by `±width/2` along right vector. Output: position (NDC), `float3 tangent`, `float2 uv`, `float widthFraction` (0=root, 1=tip).
- [ ] 1.4 Build + verify: hair vertex shader compiles. Bind a test `.hair` file (procedurally generated 100 straight strands) and render as solid color — verify billboard quads face camera and follow strand directions.

## Session 2: Chiang 2016 Hair BSDF

- [ ] 2.1 Create `include/hair_bsdf.hlsli` — declare `HairBSDFSample` struct, all lobe functions. @ref Chiang et al. 2016 "A Practical and Controllable Hair and Fur Model for Production Path Tracing" Table 1
- [ ] 2.2 Implement `Mp(float sinThetaI, float cosThetaI, float sinThetaR, float cosThetaR, float roughness) → float` — modified Gaussian longitudinal scattering. Alpha shifts per lobe: R=-α, TT=α/2, TRT=-3α/2 (α from roughness). @ref Chiang 2016 Eq. 7
- [ ] 2.3 Implement `Np(float phi, int p, float s, float gammaO, float gammaT) → float` — azimuthal scattering via logistic distribution. @ref Chiang 2016 Eq. 8. Implement `Ap(float cosTheta, float eta, float3 absorption) → float3[3]` — 3 attenuation terms using Fresnel for R, TT, TRT. @ref Chiang 2016 Eq. 9
- [ ] 2.4 Implement `EvaluateHairBSDF(float3 wo, float3 wi, float3 tangent, HairMaterial mat) → float3` — sum of all 3 lobes: `sum over p: Mp(p) * Np(p) * Ap(p)`. Decompose `wi` and `wo` into `(thetaI, phi)` in tangent-normal-bitangent frame.
- [ ] 2.5 Create `hair_ps.hlsl` — use `EvaluateHairBSDF` with strand tangent from VS output. Evaluate for each directional light. Alpha from Gaussian opacity profile (wider strands = more opaque). Build + verify: hair renders with correct lobe-based shading (visible highlight shift vs Lambert).

## Session 3: Hair Shadow (Deep Opacity Maps) + GBuffer Integration

- [ ] 3.1 Create `hair_shadow_pass.h/.cpp` — Deep Opacity Map (DOM): 8 depth layer shadow map at 1024×1024. For each hair fragment in shadow map view: write transmittance fraction to the layer whose depth bound contains the fragment's depth. DOM stores `float transmittance[8]` per texel.
- [ ] 3.2 Sample DOM in `hair_ps.hlsl`: for a given shadow ray, look up the 8 DOM layers, interpolate transmittance between bounding layers. Multiply by hair BSDF contribution for self-shadowing.
- [ ] 3.3 GBuffer integration: add `materialType=3` (Hair) as a new dispatch path in `deferred_lighting_ps.hlsl` (guarded by `HAIR_SHADING` define from shader variant system). Store strand tangent in spare GBuffer channel (replace AO if needed, or add GB channel).
- [ ] 3.4 Hair LOD: at > 1000cm camera distance, thin strands < 1px are discarded (coverage < 0.1). Strand count multiplied by `GetLODLevel(dist)` via indirect draw count.
- [ ] 3.5 Demo with a test hair asset (100 curved strands, procedural): build + verify via `--screenshot-stdout`. Verify: correct highlights (R lobe visible as offset specular), correct self-shadowing (DOM), visible LOD transition. Capture multiple camera distances.

## Session 4: Polish + Transparency + Validation

- [ ] 4.1 Per-pixel linked list (OIT) for transparent hair overlap: allocate `RWStructuredBuffer<HairFragment>` with count buffer. Hair PS appends to list. Resolve pass sorts per-pixel list by depth and composites front-to-back. Enable via `HAS_OIT` variant define.
- [ ] 4.2 Root-to-tip color gradient: `float3 strandColor = lerp(rootColor, tipColor, uv.y)` in `hair_ps.hlsl`. `rootColor` and `tipColor` as per-material parameters stored in GBuffer or material CB.
- [ ] 4.3 Build + verify final quality: render hair with OIT enabled vs disabled via `--flip` — OIT should show measurably fewer transparency sorting artifacts (lower FLIP on overlapping strand regions). GPU timestamp confirms OIT pass < 2ms.
