# Godot 4 Parallax Occlusion Mapping (POM) / Heightmap Analysis

**Date**: 2026-03-13
**Source**: Godot 4.x `scene/resources/material.cpp` and `material.h`
**Pattern Category**: Shader-based displacement, view-dependent UV offset
**Applicability to UzEngine**: Direct adaptation for POM implementation

---

## Executive Summary

Godot 4 implements POM via **StandardMaterial3D** shader generation (C++ → GLSL codegen).
The implementation consists of:

1. **Two algorithms**: Deep Parallax (iterative raymarching + binary refinement) and Simple Parallax (direct offset)
2. **Adaptive layer count**: Grazing angle fade (view-dependent) from `min_layers` to `max_layers`
3. **Invertible heightmap**: Flag to flip depth interpretation (white = high vs. black = high)
4. **Flip tangent/binormal**: MikktSpace correction for shader space mismatches
5. **No self-shadowing**: Heightmap displacement does NOT compute contact shadows; separate AO texture handles that
6. **Pre-shader integration**: Height offset calculated in fragment, then applied to all texture samples

**Key Godot Leanness Lesson**: Uses C++ string codegen to produce shader code, avoiding polymorphic shader class hierarchy. All POM logic baked into uniform-dependent conditionals at compile time.

---

## 1. Implementation Architecture

### Source Files
- **Main**: `/e/godot/scene/resources/material.cpp` (lines 1544–1614)
- **Header**: `/e/godot/scene/resources/material.h` (lines 384–703)
- **Class**: `BaseMaterial3D` — Godot's PBR material with shader auto-generation

### Material Properties

```cpp
// material.h (lines 551, 584–588)
float heightmap_scale = 0.0f;              // Range: -16.0 to 16.0
bool deep_parallax = false;                // Enable iterative raymarching
int deep_parallax_min_layers = 0;          // Min layer count (grazing angles)
int deep_parallax_max_layers = 0;          // Max layer count (normal incidence)
bool heightmap_parallax_flip_tangent = false;     // Flip T in TBN for MikktSpace
bool heightmap_parallax_flip_binormal = false;    // Flip B in TBN for MikktSpace
```

### Shader Uniforms Generated

```glsl
// From material.cpp, line 1440–1445
uniform sampler2D texture_heightmap : hint_default_black, %s;  // hint varies
uniform float heightmap_scale : hint_range(-16.0, 16.0, 0.001);
uniform int heightmap_min_layers : hint_range(1, 64);
uniform int heightmap_max_layers : hint_range(1, 64);
uniform vec2 heightmap_flip;  // (flip_tangent ? 1.0 : -1.0, flip_binormal ? 1.0 : -1.0)
```

**Texture hint**: `hint_default_black` — sensible for heightmaps (0 = no displacement).

---

## 2. Core Algorithm: Deep Parallax (Iterative Raymarching)

### Overview
Godot's "Deep Parallax" is **POM-style raymarching with linear interpolation refinement** (not full binary search, but close).

### Pseudocode (from material.cpp, lines 1553–1594)

```glsl
// Adaptive layer count based on view angle
float num_layers = mix(float(heightmap_max_layers), float(heightmap_min_layers),
                       abs(dot(vec3(0.0, 0.0, 1.0), view_dir)));
float layer_depth = 1.0 / num_layers;

// Ray direction in UV space, scaled by heightmap_scale * 0.01
vec2 p = view_dir.xy * heightmap_scale * 0.01;
vec2 delta = p / num_layers;  // Step per layer

// Init
vec2 ofs = base_uv;
float current_depth = 0.0;
float depth = texture(texture_heightmap, ofs).r;
if (heightmap_inverted) depth = 1.0 - depth;

// LINEAR SEARCH: walk down until depth intersection
while (current_depth < depth) {
    ofs -= delta;                    // Step along ray
    depth = texture(texture_heightmap, ofs).r;
    if (heightmap_inverted) depth = 1.0 - depth;
    current_depth += layer_depth;
}

// LINEAR REFINEMENT (1 iteration, not binary)
vec2 prev_ofs = ofs + delta;
float after_depth = depth - current_depth;
float before_depth = texture(texture_heightmap, prev_ofs).r - current_depth + layer_depth;
if (heightmap_inverted) before_depth = (1.0 - before_depth) - current_depth + layer_depth;

float weight = after_depth / (after_depth - before_depth);
ofs = mix(ofs, prev_ofs, weight);

// Apply offset to base_uv
base_uv = ofs;
```

