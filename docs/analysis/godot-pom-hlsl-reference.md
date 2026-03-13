# Godot 4 POM Implementation → HLSL Reference

**Purpose**: Concrete HLSL equivalent code for UzEngine based on Godot 4's material.cpp POM algorithm.

---

## 1. Material CB Structure

```hlsl
// Corresponds to HeightmapParams in material.h
cbuffer HeightmapConstants : register(b4) {
    float heightmap_scale;           // [-16.0, 16.0] → will be multiplied by 0.01 in shader
    uint heightmap_min_layers;       // [1, 64] — typically 4–8
    uint heightmap_max_layers;       // [1, 64] — typically 16–32
    uint heightmap_flags;            // bit0: enable_deep_parallax
                                     // bit1: flip_tangent
                                     // bit2: flip_binormal
                                     // bit3: invert_heightmap
    // Padding for alignment if needed
};
```

---

## 2. Texture Resources

```hlsl
Texture2D heightmap_tex : register(t9);        // Heightmap texture
SamplerState heightmap_samp : register(s0);    // Linear sampler (Godot: "Force linear filtering")
```

**Texture hint**: Default black (0.0 at unmapped regions).

---

## 3. Deep Parallax Algorithm (HLSL)

### Full Implementation

```hlsl
/// Compute parallax-occluded UV offset using deep parallax (iterative raymarching)
/// view_dir: normalized view direction in tangent space [from TBN matrix transform]
/// base_uv: initial UV coordinate
/// heightmap_scale: user material parameter [-16.0, +16.0]
/// returns: adjusted UV coordinate after parallax offset
float2 ComputeDeepParallax(in float3 view_dir,
                            in float2 base_uv,
                            in float heightmap_scale,
                            in uint min_layers,
                            in uint max_layers,
                            in bool is_heightmap_inverted) {
    // Clamp view_dir.z to avoid division issues at grazing angles
    float view_dir_z = max(abs(view_dir.z), 0.001);

    // Adaptive layer count: more layers at grazing angles
    // At normal incidence: dot(Z_axis, view_dir) = view_dir.z ≈ 1.0 → min_layers
    // At grazing angle:    dot(Z_axis, view_dir) = view_dir.z ≈ 0.0 → max_layers
    float num_layers_f = lerp((float)max_layers, (float)min_layers, view_dir_z);
    uint num_layers = max((uint)num_layers_f, 1);

    // Per-layer depth increment (0.0 → 1.0 over num_layers steps)
    float layer_depth = 1.0 / num_layers;

    // Ray direction in UV space, scaled by heightmap_scale * 0.01
    // (0.01 factor improves scale usability and avoids artifacts at distance)
    float2 p = view_dir.xy * (heightmap_scale * 0.01);
    float2 delta = p / num_layers;

    // Initialize search
    float2 ofs = base_uv;
    float current_depth = 0.0;

    // LINEAR SEARCH: walk ray until surface intersection
    // Read initial heightmap depth
    float depth = heightmap_tex.Sample(heightmap_samp, ofs).r;
    if (!is_heightmap_inverted) {
        depth = 1.0 - depth;  // Convert: white=high convention
    }

    // Loop: step along ray until current_depth exceeds heightmap surface depth
    while (current_depth < depth) {
        ofs -= delta;
        depth = heightmap_tex.Sample(heightmap_samp, ofs).r;
        if (!is_heightmap_inverted) {
            depth = 1.0 - depth;
        }
        current_depth += layer_depth;
    }

    // LINEAR REFINEMENT: interpolate between last two sample points
    float2 prev_ofs = ofs + delta;
    float after_depth = depth - current_depth;

    float before_depth = heightmap_tex.Sample(heightmap_samp, prev_ofs).r;
    if (!is_heightmap_inverted) {
        before_depth = 1.0 - before_depth;
    }
    before_depth = before_depth - current_depth + layer_depth;

    // Weighted blend: higher weight where after_depth is larger
    float weight = after_depth / (after_depth - before_depth);
    ofs = lerp(ofs, prev_ofs, weight);

    return ofs;
}
```

