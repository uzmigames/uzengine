## 1. Implementation

- [ ] 1.1 Add emissive texture slot — register t5 (or bindless index). Emissive RGB × emissiveIntensity added to final output AFTER lighting (self-illumination). Default = black (0,0,0) = no emission. HDR values > 1.0 contribute to bloom.
- [ ] 1.2 Add emissive parameters to PerObjectCB — `uint32_t emissiveIndex` (SRV heap index, 0=black), `float emissiveIntensity` (1.0 default, can exceed 1.0 for bright glow), `float3 emissiveColor` (tint multiplier, default white).
- [ ] 1.3 Update mesh_ps.hlsl for emissive — after lighting: `output.rgb += emissiveTex.Sample(uv).rgb * emissiveColor * emissiveIntensity`. Emissive is NOT affected by lighting or shadows (self-lit).
- [ ] 1.4 Add opacity texture slot — register t6 (or bindless). R channel = opacity [0,1]. Default = white (1.0 = fully opaque).
- [ ] 1.5 Implement alpha test (masked blend mode) — in mesh_ps.hlsl: `if (opacity < opacityThreshold) discard;`. UE5 default threshold = 0.333. Used for foliage, fences, grates.
- [ ] 1.6 Implement translucent blend mode — new PSO with alpha blending: `SrcBlend=SrcAlpha, DestBlend=InvSrcAlpha`. Render translucent objects after opaque pass, sorted back-to-front. Output.a = opacity.
- [ ] 1.7 Implement additive blend mode — new PSO with additive blending: `SrcBlend=One, DestBlend=One`. Used for particles, holograms, energy effects.
- [ ] 1.8 Add blend mode to PerObjectCB — `uint32_t blendMode` (0=Opaque, 1=Masked, 2=Translucent, 3=Additive). Select PSO per draw call based on blend mode.
- [ ] 1.9 Add two-sided rendering flag — `uint32_t twoSided` in PerObjectCB. When 1, disable backface culling in rasterizer state. Used for foliage, cloth.
- [ ] 1.10 Add displacement/heightmap texture slot — register t7 (or bindless). R channel = height [0,1]. Default = black (0.0 = no displacement). Reserved for POM task (3.13.0).
- [ ] 1.11 Update G-Buffer pass for emissive — write emissive to RT2.a or separate emissive RT for deferred pipeline. Deferred lighting adds emissive after lighting evaluation.
- [ ] 1.12 Create material expansion demo — scene with: emissive neon signs (glow + bloom), masked chain-link fence, translucent colored glass, additive hologram. All material types in one scene.

## 2. Visual Verification

- [ ] 2.1 Screenshot: emissive surface glowing and contributing to bloom
- [ ] 2.2 Screenshot: masked foliage — clean alpha cutout, no fringing
- [ ] 2.3 Screenshot: translucent glass — background visible through tinted surface
- [ ] 2.4 Screenshot: additive hologram — bright overlay blended with scene
- [ ] 2.5 Screenshot: emissive intensity=10 — very bright, large bloom halo
- [ ] 2.6 Regression: existing opaque materials unchanged

## 3. Documentation

- [ ] 3.1 Document all material texture slots and default values with UE5 equivalents
- [ ] 3.2 Add blend mode guide (opaque vs masked vs translucent vs additive)
- [ ] 3.3 Document per-material parameter system and PerObjectCB layout