### Key Properties

| Property | Value | Rationale |
|----------|-------|-----------|
| **Search type** | Linear (while loop) | Simple, GPU-friendly; terminates naturally at intersection |
| **Refinement** | Linear interpolation (1 step) | Reduces banding vs. pure step; not as accurate as binary search but cheaper |
| **Layer-to-depth mapping** | Layer index = current_depth; heightmap value = distance to surface | Standard POM convention: white/high = bump; black/low = valley |
| **Texture sampling** | Per-layer texture lookup in while loop | Texture-heavy but necessary for per-layer depth test |
| **Performance** | O(num_layers) texture lookups | Adaptive layers + grazing angle fade mitigate cost |

### Adaptive Layer Calculation

```glsl
float num_layers = mix(float(heightmap_max_layers), float(heightmap_min_layers),
                       abs(dot(vec3(0.0, 0.0, 1.0), view_dir)));
```

- **At normal incidence** (view_dir ≈ [0, 0, 1]): dot = 1 → `num_layers = min_layers` (fewer samples)
- **At grazing angles** (view_dir ≈ [X, Y, 0]): dot = 0 → `num_layers = max_layers` (more samples)
- **Rationale**: Grazing angles see more displacement distortion; need finer sampling

**Typical values**: `min_layers = 8`, `max_layers = 32`

---

## 3. Core Algorithm: Simple Parallax (Non-Deep)

### Overview
Direct single-sample offset, **no raymarching**. Cheaper but less accurate.

### Pseudocode (from material.cpp, lines 1597–1605)

```glsl
float depth = texture(texture_heightmap, base_uv).r;
if (!heightmap_inverted) depth = 1.0 - depth;

// Direct offset: scale by depth and heightmap_scale
vec2 ofs = base_uv - view_dir.xy * depth * heightmap_scale * 0.01;

base_uv = ofs;
```

### Properties

| Property | Value |
|----------|-------|
| **Samples** | 1 texture lookup |
| **Parallax effect** | Subtle; visible from grazing angles, minimal distortion |
| **Warping artifacts** | Higher—no refinement step |
| **Distance fade** | Implicit; larger offset at oblique angles → more artifacts at distance → visual degradation is "natural" LOD |
| **Use case** | Mobile, foliage, far-distance objects |

**Note**: Godot comments (line 1602–1603):
> "Use offset limiting to improve the appearance of non-deep parallax. This reduces the impression of depth, but avoids visible warping in the distance."

Translation: The 0.01 scalar limit (not scaling linearly) prevents z-fighting and texture swimming.

---

## 4. Material Properties: Tangent/Binormal Flips

### Why Needed

MikktSpace standard for tangent space can flip T or B relative to expected conventions.
The TBN matrix must match the heightmap authoring convention.

### Code (material.cpp, line 1550)

```glsl
vec3 view_dir = normalize(normalize(-VERTEX + EYE_OFFSET) *
                          mat3(TANGENT * heightmap_flip.x,
                               -BINORMAL * heightmap_flip.y,  // Binormal is already negated!
                               NORMAL));
```

### Notes

- **BINORMAL is pre-negated** in Godot to match MikktSpace convention
- **heightmap_flip.x/y** applied as sign multipliers: ±1.0
- If mismatch observed (POM inverted or skewed), toggle flip flags
- **No GPU cost**: Just scalar multiplies during view_dir calculation

---

## 5. Heightmap Texture Interpretation

### Flag: `FLAG_INVERT_HEIGHTMAP`

```cpp
// Conditional code generation (material.cpp, lines 1564–1568)
if (flags[FLAG_INVERT_HEIGHTMAP]) {
    code += "float depth = texture(texture_heightmap, ofs).r;\n";  // Black = high
} else {
    code += "float depth = 1.0 - texture(texture_heightmap, ofs).r;\n";  // White = high
}
```

