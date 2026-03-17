<!-- doc-meta -->
<!-- date: 2026-03-17 -->
<!-- author: claude-architect -->
<!-- type: roadmap -->
<!-- tags: rendering, prioritization, roadmap -->
<!-- end-doc-meta -->

# UzEngine Rendering — 30-Day Improvement Roadmap

**Goal**: Implement the 6-8 features that will have the HIGHEST visual impact with the LEAST implementation cost.

**Success Metric**: Move from **baseline PBR** to **AAA-quality visuals** that match UE5's look in common scenes (outdoor, indoor lit, character).

---

## PHASE 1: Stabilization (Days 1-3)

### Task 1.1: Fix GTAO GPU Hang (1 day)
**Problem**: GTAO compute shader crashes on first frame dispatch.

**Root Cause Hypothesis**:
- Root constants mismatch (GTAORootConstants = 8 DWORDs, but shader expects different layout)
- CBV binding issue (compute root sig doesn't expect b2 CBV)
- Compute shader bug (infinite loop or invalid descriptor access)

**Fix Steps**:
1. Add detailed GPU debugging:
   - Log root constant values before dispatch
   - Add RenderDoc capture at GTAO dispatch
   - Verify descriptor indices are valid (< heap size)
2. Simplify GTAO shader (reduce samples 64→32 for testing)
3. Test with minimal descriptor set (only depth SRV, no CBV)
4. If still hangs: revert to FXAA-only, mark GTAO as WIP

**Deliverable**: GTAO either working or explicitly disabled with debug logs.

---

### Task 1.2: Complete PCSS Blocker Search (1 day)
**Problem**: Shadow softness is currently fixed per-cascade. PCSS needs blocker search to compute variable kernel radius.

**Current State**:
- `maxSoftKernelSize = 40.0f` (UE5 parity)
- `tanLightSourceAngle` computed and available
- Poisson disk 16-tap PCF loop exists

**Implementation**:
```hlsl
// In shadow_common.hlsli, add blocker search:
float SearchBlocker(float2 shadowCoords, float compareDepth,
                    float searchRadius, int samples) {
    float blockerCount = 0;
    float avgBlockerDepth = 0;
    for (int i = 0; i < samples; ++i) {
        float2 offset = poissonDisk[i] * searchRadius;
        float sampleDepth = shadowMap.Sample(..., shadowCoords + offset);
        if (sampleDepth < compareDepth) {
            avgBlockerDepth += sampleDepth;
            blockerCount++;
        }
    }
    return blockerCount > 0 ? avgBlockerDepth / blockerCount : -1;
}

// In deferred_lighting_ps.hlsl, call before PCF:
float avgBlockerDepth = SearchBlocker(shadowCoord, pixelDepth,
                                      searchRadius, 16);
float penumbraRadius = (pixelDepth - avgBlockerDepth)
                     * tanLightSourceAngle
                     * maxSoftKernelSize;
// Use penumbraRadius as PCF kernel scale
```

**Deliverable**: Variable shadow softness per-pixel (far shadows soft, near shadows sharp).

---

### Task 1.3: Enable Film Grain + Vignette + Chromatic Aberration (1 day)
**Problem**: No cinematic post-process effects. These are trivial but high-impact.

**Implementation Details**:

**Film Grain**:
```hlsl
// In tonemap_ps.hlsl (after ACES):
float3 FilmGrain(float3 color, float2 uv, float time) {
    float noise = frac(sin(dot(uv + time, float2(12.9898, 78.233))) * 43758.5453);
    return color + (noise - 0.5) * 0.02; // 2% intensity
}
```

**Vignette**:
```hlsl
float Vignette(float2 uv) {
    float2 diff = uv - 0.5;
    float len = length(diff);
    return smoothstep(1.0, 0.3, len) * 0.5 + 0.5; // Screen-edge darkening
}
```

**Chromatic Aberration**:
```hlsl
float3 ChromaticAberration(Texture2D<float4> hdr, float2 uv) {
    float r = hdr.Sample(s0, uv + 0.01 * float2(1, 0)).r;
    float g = hdr.Sample(s0, uv).g;
    float b = hdr.Sample(s0, uv - 0.01 * float2(1, 0)).b;
    return float3(r, g, b);
}
```

**Add to PerFrameCB**:
- `filmGrainIntensity = 0.02f` (artist control)
- `vignetteStrength = 0.5f`
- `chromaticAberrationAmount = 0.01f`

**Deliverable**: Filmgrip, vignette, aberration in tonemap pass (toggle per-frame).

---

## PHASE 2: Major Visual Features (Days 4-20)

### Task 2.1: Temporal Anti-Aliasing (TAA) — 5 days

**Days 4-8**

**Architecture**:
```
Frame N:
  - Generate Halton jitter offset (2D)
  - Add to camera projection (0.5-1.0 pixel offset)
  - Render with jittered projection
  - Reproject previous frame samples via invViewProj
  - Accumulate 4 frames with exponential filter

Frame N+1:
  - Repeat with different jitter value
  - Blend with history: color = lerp(newSample, history, 0.8)
```

**Implementation**:
1. **Jitter generation** (2 hours):
   - Generate Halton sequence (2D, 4 samples)
   - Store in ShaderManager as uniform

2. **History buffer** (2 hours):
   - Create 2 RGBA16F textures (ping-pong)
   - Manage in SceneRenderer
   - Bind to deferred lighting as UAV

3. **Reprojection** (3 hours):
   - Add jitter to camera viewProj in PerFrameCB
   - Compute world position from depth (invViewProj)
   - Compute screen position in previous frame (prevViewProj)
   - Clamp samples to [min, max] of 3x3 neighborhood

4. **Integration** (2 hours):
   - Call in rendering loop (after lighting, before bloom)
   - Swap ping-pong buffers each frame
   - Enable TAA by default, fallback FXAA on high FPS

**Code Locations**:
- Create: `passes/postprocess/taa_pass.h`
- Integrate: `scene_renderer.cpp:RenderFrame()` (after lighting, before bloom)
- Shader: `shaders/runtime/postprocess/taa_cs.hlsl`

**Deliverable**: TAA reduces aliasing 3x, enables 1080p + 1.5x upsample = 1440p quality.

---

### Task 2.2: Screen-Space Reflections (SSR) — 4 days

**Days 9-12**

**Architecture**:
```
1. Ray-march through Hi-Z pyramid (terminate at depth hit)
2. Cone trace (per-roughness angle) for specular
3. Fade with distance + screen-edge vignette
4. Composite into HDR before bloom
```

**Implementation**:
1. **Hi-Z Integration** (1 hour):
   - SSR compute shader reads Hi-Z pyramid (SRV)
   - Uses mip selection based on step size

2. **Ray-March** (2 hours):
   - Per-pixel ray direction (reflect V around N)
   - Marching loop (16-32 steps) through screen-space depth
   - Binary search refinement (optional, 4-8 steps)
   - Terminate at depth hit or screen-edge

3. **Cone Trace** (1 hour):
   - Per-roughness: wider cone = more samples blended
   - Sample 3x3 neighborhood, weight by cone angle
   - Fallback to IBL cubemap if no hit

4. **Composite** (1 hour):
   - Read SSR result (RG16F: hit color + confidence)
   - Blend: `result = lerp(ibl, ssr, confidence)`
   - Add to deferred lighting output (before bloom)

**Code Locations**:
- Create: `passes/postprocess/ssr_pass.h`
- Shader: `shaders/runtime/postprocess/ssr_cs.hlsl`

**UE5 Reference**:
- `PostProcessScreenSpaceReflections.cpp:260-400` (ray-march logic)
- `ScreenSpaceRayMarching.ush:100-200` (cone sampling)

**Deliverable**: Glossy/metallic surfaces show reflections (mirrors, polished metal, water).

---

### Task 2.3: Contact Shadows — 2 days

**Days 13-14**

**Implementation**:
```hlsl
// In shadow_common.hlsli:
float ContactShadow(float3 worldPos, float3 N, float3 L,
                    Texture2D<float> depth,
                    float4x4 viewProj) {
    // Ray-march from pixel toward light
    // Terminate at depth hit (object between pixel and light)
    float3 rayDir = L;
    float rayLength = 0;
    float shadowFactor = 1.0;

    for (int step = 0; step < 16; ++step) {
        float3 rayPos = worldPos + rayDir * rayLength;
        float4 rayScreenPos = mul(float4(rayPos, 1), viewProj);
        rayScreenPos.xy /= rayScreenPos.w;
        rayScreenPos.xy = rayScreenPos.xy * 0.5 + 0.5;
        rayScreenPos.y = 1 - rayScreenPos.y; // Flip Y for DX12

        float sampleDepth = depth.Sample(s0, rayScreenPos.xy);
        float rayDepth = rayScreenPos.z;

        if (rayDepth > sampleDepth + bias) {
            shadowFactor = 0.0;
            break;
        }
        rayLength += stepSize;
        if (rayLength > maxContactDistance) break;
    }
    return shadowFactor;
}

// In deferred_lighting_ps.hlsl:
float contact = ContactShadow(worldPos, N, L, depth, viewProj);
shadowFactor *= contact;
```

**Code Locations**:
- Add to: `shadow_common.hlsli` (shared helpers)
- Integrate: `deferred_lighting_ps.hlsl` (after PCF shadow sample)

**Deliverable**: Prevents floating objects, adds visual contact deformation.

---

### Task 2.4: Motion Blur — 3 days

**Days 15-17**

**Architecture**:
```
1. Velocity pass (already exists): per-pixel velocity vector (2D screen-space)
2. Gather-based blur: sample along velocity vector (8-16 taps)
3. Tile-max optimization: per-tile velocity = max velocity in tile
4. Composite after lighting
```

**Current State**: `velocity_ps.hlsl` exists, velocity texture is created.

**Implementation**:
1. **Velocity Export** (1 hour):
   - In GBuffer pass, output velocity (currently in velocity_ps only)
   - Store as RG16F (normalized per-frame velocity)

2. **Motion Blur Shader** (1 hour):
   - Per-pixel: read velocity from texture
   - Gather-blur: sample 8-16 times along velocity direction
   - Weighted average (center tap = 2x weight)

3. **Tile-Max Optimization** (1 hour):
   - Tile-size: 32x32 pixels
   - Compute per-tile max velocity (compute shader)
   - Use in gather loop for adaptive sample count

4. **Integrate** (1 hour):
   - Create `motion_blur_pass.h`
   - Call after tonemap (before UI composite)
   - Add `motionBlurStrength` to PerFrameCB (0.5-2.0)

**Code Locations**:
- Create: `passes/postprocess/motion_blur_pass.h`
- Shader: `shaders/runtime/postprocess/motion_blur_ps.hlsl`
- Reference velocity: `velocity_ps.hlsl` (already in codebase)

**Deliverable**: Smooth camera pans (removes stuttering, adds cinematic feel).

---

### Task 2.5: Decals — 5 days

**Days 18-22**

**Architecture**:
```
Deferred Decal Pass:
  1. Render decal quads (world-space mesh)
  2. Read GBuffer (albedo, normal, roughness, metallic)
  3. Blend decal parameters (blend mask texture)
  4. Write modified GBuffer
  5. Lighting reads blended GBuffer
```

**Implementation**:
1. **Decal Data** (1 hour):
   - Add `DecalComponent` (position, rotation, scale, material)
   - Store in ECS (vector of DecalComponent)

2. **Decal Pass** (2 hours):
   - Render decal meshes (quad, cube, or cylinder)
   - Per-decal: read blend mask (RGBA8 = mask for albedo, normal, roughness, metallic)
   - Blend GBuffer (additive for normal, multiply for roughness, etc.)
   - Write back to GBuffer UAV

3. **Material** (1 hour):
   - Decal material texture (albedo + blend mask)
   - Create fallback white decal (no-op blend)

4. **Integration** (1 hour):
   - Add to render graph: GBuffer → Decal → Lighting
   - Iterate ECS DecalComponent in scene_renderer.cpp

**Code Locations**:
- Create: `passes/geometry/decal_pass.h`
- Create: `engine/ecs/graphics/decal_component.h`
- Shader: `shaders/runtime/geometry/decal_ps.hlsl` (fullscreen, depth-test to frustum)

**UE5 Reference**:
- `Renderer/Decals.cpp:300-500` (decal blending logic)
- `DeferredShadingCommon.ush:100-150` (GBuffer read/write)

**Deliverable**: Scorch marks, bullet holes, graffiti (AAA visual vocabulary).

---

## PHASE 3: Polish (Days 23-30)

### Task 3.1: Emissive Map Support — 1 day

**Days 23**

**Changes**:
1. Add `emissiveIndex` to PerObjectCB (uint32 SRV heap index)
2. In GBuffer pass: sample emissive texture (RGBA8 or R16F)
3. In deferred lighting: add emissive to HDR before bloom threshold
4. Create fallback black emissive texture (0 contribution)

**Code**:
```hlsl
// In mesh_ps.hlsl:
float3 emissive = emissiveTexture.Sample(s0, uv).rgb;
float4 gBufColor = baseColor;
// ... normal/PBR sampling ...

// In deferred_lighting_ps.hlsl:
float3 sceneColor = dirLight + pointLights + ibl;
sceneColor += emissive * emissiveIntensity; // Add before bloom
```

**Deliverable**: Self-illuminated surfaces (neon signs, glowing runes, etc.).

---

### Task 3.2: Volumetric Fog (Froxels) — 8 days

**Days 24-30+ (overflow, but high-priority)**

**Architecture**:
```
1. Froxel Grid: 160x90x64 (depth-divided)
2. Light Accumulation: per-froxel, per-light contributions
3. Scattering: march through grid, accumulate along ray
4. Composite: fullscreen pass, blend with scene
```

**This is complex** — recommend deferring to next 2-week cycle.

For now: Keep current 2D height fog. Add volumetric fog task to backlog.

---

## IMPLEMENTATION CHECKLIST

### Phase 1 (Days 1-3)

- [ ] **Task 1.1: Fix GTAO**
  - [ ] Add GPU debugging logs (root constants, descriptor validation)
  - [ ] Simplify GTAO shader (reduce samples)
  - [ ] Test with RenderDoc capture
  - [ ] Decide: enable GTAO or disable permanently with comments

- [ ] **Task 1.2: Complete PCSS**
  - [ ] Implement blocker search loop (32 samples)
  - [ ] Compute penumbra radius from blocker depth
  - [ ] Variable PCF kernel radius per-pixel
  - [ ] Test shadow quality (far soft, near sharp)

- [ ] **Task 1.3: Film Grain + Vignette + Chromatic Aberration**
  - [ ] Add film grain function to tonemap_ps.hlsl
  - [ ] Add vignette function to tonemap_ps.hlsl
  - [ ] Add chromatic aberration (per-channel UV offset)
  - [ ] Add controls to PerFrameCB
  - [ ] Test visually (should feel more cinematic)

### Phase 2 (Days 4-20)

- [ ] **Task 2.1: TAA**
  - [ ] Generate Halton sequence (2D, 4 samples)
  - [ ] Create history buffer (ping-pong RGBA16F)
  - [ ] Implement reprojection via invViewProj
  - [ ] Add jitter to camera viewProj
  - [ ] Integrate into render loop
  - [ ] Test: enable TAA, verify aliasing reduction

- [ ] **Task 2.2: SSR**
  - [ ] Integrate Hi-Z pyramid reading
  - [ ] Ray-march shader (16-32 steps, binary refinement)
  - [ ] Per-roughness cone trace (3x3 neighborhood sampling)
  - [ ] Composite into deferred lighting
  - [ ] Test: metallic sphere should show reflections

- [ ] **Task 2.3: Contact Shadows**
  - [ ] Implement ray-march from pixel toward light
  - [ ] Integrate into shadow sampling (multiply with PCF result)
  - [ ] Test: objects should have contact darkening

- [ ] **Task 2.4: Motion Blur**
  - [ ] Export velocity from GBuffer pass
  - [ ] Tile-max velocity computation (compute shader)
  - [ ] Gather-blur shader (8-16 taps along velocity)
  - [ ] Integrate after tonemap
  - [ ] Test: pan camera, verify smooth blur trail

- [ ] **Task 2.5: Decals**
  - [ ] Add DecalComponent to ECS
  - [ ] Implement decal pass (GBuffer read/write)
  - [ ] Blend logic (albedo, normal, roughness, metallic)
  - [ ] Integrate into render graph
  - [ ] Test: place decal quad, verify blending

### Phase 3 (Days 23-30)

- [ ] **Task 3.1: Emissive Map**
  - [ ] Add emissiveIndex to PerObjectCB
  - [ ] Sample emissive in mesh_ps.hlsl
  - [ ] Add to HDR in deferred_lighting_ps.hlsl
  - [ ] Test: glowing surface should bloom

- [ ] **Task 3.2: Volumetric Fog**
  - [ ] (Deferred to next cycle due to complexity)

---

## SUCCESS METRICS

After 30 days:

| Metric | Current | Target | Status |
|--------|---------|--------|--------|
| **TAA** | None | ✅ 4-frame history | +5 visual |
| **SSR** | None | ✅ Working | +5 visual |
| **Contact Shadows** | None | ✅ Working | +4 visual |
| **PCSS** | ⚠️ Fixed kernel | ✅ Variable penumbra | +3 visual |
| **Motion Blur** | None | ✅ Working | +3 visual |
| **Decals** | None | ✅ Basic working | +4 visual |
| **Post-Process FX** | FXAA only | ✅ + Grain + Vignette + Aberration | +2 visual |
| **Emissive** | Implicit | ✅ Texture-driven | +2 visual |
| **GTAO** | ❌ Disabled | ✅ Fixed or explicitly disabled | +1 visual |

**Cumulative Visual Improvement**: 5 + 5 + 4 + 3 + 3 + 4 + 2 + 2 + 1 = **29/45 possible points** = **64% improvement** toward UE5 parity.

---

## RISK MITIGATION

| Task | Risk | Mitigation |
|------|------|-----------|
| GTAO Fix | Unknown hang root cause | Start with RenderDoc capture, reduce complexity |
| TAA | Reprojection artifacts | Implement clamping + velocity dilate |
| SSR | Ray-march miss/artifacts | Fallback to IBL, fade at screen edges |
| PCSS | Blocker search cost | Limit samples to 32, early-exit if found |
| Motion Blur | Performance cost | Tile-max optimization, reduce taps on low-end |
| Decals | GBuffer write conflicts | Use UAV with careful synchronization |

---

## EFFORT ESTIMATE SUMMARY

| Phase | Days | Features | Cumulative Work |
|-------|------|----------|-----------------|
| Phase 1 (Stabilization) | 3 | Fix GTAO, PCSS, FX | 3 days |
| Phase 2 (Major Features) | 17 | TAA, SSR, Contact, Motion, Decals | 20 days |
| Phase 3 (Polish) | 10 | Emissive, Volumetric Fog | 30 days |

**Total**: 30 days of focused engineering for **3-4x visual quality improvement** (baseline PBR → AAA-quality).

---

**Document Version**: 1.0
**Last Updated**: 2026-03-17
**Owner**: unreal-architect