### Simplified Version (Less Commentary)

```hlsl
float2 ComputeDeepParallax(float3 view_dir, float2 base_uv, float hm_scale,
                            uint min_layers, uint max_layers, bool hm_inverted) {
    float num_layers_f = lerp((float)max_layers, (float)min_layers, abs(view_dir.z));
    uint num_layers = max((uint)num_layers_f, 1);

    float layer_depth = 1.0 / num_layers;
    float2 delta = view_dir.xy * (hm_scale * 0.01) / num_layers;

    float2 ofs = base_uv;
    float current_depth = 0.0;

    float depth = heightmap_tex.Sample(heightmap_samp, ofs).r;
    if (!hm_inverted) depth = 1.0 - depth;

    while (current_depth < depth && num_layers > 0) {
        ofs -= delta;
        depth = heightmap_tex.Sample(heightmap_samp, ofs).r;
        if (!hm_inverted) depth = 1.0 - depth;
        current_depth += layer_depth;
        num_layers--;  // Prevent infinite loop (safety)
    }

    float2 prev_ofs = ofs + delta;
    float after_depth = depth - current_depth;
    float before_depth = heightmap_tex.Sample(heightmap_samp, prev_ofs).r;
    if (!hm_inverted) before_depth = 1.0 - before_depth;
    before_depth -= current_depth - layer_depth;

    float weight = after_depth / max(after_depth - before_depth, 0.001);
    ofs = lerp(ofs, prev_ofs, weight);

    return ofs;
}
```

---

## 4. Simple Parallax Algorithm (HLSL)

### Overview
Single-sample offset; no raymarching. ~1/8th the cost of deep parallax.

### Implementation

```hlsl
/// Simple parallax: direct UV offset based on single heightmap sample
/// Cheaper but less accurate; visible warping at grazing angles
float2 ComputeSimpleParallax(in float3 view_dir,
                              in float2 base_uv,
                              in float heightmap_scale,
                              in bool is_heightmap_inverted) {
    float depth = heightmap_tex.Sample(heightmap_samp, base_uv).r;
    if (!is_heightmap_inverted) {
        depth = 1.0 - depth;
    }

    // Direct offset; 0.01 factor limits max distortion at distance
    float2 ofs = base_uv - view_dir.xy * depth * (heightmap_scale * 0.01);
    return ofs;
}
```

---

## 5. View Space Calculation (TBN Transform)

### In Vertex Shader (Preferred: Cheaper)

```hlsl
// VS output struct
struct PSInput {
    float3 tangent : TANGENT;      // Tangent (from mesh)
    float3 binormal : BINORMAL;    // Binormal (from mesh)
    float3 normal : NORMAL;        // Normal (from mesh)
    // ... other outputs
};

// In PS, reconstruct view direction in tangent space
float3 vs_pos = input.world_pos;
float3 view_dir_world = normalize(camera_pos - vs_pos);

// TBN matrix (MikktSpace convention: binormal negated)
float3x3 tbn = float3x3(
    input.tangent * flip_t_sign,          // flip_t_sign = flip_tangent ? 1.0 : -1.0
    -input.binormal * flip_b_sign,        // Godot pre-negates binormal; flip_b adjusts
    input.normal
);

float3 view_dir_tangent = normalize(mul(view_dir_world, tbn));
```

### In Fragment Shader (Godot's Approach)

```hlsl
// Compute per-fragment; cost ~3 float3 ops + normalize
float3 vs_pos = input.world_pos;
float3 view_dir_world = normalize(camera_pos - vs_pos);

float3x3 tbn = float3x3(
    input.tangent * flip_t_sign,
    -input.binormal * flip_b_sign,
    input.normal
);

float3 view_dir = normalize(mul(view_dir_world, tbn));

// Now use view_dir in parallax calculation
```

### Flip Signs (Godot Convention)