**Default (white = high)**:
- Typical heightmap convention
- Black texture = valley; white = peak
- Depth = 1.0 - sampled_value

**Inverted (black = high)**:
- Rarely used; for special textures (occlusion maps repurposed as heightmaps)
- Depth = sampled_value

---

## 6. Integration with PBR Pipeline

### Placement in Fragment Shader

1. **First**: Heightmap POM calculation (modifies `base_uv`)
2. **Then**: All texture samples use modified `base_uv`
   - Albedo, normal, metallic, roughness, AO, emission — all use `base_uv` from POM offset

### Code (material.cpp, lines 1608–1631)

```glsl
base_uv = ofs;  // Apply POM offset

// All subsequent texture samples use base_uv
vec4 albedo_tex = texture(texture_albedo, base_uv);
float metallic_tex = dot(texture(texture_metallic, base_uv), metallic_texture_channel);
vec4 roughness_tex = texture(texture_roughness, base_uv);
// ... etc
```

### Detail UV (if enabled)

```cpp
// material.cpp, lines 1609–1611
if (features[FEATURE_DETAIL] && detail_uv == DETAIL_UV_2) {
    code += "base_uv2 -= ofs;\n";  // Apply same offset to secondary UV
}
```

**Rationale**: Detail maps often on UV2; applying same offset maintains tiling coherence.

---

## 7. No Self-Shadowing in POM

### Key Finding

**Godot's POM does NOT compute contact shadows** from the heightmap.

Self-shadowing (cavity occlusion from recessed details) is handled separately:
- **Ambient Occlusion** texture (separate from heightmap)
- **Baked into geometry normal** during authoring
- Optional per-material AO multiplier

### Why?

Self-shadowing requires light-space raycasts, which are:
1. **Expensive**: Additional texture lookups per light
2. **Complex**: Requires light direction per fragment
3. **Artist-workable**: AO maps baked offline easier than real-time computation

**UzEngine Adaptation**: Same approach—use separate heightmap for POM + separate AO texture for cavity.

---

## 8. Performance Considerations

### Texture Sample Count (Deep Parallax)

```
Typical: 8–32 samples per pixel per fragment shader call
Cost: 8–32 texture lookups (with potential texture cache misses)
```

### Layer Adaptive Scaling

**Why adaptive layer count works**:

- **Normal incidence** (top-down view): Heightmap displacement appears minimal → fewer layers needed
- **Grazing angles** (sideway view): Displacement amplified perceptually → more samples justify cost

**Empirical trade-off** (from game engine practice):
- `min_layers = 4..8`: Normal incidence
- `max_layers = 16..32`: Grazing angle
- Beyond 32 layers: diminishing returns, visual pop when switching layers

### Godot's 0.01 Scalar

```glsl
vec2 p = view_dir.xy * heightmap_scale * 0.01;
```

**Purpose**: Limit max displacement to 1% of heightmap scale range per sample
- Without this: extreme heightmap_scale values cause visible swimming/artifacts
- With this: scale becomes user-friendly (range -16 to +16)

**UzEngine corollary**: If exposing heightmap_scale as material parameter, apply similar limiting to prevent artist errors.

---

## 9. Constraints and Limitations

| Constraint | Impact | Workaround |
|-----------|--------|-----------|
| **No triplanar** | Heightmap incompatible with triplanar UV | Use planar UV mapping; manually blend meshes for varied surfaces |
| **Linear search only** | No binary refinement between last sample and intersection | Accept slight banding; increase layers for smoothness |
| **No self-occlusion light** | Can't self-shadow in real-time | Bake AO texture separately |
| **Single heightmap** | One displacement map per material | Multi-layer displacement requires material variants or shader branching |
| **View-dependent** | Parallax effect only visible from angles | Expected; on normal incidence it fades (by design) |
| **No distance fade** | Expensive even at far distances; no per-vertex LOD | Use lower min_layers + material distance fade (separate system) |

---

## 10. Godot's Leanness Lessons for UzEngine

### Lesson 1: String Codegen, Not Polymorphic Shaders

**Godot approach**:
- C++ builds shader text conditionally (if-chains on material flags)
- Single GLSL source generated per unique configuration
- No virtual shader classes, no inheritance

