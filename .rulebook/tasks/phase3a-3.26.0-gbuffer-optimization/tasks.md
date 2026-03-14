## 1. Implementation

- [x] 1.1 Implement octahedral normal encode/decode in HLSL include (`octahedral.hlsli`) — `float2 OctEncode(float3 n)`: project to octahedron face, wrap lower hemisphere, output [0,1]². `float3 OctDecode(float2 f)`: reverse projection, normalize. Reference: Cigolle 2014, UE5 `EncodeNormal`/`DecodeNormal` in BasePassPixelShader.usf.
- [x] 1.2 Modify G-Buffer layout — RT0: R8G8B8A8_UNORM (RGB=albedo sRGB, A=AO). RT1: R8G8B8A8_UNORM (RG=octahedral normal, B=metallic, A=roughness). Remove R16G16B16A16_FLOAT RT. Update gbuffer_ps.hlsl to write new layout.
- [x] 1.3 Update deferred lighting pass to decode octahedral normals — in `deferred_lighting_ps.hlsl`: read RT1.rg, call `OctDecode()`, use reconstructed normal for lighting. Read metallic from RT1.b, roughness from RT1.a, AO from RT0.a.
- [x] 1.4 Update G-Buffer debug pass — `gbuffer_debug_pass.cpp` visualizations must decode from new layout. Normal debug must call OctDecode. Material debug reads from new channels.
- [x] 1.5 Add velocity buffer MRT — new RT: RG16F. Written during G-Buffer pass as SV_Target2 (or separate fullscreen pass). Camera-only velocity: `velocity = currentScreenPos - reprojectPrev(worldPos, prevViewProj)`.
- [x] 1.6 Implement depth-based velocity generation — fullscreen pass: for each pixel, reconstruct world position from depth + invViewProj, reproject through prevViewProj, compute screen-space delta. Output RG16F in pixels.
- [x] 1.7 Store previous frame ViewProj matrix — each frame: after rendering, save current ViewProj as prevViewProj for next frame. Pass to velocity shader via constant buffer.
- [ ] 1.8 Implement per-object velocity (optional, for dynamic objects) — vertex shader: transform vertex by both current and previous model matrix + ViewProj. Write delta as per-vertex velocity. Overrides camera-only velocity for that pixel.
- [x] 1.9 Add velocity debug visualization — render velocity as color: `debugColor = float3(velocity.x * 0.5 + 0.5, velocity.y * 0.5 + 0.5, 0)`. Red/green shows X/Y motion. Toggle via debug flag.
- [x] 1.10 Update all G-Buffer readers — any code that samples G-Buffer textures must be updated: lighting pass, SSAO (future), SSR (future), debug pass. Ensure consistent channel mapping.
- [x] 1.11 Update render graph resource creation — change RT1 format from FLOAT to UNORM, add velocity RT allocation. Update clear values.
- [ ] 1.12 Create G-Buffer optimization demo — deferred scene with debug views: albedo, normals (decoded), metallic, roughness, AO, velocity. Camera orbit showing velocity field.

## 2. Visual Verification

- [x] 2.1 Screenshot: deferred lit scene — identical visual quality before/after normal compression
- [ ] 2.2 Screenshot: normal debug view — smooth normals with no banding from octahedral encoding
- [ ] 2.3 Screenshot: velocity debug view (static camera) — all black/neutral (zero velocity)
- [ ] 2.4 Screenshot: velocity debug view (camera rotating) — radial color pattern
- [ ] 2.5 Screenshot: velocity debug view (moving object) — object shows velocity, background static
- [x] 2.6 Regression: deferred_test demo produces identical output

## 3. Documentation

- [ ] 3.1 Document new G-Buffer layout with channel mapping table
- [ ] 3.2 Add octahedral encoding algorithm reference and quality analysis
- [ ] 3.3 Document velocity buffer format, generation methods, and per-object velocity protocol