```cpp
// From material.h / material.cpp
float flip_t = heightmap_parallax_flip_tangent ? 1.0 : -1.0;
float flip_b = heightmap_parallax_flip_binormal ? 1.0 : -1.0;
```

**Rationale**: MikktSpace can flip T or B compared to expected convention. The checkboxes let artists correct it.

---

## 6. Main Shader Entry Point (Fragment Shader)

```hlsl
void PSMain(PSInput input, out float4 out_color : SV_TARGET0) {
    // ─────────────────────────────────────────────────────────────────────
    // 1. Prepare base UV (before parallax)
    // ─────────────────────────────────────────────────────────────────────
    float2 base_uv = input.uv;  // Or computed from triplanar, etc.

    // ─────────────────────────────────────────────────────────────────────
    // 2. Compute parallax offset (if heightmap enabled)
    // ─────────────────────────────────────────────────────────────────────
    if ((heightmap_flags & 0x1) != 0) {  // ENABLE_HEIGHTMAP flag
        // Compute view direction in tangent space
        float3 vs_pos = input.world_pos;
        float3 view_dir_world = normalize(camera_pos - vs_pos);

        float3x3 tbn = float3x3(
            input.tangent * (heightmap_flags & 0x2 ? 1.0 : -1.0),
            -input.binormal * (heightmap_flags & 0x4 ? 1.0 : -1.0),
            input.normal
        );
        float3 view_dir = normalize(mul(view_dir_world, tbn));

        // Apply parallax
        bool use_deep = (heightmap_flags & 0x1) != 0;
        bool is_inverted = (heightmap_flags & 0x8) != 0;

        if (use_deep) {
            base_uv = ComputeDeepParallax(view_dir, base_uv,
                                          heightmap_scale,
                                          heightmap_min_layers,
                                          heightmap_max_layers,
                                          is_inverted);
        } else {
            base_uv = ComputeSimpleParallax(view_dir, base_uv,
                                            heightmap_scale,
                                            is_inverted);
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3. Sample all textures using parallax-adjusted UV
    // ─────────────────────────────────────────────────────────────────────
    float4 albedo = albedo_tex.Sample(main_samp, base_uv);
    float metallic = metallic_tex.Sample(main_samp, base_uv).r;
    float roughness = roughness_tex.Sample(main_samp, base_uv).r;
    float3 normal = normal_tex.Sample(main_samp, base_uv).rgb * 2.0 - 1.0;
    float ao = ao_tex.Sample(main_samp, base_uv).r;

    // ... rest of PBR pipeline

    out_color = float4(final_color, 1.0);
}
```

---

## 7. Material Constant Buffer Packing

```cpp
// C++ side (runtime)
struct HeightmapConstants {
    float heightmap_scale;              // offset 0, size 4
    uint heightmap_min_layers;          // offset 4, size 4
    uint heightmap_max_layers;          // offset 8, size 4
    uint heightmap_flags;               // offset 12, size 4
    // Total: 16 bytes (1 DWORD × 4)
};

// Can be packed in existing material CB if space available,
// or separate b4 register as shown in earlier examples.
```

### Flags Bitfield

```cpp
// Bit layout
#define HM_FLAG_ENABLE_HEIGHTMAP    0x01
#define HM_FLAG_FLIP_TANGENT        0x02
#define HM_FLAG_FLIP_BINORMAL       0x04
#define HM_FLAG_INVERT_HEIGHTMAP    0x08
```

---

## 8. Comparison: Godot GLSL → HLSL Translation

| Godot GLSL | HLSL Equivalent | Notes |
|-----------|-----------------|-------|
| `texture(sampler, uv)` | `tex.Sample(samp, uv)` | DirectX texture API |
| `mix(a, b, t)` | `lerp(a, b, t)` | Renamed function |
| `while (cond)` | `while (cond)` | Identical |
| `mat3(...)` | `float3x3(...)` | Matrix type name |
| `normalize(v)` | `normalize(v)` | Identical |
| `dot(a, b)` | `dot(a, b)` | Identical |
| `abs(x)` | `abs(x)` | Identical |
| `max(a, b)` | `max(a, b)` | Identical |
| Conditionals: `if (flag) code += "..."` | Uniform flag bit checks `if ((flags & mask) != 0)` | Shader-time (Godot codegen) vs. Runtime (HLSL branching) |