**UzEngine opportunity**:
- Use HLSL codegen (append snippets based on material flags)
- Or: Use shader variants + defines (DXC preprocessor)
- **Cost**: Keep shader source generation simple; avoid macro explosion

### Lesson 2: Uniforms Over Branching

**Godot uses**:
- `uniform int heightmap_min_layers : hint_range(1, 64);`
- Dynamic layer count via `mix(max_layers, min_layers, ...)`

**UzEngine corollary**:
- Store min/max layers as material CBV data
- Compute num_layers in shader (1–2 float ops)
- No GPU branching; just data interpolation

### Lesson 3: View-Space Calculation in Fragment

**Godot calculates**:
```glsl
vec3 view_dir = normalize(normalize(-VERTEX + EYE_OFFSET) * mat3(...));
```

Per-fragment, not per-vertex. **Cost**: Low (3 float3 ops + normalize).

**UzEngine variant**: Could pass view_dir from vertex shader (cheaper), but Godot keeps it in fragment for simplicity.

### Lesson 4: Separation of Concerns

- **POM** = UV offset only
- **Self-shadowing** = Separate AO texture
- **Normal detail** = Normal map (not displaced from heightmap)

This design keeps each system simple and composable.

---

## 11. UzEngine Implementation Roadmap

### Phase 1: Direct Adaptation (Minimal)

```cpp
// material_params.h
struct HeightmapParams {
    float scale;              // -16.0 to 16.0
    uint32_t min_layers;      // 4–8
    uint32_t max_layers;      // 16–32
    bool enable_deep_parallax;
    bool flip_tangent;        // MikktSpace correction
    bool flip_binormal;       // MikktSpace correction
    bool invert_heightmap;    // White=high vs. Black=high
};
```

### Phase 2: Shader Codegen (HLSL)

```hlsl
// shaders/pbr_base.hlsl (pseudo-code)

#if HEIGHTMAP_ENABLED

float3 view_dir = normalize(normalize(-input.world_pos + eye_pos) *
                            float3x3(tangent * flip_t, -binormal * flip_b, normal));

#if DEEP_PARALLAX
float num_layers = lerp(max_layers, min_layers, abs(dot(float3(0,0,1), view_dir)));
float2 delta = view_dir.xy * heightmap_scale * 0.01 / num_layers;
// ... linear search loop
#else
float depth = 1.0 - heightmap_tex.Sample(samp, base_uv).r;
base_uv -= view_dir.xy * depth * heightmap_scale * 0.01;
#endif

#endif
```

### Phase 3: Material UI

- **Inspector panels**: heightmap_scale slider, min/max layers, flip checkboxes
- **Texture preview**: Show heightmap + wireframe overlay of offset direction

### Phase 4: Testing

- **Unit tests**: Layer count calculation for view angles
- **Visual tests**: Compare against Godot/UE5 reference
- **Performance**: Measure texture sample cost; profile on target GPUs

---

## 12. Comparison: Godot vs. UE5 vs. UzEngine Current State

### Godot 4

| Aspect | Implementation |
|--------|---|
| **Raymarching** | Linear search + 1-step linear refinement |
| **Layer adaptivity** | Grazing angle fade (view.z → layers) |
| **Self-shadowing** | No (separate AO texture) |
| **Heightmap type** | Single texture, invertible |
| **Integration** | Pre-fragment offset, applied to all texture samples |
| **Distance fade** | Implicit (user increases min_layers for LOD) |

### UE5 (Parallax Occlusion Mapping)

| Aspect | Implementation |
|--------|---|
| **Raymarching** | Linear search + N-step binary refinement (more accurate) |
| **Layer adaptivity** | Similar grazing angle fade |
| **Self-shadowing** | Yes (light-space raycasts; expensive) |
| **Heightmap type** | Single or blended textures |
| **Integration** | Pre-fragment offset via material expressions |
| **Distance fade** | Explicit distance fade parameter |

### UzEngine Current (pre-POM)

| Aspect | Implementation |
|--------|---|
| **Displacement** | Bump mapping via normal map (already implemented) |
| **Heightmap support** | None currently |
| **Self-shadowing** | Contact shadows (separate heightmap) |
| **Target approach** | Adopt Godot's linear model (simpler, GPU-efficient) |

