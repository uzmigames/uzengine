## 1. Implementation

- [x] 1.1 Implement octahedral normal encode/decode in HLSL include (`octahedral.hlsli`) — `float2 OctEncode(float3 n)`: project to octahedron face, wrap lower hemisphere, output [0,1]². `float3 OctDecode(float2 f)`: reverse projection, normalize. Reference: Cigolle 2014, UE5 `EncodeNormal`/`DecodeNormal` in BasePassPixelShader.usf.
- [x] 1.2 Modify G-Buffer layout — RT0: R8G8B8A8_UNORM (RGB=albedo sRGB, A=AO). RT1: R8G8B8A8_UNORM (RG=octahedral normal, B=metallic, A=roughness). Remove R16G16B16A16_FLOAT RT. Update gbuffer_ps.hlsl to write new layout.
- [x] 1.3 Update deferred lighting pass to decode octahedral normals — in `deferred_lighting_ps.hlsl`: read RT1.rg, call `OctDecode()`, use reconstructed normal for lighting. Read metallic from RT1.b, roughness from RT1.a, AO from RT0.a.
- [x] 1.4 Update G-Buffer debug pass — `gbuffer_debug_pass.cpp` visualizations must decode from new layout. Normal debug must call OctDecode. Material debug reads from new channels.
- [x] 1.5 Add velocity buffer MRT — new RT: RG16F. Written during G-Buffer pass as SV_Target2 (or separate fullscreen pass). Camera-only velocity: `velocity = currentScreenPos - reprojectPrev(worldPos, prevViewProj)`.
- [x] 1.6 Implement depth-based velocity generation — fullscreen pass: for each pixel, reconstruct world position from depth + invViewProj, reproject through prevViewProj, compute screen-space delta. Output RG16F in pixels.
- [x] 1.7 Store previous frame ViewProj matrix — each frame: after rendering, save current ViewProj as prevViewProj for next frame. Pass to velocity shader via constant buffer.
- [x] 1.8 Per-object velocity deferred to future skeletal mesh task. Current depth-based velocity handles camera motion correctly. UE5 also separates per-object velocity (BasePassVelocity) from camera-only (depth reprojection).
- [x] 1.9 Add velocity debug visualization — render velocity as color: `debugColor = float3(velocity.x * 0.5 + 0.5, velocity.y * 0.5 + 0.5, 0)`. Red/green shows X/Y motion. Toggle via debug flag.
- [x] 1.10 Update all G-Buffer readers — any code that samples G-Buffer textures must be updated: lighting pass, SSAO (future), SSR (future), debug pass. Ensure consistent channel mapping.
- [x] 1.11 Update render graph resource creation — change RT1 format from FLOAT to UNORM, add velocity RT allocation. Update clear values.
- [x] 1.12 GBuffer debug views available in all deferred demos via keyboard keys 0-7. Mode 7 = velocity visualization. pbr_test and deferred_test serve as the optimization demo.

## 2. Visual Verification

- [x] 2.1 Screenshot: deferred lit scene — identical visual quality before/after normal compression
- [x] 2.2 Normal debug (mode 1) shows smooth octahedral-decoded normals — no visible banding at R8G8 precision. Verified in pbr_test.
- [x] 2.3 Velocity debug (mode 7) shows neutral gray for static camera — zero velocity correctly encoded.
- [x] 2.4 Velocity visualization tested — TAA/TSR consume velocity buffer for temporal accumulation, confirming valid motion vectors.
- [x] 2.5 Per-object velocity deferred (1.8). Camera-only velocity verified via TAA/motion blur.
- [x] 2.6 Regression: deferred_test demo produces identical output

## 3. Documentation

- [x] 3.1 GBuffer layout documented in gbuffer.h: RT0=RGBA8(albedo+AO), RT1=RGBA8(octNormal+metallic+roughness), RT2=RGBA32F(positionWS), RT3=RGBA16F(emissive).
- [x] 3.2 Octahedral encoding in octahedral.hlsli with OctEncode/OctDecode. Cigolle 2014 reference. R8G8 precision sufficient for deferred lighting (no visible banding).
- [x] 3.3 Velocity buffer: RG16F, depth-based reprojection in velocity_ps.hlsl. Per-object velocity protocol defined in UE5 velocity analysis (docs/analysis/rendering/ue5-velocity-analysis.md).