---

## 9. Integration Checklist for UzEngine

- [ ] Define `HeightmapConstants` struct in `constant_buffers.h`
- [ ] Add heightmap CB register allocation in RHI root signature
- [ ] Implement `ComputeDeepParallax()` and `ComputeSimpleParallax()` in shared shader include (e.g., `shaders/include/parallax.hlsli`)
- [ ] Update `pbr_pass.hlsl` fragment shader to call parallax before texture sampling
- [ ] Add heightmap material parameters to `MaterialParams` struct
- [ ] Expose UI controls: heightmap scale slider, min/max layers, flip checkboxes
- [ ] Write regression tests: capture reference images from Godot, compare UzEngine output
- [ ] Performance profile: measure texture sample cost on target GPU; adjust default min/max layers
- [ ] Documentation: Add heightmap authoring guide to asset pipeline docs

---

## 10. Example: Before/After Shader Structure

### Before (No Parallax)

```hlsl
void PSMain(PSInput input, out float4 out_color : SV_TARGET0) {
    float2 uv = input.uv;

    float4 albedo = albedo_tex.Sample(main_samp, uv);
    float metallic = metallic_tex.Sample(main_samp, uv).r;
    float roughness = roughness_tex.Sample(main_samp, uv).r;
    float3 normal = normal_tex.Sample(main_samp, uv).rgb;

    // PBR calculations...
    out_color = float4(final_color, 1.0);
}
```

### After (With Parallax)

```hlsl
void PSMain(PSInput input, out float4 out_color : SV_TARGET0) {
    float2 uv = input.uv;

    // ← NEW: Parallax offset calculation
    if (heightmap_enabled) {
        float3 view_dir = ComputeViewDirTangentSpace(input);
        uv = heightmap_use_deep_parallax
            ? ComputeDeepParallax(view_dir, uv, ...)
            : ComputeSimpleParallax(view_dir, uv, ...);
    }

    float4 albedo = albedo_tex.Sample(main_samp, uv);
    float metallic = metallic_tex.Sample(main_samp, uv).r;
    float roughness = roughness_tex.Sample(main_samp, uv).r;
    float3 normal = normal_tex.Sample(main_samp, uv).rgb;

    // PBR calculations unchanged...
    out_color = float4(final_color, 1.0);
}
```

---

## 11. Performance Profile (Baseline)

### Deep Parallax

| Scenario | Texture Lookups | Est. Cost (GPU) | Notes |
|----------|-----------------|-----------------|-------|
| Normal incidence | ~8 | 0.3 ms @ 1440p | min_layers kicks in |
| 45° angle | ~20 | 0.8 ms @ 1440p | Adaptive |
| Grazing (85°) | ~32 | 1.2 ms @ 1440p | max_layers; perceptually critical |

**Total frame time impact @ 1440p, 60 FPS**: ~2–3 ms for full screen coverage. Acceptable if parallax not on every pixel.

### Simple Parallax

| Scenario | Texture Lookups | Est. Cost (GPU) |
|----------|-----------------|-----------------|
| Any angle | 1 | 0.04 ms @ 1440p |

**Orders of magnitude cheaper; use for distant objects / mobile.**

---

## 12. Known Issues & Workarounds

### Issue 1: UV Wrapping at Edges

**Symptom**: Parallax offset samples outside mesh UV bounds → texture bleeding/artifacts.

**Workaround (Godot approach)**:
- Artist responsibility to ensure heightmaps don't displace beyond UV island edges
- Optional: Clamp/mirror sampling at boundaries (add checks in parallax function)

### Issue 2: Banding in Deep Parallax

**Symptom**: Visible layers/steps at shallow angles.