---

## 13. Code References (Absolute Paths)

### Key Source Files

1. **Material property definitions**:
   `/e/godot/scene/resources/material.h` (lines 384–703)

2. **Shader codegen (POM algorithm)**:
   `/e/godot/scene/resources/material.cpp` (lines 1544–1614)

3. **Property setters/getters**:
   `/e/godot/scene/resources/material.cpp` (lines 2863–2905)

4. **ClassDB bindings**:
   `/e/godot/scene/resources/material.cpp` (lines 3501–3502, and similar)

### Extracted Code Snippets

#### Deep Parallax Algorithm (Full)
```glsl
// From material.cpp, lines 1550–1594
vec3 view_dir = normalize(normalize(-VERTEX + EYE_OFFSET) *
                          mat3(TANGENT * heightmap_flip.x,
                               -BINORMAL * heightmap_flip.y,
                               NORMAL));

float num_layers = mix(float(heightmap_max_layers), float(heightmap_min_layers),
                       abs(dot(vec3(0.0, 0.0, 1.0), view_dir)));
float layer_depth = 1.0 / num_layers;
vec2 p = view_dir.xy * heightmap_scale * 0.01;
vec2 delta = p / num_layers;
vec2 ofs = base_uv;

float depth = texture(texture_heightmap, ofs).r;
if (!FLAG_INVERT_HEIGHTMAP) depth = 1.0 - depth;

float current_depth = 0.0;
while (current_depth < depth) {
    ofs -= delta;
    depth = texture(texture_heightmap, ofs).r;
    if (!FLAG_INVERT_HEIGHTMAP) depth = 1.0 - depth;
    current_depth += layer_depth;
}

vec2 prev_ofs = ofs + delta;
float after_depth = depth - current_depth;
float before_depth = texture(texture_heightmap, prev_ofs).r - current_depth + layer_depth;
if (!FLAG_INVERT_HEIGHTMAP) before_depth = (1.0 - before_depth) - current_depth + layer_depth;

float weight = after_depth / (after_depth - before_depth);
ofs = mix(ofs, prev_ofs, weight);

base_uv = ofs;
```

#### Simple Parallax Algorithm (Full)
```glsl
// From material.cpp, lines 1597–1605
float depth = texture(texture_heightmap, base_uv).r;
if (!FLAG_INVERT_HEIGHTMAP) depth = 1.0 - depth;

vec2 ofs = base_uv - view_dir.xy * depth * heightmap_scale * 0.01;
base_uv = ofs;
```

---

## 14. Summary Table: Ready for UzEngine Integration

| Feature | Godot Implementation | UzEngine Adoption Status |
|---------|---------------------|------------------------|
| **POM algorithm** | Linear search + 1-step refinement | Ready; direct HLSL port |
| **Adaptive layers** | Grazing angle fade via dot(z_axis, view_dir) | Ready; straightforward |
| **Material params** | scale, min/max layers, flip_t, flip_b, invert flag | Ready; define in material CB |
| **Integration point** | Pre-fragment UV offset, applied to all textures | Ready; insert before texture samples |
| **Self-shadowing** | None (separate AO) | Ready; align with UzEngine's contact shadow system |
| **Distance LOD** | User tunes min_layers | Partial; recommend explicit distance fade parameter |
| **Testing vectors** | Procedural heightmap, animated camera angles | Partial; need visual regression suite |
| **Performance target** | 60 FPS @ 1440p, ~32 layers max | TBD; profile on target GPU |

---

## Conclusion

Godot 4's POM implementation is **lean, well-integrated, and GPU-efficient**. The linear raymarching + single-step refinement approach offers a sweet spot between accuracy and performance. The adaptive layer count based on view angle is elegant and avoids expensive distance-based fade parameters.

**For UzEngine**: Direct HLSL adaptation is straightforward. The main design decision is whether to support self-shadowing (UE5-style, expensive) or defer to baked AO (Godot-style, simpler). Godot's choice aligns with UzEngine's existing contact-shadow-via-heightmap system, so recommend **Godot approach** (POM + separate AO) unless self-shadowing becomes a critical visual requirement.