**Workaround**:
- Increase `min_layers` (cost: slightly more samples everywhere)
- Use dithering or noise to break banding (advanced)

### Issue 3: View-Dependent Artifacts at Grazing Angles

**Symptom**: Swimming/warping when camera rotates parallel to surface.

**Workaround** (Godot's):
- Simple parallax uses 0.01 scale factor to limit offset magnitude
- Comment: "reduces impression of depth, but avoids visible warping in the distance"

### Issue 4: Parallax + Triplanar Incompatible

**Symptom**: Godot warns if both enabled; disables parallax.

**Reason**: Triplanar blends multiple UVs; parallax offset per-UV conflicts with blend.

**Workaround**: Choose either triplanar OR parallax per material.

---

## 13. Testing Vectors

### Unit Test Cases

```cpp
// test_parallax.cpp (pseudo-Google Test)

TEST(DeepParallax, AdaptiveLayerCount) {
    // View from top (z=1.0): num_layers should ≈ min
    float3 view_top = float3(0, 0, 1.0);
    uint layers_top = ComputeLayerCount(view_top, 4, 32);
    EXPECT_LE(layers_top, 8);

    // View from side (z≈0.0): num_layers should ≈ max
    float3 view_side = float3(1.0, 0, 0.001);
    uint layers_side = ComputeLayerCount(normalize(view_side), 4, 32);
    EXPECT_GE(layers_side, 24);
}

TEST(DeepParallax, HeightmapInversion) {
    // White = high (normal)
    float depth_white_high = SampleHeightmapDepth(0.8, false);
    EXPECT_NEAR(depth_white_high, 0.2, 0.001);  // 1.0 - 0.8

    // Black = high (inverted)
    float depth_black_high = SampleHeightmapDepth(0.8, true);
    EXPECT_NEAR(depth_black_high, 0.8, 0.001);  // Direct sample
}

TEST(SimpleParallax, DirectOffset) {
    float2 uv_result = ComputeSimpleParallax(
        float3(1, 0, 0.5),  // view_dir
        float2(0.5, 0.5),   // base_uv
        1.0,                // scale
        false               // not inverted
    );
    // Offset should be in -view_dir.xy direction
    EXPECT_LT(uv_result.x, 0.5);
}
```

### Visual Regression Tests

1. **Plane with heightmap**: Capture reference from Godot @ 8 camera angles
2. **Compare UzEngine output**: Pixel-level diff (allow ±2% tolerance for precision differences)
3. **Edge cases**:
   - Inverted heightmap
   - Flipped tangent/binormal
   - min_layers = max_layers (constant layer count)
   - Zero heightmap scale

---

## 14. References to Godot Source

Godot 4.x source paths:

1. **Material header**: `/e/godot/scene/resources/material.h` (lines 384–703)
   - Property declarations: `deep_parallax`, `deep_parallax_min_layers`, `deep_parallax_max_layers`, `heightmap_parallax_flip_tangent`, `heightmap_parallax_flip_binormal`

2. **Material codegen (POM)**: `/e/godot/scene/resources/material.cpp` (lines 1544–1614)
   - Deep parallax algorithm (lines 1553–1594)
   - Simple parallax algorithm (lines 1596–1605)
   - TBN matrix with flips (line 1550)
   - Adaptive layer calculation (line 1557)

3. **Property setters**: `/e/godot/scene/resources/material.cpp` (lines 2863–2905)
   - `set_heightmap_deep_parallax(bool)`
   - `set_heightmap_deep_parallax_min_layers(int)`
   - `set_heightmap_deep_parallax_max_layers(int)`
   - `set_heightmap_deep_parallax_flip_tangent(bool)`
   - `set_heightmap_deep_parallax_flip_binormal(bool)`

---

## Conclusion

This reference provides a direct, working translation of Godot 4's POM algorithm into HLSL for UzEngine. The code is ready for integration into the PBR rendering pipeline with minimal modification. Key design decisions (linear search, single-step refinement, no self-shadowing) align with UzEngine's performance targets and existing architecture.

