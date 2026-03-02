# Material Editor Spec

## Overview & Goals

The Material Editor is a **high-level, artist-facing tool** embedded in the VSCode extension that allows character artists and environment artists to configure material instances, assign textures, tune PBR parameters, build material layer stacks, and manage per-part material assignments on multipart characters -- all with a live 3D preview rendered by UzEngine's DX12 backend.

The Material Editor does **not** expose shader graphs or node-based programming. That domain belongs to the separate [Shader Editor](shader-editor.md). Instead, the Material Editor operates one level above: it consumes compiled shaders (`.hlsl` files referenced from `.uzmat`) and presents their exposed parameters as editable controls. Artists configure **what values** flow into the shader; they never see the shader source.

### Design Principles

1. **Instance-based workflow** -- Artists work with material instances that inherit from master materials. They override only what differs, keeping the data footprint small and changes propagable.
2. **AI-friendly formats** -- All data is stored in `.uzmat` INI files, readable by both humans and LLMs. No binary blobs for material definitions.
3. **Multipart-first** -- The editor is designed around the multipart character workflow from day one: load a full character skeleton with N mesh pieces, each with M material slots, and configure them holistically.
4. **Live preview** -- Every parameter change triggers an immediate re-render via the JPEG+WebSocket streaming pipeline. No export step needed.
5. **Non-destructive layering** -- Material layers (dirt, wetness, damage) stack on top of the base material without modifying it. Layers can be toggled at runtime from Lua scripts.

### Target Users

| User | Workflow |
|------|----------|
| **Character Artist** | Import FBX/glTF character with multiple body parts (head, torso, arms, legs, hair, eyes). Assign skin material to body parts, hair material to hair mesh, eye material to eye meshes. Tune skin subsurface tint, roughness maps, detail normals. Add dirt/blood layers for combat variants. |
| **Environment Artist** | Create layered materials for terrain props -- stone base + moss overlay + wetness layer. Tune roughness/metallic per layer. Use procedural masks for edge wear. Reuse master materials across dozens of props via instances. |
| **Technical Artist** | Create master materials with carefully tuned parameter ranges. Set up dye channels for character customization (player armor color). Configure LOD material chains with texture mip biases. |

### Master Material vs. Material Instance

```
Master Material (compiled shader + parameter definitions)
    |
    +-- Base Instance (default parameter values)
    |       |
    |       +-- Character Instance (per-character overrides)
    |       |       |
    |       |       +-- LOD1 Instance (reduced textures, mip bias)
    |       |       +-- LOD2 Instance (vertex color only, no textures)
    |       |
    |       +-- Variant Instance (color variant for marketplace skin)
    |
    +-- Another Base Instance (different preset)
```

A **master material** is the combination of a compiled shader and its full parameter schema (extracted via DXC reflection at compile time). It defines which parameters exist, their types, default values, and valid ranges.

A **material instance** references a master (or another instance as parent) and overrides specific parameter values. Unoverridden parameters fall through to the parent. This inheritance chain can be N levels deep, though in practice 2-3 levels is typical.

---

## Editor Layout

### ASCII Mockup

```
+-----------------------------------------------------------------------+
|  TOOLBAR                                                              |
|  [Save] [Revert] [Export] [Apply to Scene] | Master: pbr.hlsl  v     |
+----------+----------------------------------+-------------------------+
|          |                                  |                         |
| MATERIAL |       3D PREVIEW VIEWPORT        |  TEXTURE SLOTS          |
| SLOT     |                                  |                         |
| LIST     |   +------------------------+     |  Albedo    [brick.png ] |
|          |   |                        |     |  Normal    [brick_n   ] |
| > Head   |   |    [live DX12 render]  |     |  Metallic  [---none---] |
|   Body   |   |                        |     |  Roughness [brick_r   ] |
|   Arms   |   |    orbit / turntable   |     |  AO        [brick_ao ] |
|   Legs   |   |                        |     |  Emissive  [---none---] |
|   Hair   |   +------------------------+     |  Opacity   [---none---] |
|   Eyes   |                                  |                         |
|          |  View: [PBR] [Wire] [Alb] [Nrm]  +-------------------------+
|          |  IBL:  [Studio v]  Anim: [T-pose] |                         |
|          |  A/B:  [off]  Channel: [RGB]      |  PARAMETERS             |
|          |                                  |                         |
|          |                                  |  albedo_color  [======] |
|          |                                  |  roughness     [==.6==] |
|          |                                  |  metallic      [==0===] |
|          |                                  |  normal_scale  [==1===] |
|          |                                  |  ao_intensity  [==1===] |
|          |                                  |  alpha_mode    [OPAQUE] |
|          |                                  |  alpha_cutoff  [==.5==] |
|          |                                  |  tint_channel  [==0===] |
|          |                                  |  uv_scale      [1,1  ] |
|          |                                  |                         |
+----------+----------------------------------+-------------------------+
|                                             |                         |
|          (status bar / compile errors)      |  LAYER STACK            |
|                                             |                         |
|                                             |  [x] Base        1.0   |
|                                             |  [x] Dirt        0.3   |
|                                             |  [ ] Wetness     0.0   |
|                                             |  [ ] Blood       0.0   |
|                                             |  [+ Add Layer]         |
|                                             |                         |
+---------------------------------------------+-------------------------+
```

### Panel Descriptions

#### Material Slot List (left, 180px)

Displays all material slots on the currently loaded mesh (or multipart character). Each slot is a row showing:

- Slot index and name (from `.uzmesh` submesh table or FBX material name)
- Assigned `.uzmat` file path (or "unassigned" in red)
- Thumbnail preview (64x64 rendered swatch)

Clicking a slot selects it. The right panels (Texture Slots, Parameters, Layer Stack) update to show that slot's material. Drag-and-drop a `.uzmat` file from the Asset Browser onto a slot to assign it.

Multi-select (Ctrl+click) allows bulk operations: assign the same material to multiple slots, or bulk-adjust a parameter across selected slots.

#### 3D Preview Viewport (center)

A live DX12-rendered viewport streaming via the engine's JPEG+WebSocket pipeline. Features:

| Feature | Details |
|---------|---------|
| **Camera** | Orbit camera with mouse drag. Scroll to zoom. Middle-click to pan. Double-click to frame selected mesh piece. |
| **Turntable** | Toggle auto-rotation (configurable speed 0-60 RPM). |
| **IBL Environment** | 5 presets selectable from dropdown (see table below). Each is an HDR cubemap for image-based lighting. |
| **View Modes** | PBR (full lit), Wireframe, Albedo-only, Normal-only, Roughness-only, Metallic-only, AO-only, Emissive-only. |
| **Split View** | Side-by-side comparison of two view modes (e.g., PBR vs Normals). |
| **A/B Mode** | Before/after comparison. "A" captures current state; further edits show as "B". Toggle to compare. |
| **Animation** | T-pose (default), Idle animation, or any `.uzanim` clip from the skeleton. |
| **Channel Isolation** | View individual texture channels: R, G, B, A, or full RGB. |
| **Specular Probe** | Overlay mode showing a specular highlight response sphere to evaluate roughness/metallic tuning. |
| **Background** | Solid color (default: 18% gray), checkerboard (for alpha), or IBL environment visible. |

**IBL Environment Presets**:

| Preset | HDR File | Description | Use Case |
|--------|----------|-------------|----------|
| Studio | `env/studio.hdr` | Neutral soft-box lighting, even illumination | General material tuning, skin |
| Outdoor | `env/outdoor.hdr` | Daylight sky + ground bounce, warm sun | Environment props, foliage |
| Night | `env/night.hdr` | Cool moonlight, dark sky, low-key | Emissive materials, glowing effects |
| Lava | `env/lava.hdr` | Orange/red volcanic environment, high dynamic range | Extreme HDR testing, metals |
| Snow | `env/snow.hdr` | Overcast white sky, cold blue shadows | White-balance testing, ice materials |

#### Texture Slots Panel (right-top)

Displays all texture slots defined by the master shader, with drag-and-drop zones for each. The standard PBR slot set:

| Slot | Register | Format | sRGB | Description |
|------|----------|--------|------|-------------|
| Albedo | `t0` | BC7 SRGB | Yes | Base color map |
| Normal | `t1` | BC5 UNORM | No | Tangent-space normal (RG), Z reconstructed |
| Metallic | `t2` | BC4 UNORM | No | Metallic mask (single channel) |
| Roughness | `t3` | BC4 UNORM | No | Roughness map (single channel) |
| AO | `t4` | BC4 UNORM | No | Ambient occlusion (baked or SSAO supplement) |
| Emissive | `t5` | BC7 SRGB | Yes | Emissive color mask |
| Opacity | `t6` | BC4 UNORM | No | Opacity mask (for MASK/BLEND alpha modes) |

Each slot shows:
- Texture thumbnail (64x64)
- File path (editable, or drag-drop to assign)
- Resolution and format info (e.g., "1024x1024 BC7")
- "Clear" button to unassign
- "Open in Texture Preview" button to view in the [Texture Preview Panel](../specs/asset-pipeline.md)

When a texture is assigned or removed, the shader variant system automatically updates `HAS_<SLOT>_TEX` defines and recompiles the variant if needed.

#### Parameter Panel (right-mid)

Displays all numeric/color/enum parameters exposed by the material's shader, extracted via DXC reflection. Each parameter shows:

| Type | Control | Widget |
|------|---------|--------|
| `float` | Slider + numeric input | Range slider `[min..max]` with step |
| `float2` | Two sliders | Labeled X, Y |
| `float3` (color) | Color picker + hex input | sRGB color wheel with HDR intensity slider |
| `float3` (vector) | Three sliders | Labeled X, Y, Z |
| `float4` | Four sliders or color+alpha | Context-dependent |
| `enum` | Dropdown | E.g., alpha_mode: OPAQUE / MASK / BLEND |
| `bool` | Checkbox | Toggle defines like ENABLE_DETAIL_UV |

Parameters that differ from the parent instance are shown with a **blue override indicator** (a small dot next to the label). Right-clicking a parameter shows a context menu: "Reset to parent value", "Copy value", "Paste value".

Parameter metadata (range, step, display name, tooltip) is defined in the master material header or a sidecar `.uzmatdef` file.

#### Layer Stack Panel (right-bottom)

Displays the material layer stack for the selected slot. Layers are stacked bottom-to-top, with the base material at the bottom. See [Section 5: Material Layering](#5-material-layering) for full details.

Each layer row shows:
- Enable/disable checkbox
- Layer name (e.g., "Dirt", "Wetness")
- Intensity slider `[0.0 .. 1.0]`
- Blend mode indicator (Multiply, Overlay, Additive)
- Expand arrow to reveal per-layer settings (mask, tint, UV scale)

Drag to reorder layers. Right-click for "Delete layer", "Duplicate layer", "Edit mask".

#### Toolbar

| Button | Action |
|--------|--------|
| **Save** | Write all modified `.uzmat` files to disk. Keyboard: `Ctrl+S`. |
| **Revert** | Discard all unsaved changes, reload from disk. Keyboard: `Ctrl+Z` (undo) or toolbar revert (full reset). |
| **Export** | Export the current material setup as a standalone `.uzmat` package (material + referenced textures). |
| **Apply to Scene** | Push material changes to the active scene. Auto-applies via hot-reload when "Auto Apply" is enabled. |
| **Master selector** | Dropdown showing the current master shader. Change to re-parent the instance. |

---

## PBR Material Instance System

### Reflection-Based Parameter Schema

When a master material's shader is compiled, DXC reflection extracts the cbuffer layout and texture bindings. The engine stores this schema in memory and uses it to:

1. Validate `.uzmat` parameter names against the shader's actual uniforms.
2. Generate the Parameter Panel UI controls with correct types and default values.
3. Detect type mismatches at load time and log warnings.

```cpp
// src/renderer/material/material_schema.h

struct ParamDef {
    StringId       name;           // "roughness", "albedo", etc.
    ParamType      type;           // FLOAT, FLOAT2, FLOAT3, FLOAT4, INT, BOOL
    uint32_t       cbuffer_offset; // byte offset in material cbuffer
    float          default_value[4];
    float          min_value[4];   // display range minimum
    float          max_value[4];   // display range maximum
    float          step;           // slider step size
    StringId       display_name;   // "Roughness" (for UI)
    StringId       tooltip;        // "0 = mirror, 1 = fully rough"
    ParamFlags     flags;          // IS_COLOR, IS_HDR, IS_HIDDEN
};

struct TextureSlotDef {
    StringId       name;           // "albedo_texture"
    uint32_t       register_index; // t0, t1, etc.
    TextureFormat  expected_format; // BC7_SRGB, BC5_UNORM, etc.
    bool           is_srgb;
    StringId       display_name;   // "Albedo Map"
};

struct MaterialSchema {
    StringId                   shader_path;
    Array<ParamDef>            params;
    Array<TextureSlotDef>      texture_slots;
    uint32_t                   cbuffer_size;    // total byte size of material cbuffer

    // Extracted from DXC reflection after shader compilation
    static MaterialSchema extract(IDxcBlob* vs_blob, IDxcBlob* ps_blob);
};
```

### Material Instance Data Model

```cpp
// src/renderer/material/material_instance.h

struct MaterialInstance {
    StringId                   name;
    StringId                   path;           // "res://materials/skin_base.uzmat"
    Handle<MaterialInstance>   parent;         // null for master-level instances
    Handle<MaterialSchema>     schema;         // from the compiled shader

    // Override maps: only keys that differ from parent are stored
    HashMap<StringId, float[4]>        param_overrides;
    HashMap<StringId, Handle<Texture>> texture_overrides;

    // Layer stack (empty if no layering)
    Array<MaterialLayer>               layers;

    // Resolved values (computed at load time / on change)
    struct Resolved {
        Array<float>            cbuffer_data;    // packed cbuffer ready for GPU upload
        Array<Handle<Texture>>  textures;        // ordered by register index
        ShaderVariantKey        variant_key;     // auto-computed from active textures
    } resolved;

    // Resolve inheritance chain: walk parents, apply overrides
    void resolve();

    // Serialize to / deserialize from .uzmat text format
    void save(const char* path) const;
    static MaterialInstance load(const char* path, MaterialRegistry* reg);
};
```

### Inheritance Resolution

When `resolve()` is called:

```
1. Start with parent's resolved cbuffer_data (or schema defaults if no parent).
2. For each entry in param_overrides:
   a. Find the param's cbuffer_offset from schema.
   b. Write override value at that offset.
3. For each entry in texture_overrides:
   a. Find the texture slot's register index from schema.
   b. Replace the texture handle at that index.
4. Recompute ShaderVariantKey based on which texture slots are non-null.
5. Cache the resolved data for GPU upload.
```

Resolution is triggered on:
- Material load
- Parent material change
- Any parameter or texture override change
- Layer stack modification

---

## Multipart Character Material Workflow

### Concept

A "multipart character" is a skeletal mesh with multiple submeshes (mesh pieces), each assigned to one or more material slots. A typical RPG character might have:

```
Character: "HeroKnight"
Skeleton:  hero_knight.uzskel (67 bones)

Mesh Pieces (submeshes in .uzmesh):
  [0] Head       -> Material Slot 0: skin_head.uzmat
                    Material Slot 1: eye_left.uzmat
                    Material Slot 2: eye_right.uzmat
  [1] Torso      -> Material Slot 3: armor_torso.uzmat
  [2] Arms       -> Material Slot 4: skin_arms.uzmat
                    Material Slot 5: armor_bracers.uzmat
  [3] Legs       -> Material Slot 6: armor_legs.uzmat
                    Material Slot 7: skin_legs.uzmat (under-armor)
  [4] Hair       -> Material Slot 8: hair.uzmat
  [5] Cape       -> Material Slot 9: cape_cloth.uzmat
  [6] Weapon     -> Material Slot 10: weapon_blade.uzmat
                    Material Slot 11: weapon_handle.uzmat
```

### Material Slot List Detail

```
MATERIAL SLOTS
+----------------------------------------------+
| Mesh Piece    | Slot | Material          | UV |
|---------------|------|-------------------|----|
| > Head        |  #0  | skin_head.uzmat   | 0  |
|               |  #1  | eye_left.uzmat    | 0  |
|               |  #2  | eye_right.uzmat   | 0  |
| > Torso       |  #3  | armor_torso.uzmat | 0  |
| > Arms        |  #4  | skin_arms.uzmat   | 0  |
|               |  #5  | armor_bracers     | 0  |
| > Legs        |  #6  | armor_legs.uzmat  | 0  |
|               |  #7  | skin_legs.uzmat   | 0  |
| > Hair        |  #8  | hair.uzmat        | 0  |
| > Cape        |  #9  | cape_cloth.uzmat  | 0  |
| > Weapon      | #10  | weapon_blade      | 0  |
|               | #11  | weapon_handle     | 0  |
+----------------------------------------------+
```

### UV Channel Assignment

Each material slot can specify which UV channel to use for sampling. The `.uzmesh` format supports up to 4 UV channels per vertex:

| UV Channel | Purpose | Typical Use |
|------------|---------|-------------|
| `uv0` | Main texture coordinates | Albedo, normal, PBR maps |
| `uv1` | Lightmap UV | Baked lightmaps (unique, non-overlapping) |
| `uv2` | Detail UV | Tiling detail normal / noise overlay |
| `uv3` | Reserved | Custom shader use |

The UV channel is set per-slot in the `.uzmat` file via the `uv_channel` parameter, and the shader variant system generates the appropriate `UV_CHANNEL=N` define.

### Shared Texture Atlases

For characters, it is common to pack all body part textures into a single atlas to reduce texture binds and enable instancing of character variants. The Material Editor supports this workflow:

1. **Atlas indicator**: When multiple material slots reference the same texture file, the Texture Slots panel shows a "Shared" badge and the atlas tile boundary.
2. **UV offset/scale per slot**: Each slot can define `uv_offset` and `uv_scale` to select its region within the atlas.
3. **Atlas preview**: Clicking "Show Atlas" overlays the UV layout on the texture preview, highlighting which region belongs to the selected slot.

```ini
; skin_head.uzmat -- uses a shared body atlas
[uzmaterial version=1]
shader = "res://shaders/builtin/pbr.hlsl"
parent = "res://materials/skin_base.uzmat"

albedo_texture = "res://textures/hero_body_atlas.png"
uv_offset      = [0.0, 0.0]
uv_scale       = [0.5, 0.5]     ; head occupies top-left quadrant
```

### Dye / Color Variation System

The dye system enables runtime color customization without duplicating textures. It works via **tint channels** -- up to 4 independent HSV color shifts applied to masked regions of the albedo.

```
Tint Channel Map: RGBA8 texture where each channel is a mask
  R = Tint Channel 0 (e.g., primary armor color)
  G = Tint Channel 1 (e.g., secondary accent color)
  B = Tint Channel 2 (e.g., cloth/cape color)
  A = Tint Channel 3 (e.g., trim/emblem color)
```

Each tint channel has three parameters:

| Parameter | Type | Range | Description |
|-----------|------|-------|-------------|
| `tint_N_hue` | float | -180..180 | Hue shift in degrees |
| `tint_N_saturation` | float | -1..1 | Saturation adjustment (-1 = desaturate, +1 = oversaturate) |
| `tint_N_value` | float | -1..1 | Value/brightness adjustment |

HLSL implementation:

```hlsl
// shaders/include/dye_system.hlsli
#ifndef UZ_DYE_SYSTEM_HLSLI
#define UZ_DYE_SYSTEM_HLSLI

// Convert RGB to HSV
float3 rgb_to_hsv(float3 c) {
    float4 K = float4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
    float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
    float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// Convert HSV to RGB
float3 hsv_to_rgb(float3 c) {
    float4 K = float4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

// Apply a single tint channel
float3 apply_tint(float3 base_rgb, float mask, float hue_shift, float sat_adj, float val_adj) {
    if (mask < 0.01) return base_rgb;

    float3 hsv = rgb_to_hsv(base_rgb);
    hsv.x = frac(hsv.x + hue_shift / 360.0);           // hue shift (wrapping)
    hsv.y = saturate(hsv.y + sat_adj);                   // saturation adjust
    hsv.z = saturate(hsv.z + val_adj);                   // value adjust
    float3 tinted = hsv_to_rgb(hsv);

    return lerp(base_rgb, tinted, mask);                 // blend by mask intensity
}

// Apply all 4 tint channels from a tint channel map
float3 apply_dye_system(float3 base_rgb, float4 tint_mask,
                         float4 hue_shifts, float4 sat_adjs, float4 val_adjs) {
    float3 result = base_rgb;
    result = apply_tint(result, tint_mask.r, hue_shifts.x, sat_adjs.x, val_adjs.x);
    result = apply_tint(result, tint_mask.g, hue_shifts.y, sat_adjs.y, val_adjs.y);
    result = apply_tint(result, tint_mask.b, hue_shifts.z, sat_adjs.z, val_adjs.z);
    result = apply_tint(result, tint_mask.a, hue_shifts.w, sat_adjs.w, val_adjs.w);
    return result;
}

#endif // UZ_DYE_SYSTEM_HLSLI
```

Lua API for runtime dye changes:

```lua
-- In a Lua script attached to the character node
local mat = self.node:get_material(3)  -- armor_torso slot
mat:set_param("tint_0_hue", 120.0)     -- shift primary color to green
mat:set_param("tint_0_saturation", 0.2) -- slightly more saturated
mat:set_param("tint_1_hue", -30.0)     -- shift accent color toward red
```

---

## Material Layering

### Layer Architecture

Material layers provide a non-destructive way to add surface detail (dirt, wetness, damage, ice) on top of a base material. Layers are evaluated in the pixel shader after the base PBR parameters are computed, blending additional albedo, roughness, metallic, and normal contributions.

Each layer is self-contained: it has its own textures, blend mode, mask, and intensity. Layers are serialized as part of the `.uzmat` file.

### Layer Types

| Layer Type | Typical Effect | Default Blend | Default Textures |
|------------|---------------|---------------|-----------------|
| `base` | The PBR base material | N/A (always first) | Albedo, Normal, R/M, AO |
| `dirt` | Accumulated grime, dust | Multiply | Brown albedo + roughness increase |
| `wetness` | Rain, puddles, sweat | Overlay | Darkened albedo + roughness decrease + slight normal flatten |
| `blood` | Combat splatter | Additive | Dark red albedo + high roughness |
| `burn` | Fire/explosion damage | Multiply | Charred black albedo + high roughness |
| `ice` | Frozen surface | Overlay | Blue-white albedo + low roughness + ice normal |
| `damage` | Scratches, dents | Overlay | Reveal layer albedo + roughness variation + dent normals |

### Layer Data Structure

```cpp
// src/renderer/material/material_layer.h

enum class LayerBlendMode : uint8_t {
    MULTIPLY  = 0,   // result = base * layer
    OVERLAY   = 1,   // result = base < 0.5 ? 2*base*layer : 1 - 2*(1-base)*(1-layer)
    ADDITIVE  = 2,   // result = base + layer * intensity
    REPLACE   = 3,   // result = lerp(base, layer, mask * intensity)
};

enum class LayerMaskType : uint8_t {
    TEXTURE       = 0,   // explicit mask texture
    CURVATURE     = 1,   // procedural: edges/convex regions accumulate
    HEIGHT        = 2,   // procedural: Y-axis height gradient
    VERTEX_COLOR  = 3,   // mask from vertex color channel (R, G, B, or A)
    NOISE         = 4,   // procedural: Perlin/simplex noise mask
};

struct MaterialLayer {
    StringId            name;           // "dirt", "wetness", etc.
    LayerType           type;           // enum from table above
    bool                enabled;        // toggle on/off
    float               intensity;      // global blend intensity [0..1]

    // Per-layer textures (optional; nullptr means layer has no contribution for that channel)
    Handle<Texture>     albedo;         // layer color overlay
    Handle<Texture>     normal;         // layer normal detail
    Handle<Texture>     roughness;      // layer roughness modifier
    Handle<Texture>     metallic;       // layer metallic modifier

    // Blending
    LayerBlendMode      blend_mode;     // how this layer combines with layers below
    float               blend_falloff;  // edge softness of the mask [0.01 .. 1.0]

    // Mask
    LayerMaskType       mask_type;
    Handle<Texture>     mask_texture;   // used when mask_type == TEXTURE
    uint8_t             mask_channel;   // which channel of mask_texture (0=R, 1=G, 2=B, 3=A)
    float               mask_scale;     // UV tiling of the mask
    float               mask_offset;    // threshold offset for procedural masks

    // Tiling
    float2              uv_scale;       // UV tiling for layer textures
    float2              uv_offset;      // UV offset for layer textures
};
```

### Layer Stack Panel Detail

```
LAYER STACK
+--------------------------------------------------+
|  [drag] [x] Base                          1.0     |
|         Blend: ---  Mask: ---                     |
+--------------------------------------------------+
|  [drag] [x] Dirt                          0.3     |
|         Blend: Multiply  Mask: Curvature          |
|         > Albedo: dirt_overlay.png                |
|         > Roughness: +0.2 (additive)             |
|         > Mask Scale: 2.0  Threshold: 0.4        |
+--------------------------------------------------+
|  [drag] [ ] Wetness                       0.0     |
|         Blend: Overlay  Mask: Height              |
|         > Albedo: darken -0.15                    |
|         > Roughness: -0.4 (smooth when wet)       |
|         > Height Min: 0.0  Height Max: 0.3       |
+--------------------------------------------------+
|  [drag] [ ] Blood                         0.0     |
|         Blend: Additive  Mask: Texture            |
|         > Albedo: blood_splatter.png              |
|         > Roughness: +0.3                         |
|         > Mask: blood_mask.png (R channel)        |
+--------------------------------------------------+
|  [+ Add Layer v]                                  |
+--------------------------------------------------+
```

### Procedural Masks

**Curvature-based (Edge Wear)**: Computed from the mesh's vertex curvature or a curvature map baked during import. Convex edges accumulate the layer (e.g., paint chipping on armor edges, dirt in crevices).

```hlsl
// Curvature mask: high curvature = more layer
float curvature_mask(float3 world_normal, float3 world_pos, float threshold) {
    // Approximate curvature from screen-space normal derivatives
    float3 dNdx = ddx(world_normal);
    float3 dNdy = ddy(world_normal);
    float curvature = length(dNdx) + length(dNdy);
    return smoothstep(threshold, threshold + 0.2, curvature);
}
```

**Height-based (Puddle Accumulation)**: Uses the world-space Y coordinate to apply the layer below a height threshold. Useful for puddles (wetness at ground level) or snow (accumulation on upward-facing surfaces).

```hlsl
// Height mask: world Y below threshold gets the layer
float height_mask(float world_y, float min_height, float max_height) {
    return 1.0 - smoothstep(min_height, max_height, world_y);
}

// Top-facing mask: surfaces pointing up accumulate (snow, dust)
float top_facing_mask(float3 world_normal, float threshold) {
    return smoothstep(threshold, 1.0, dot(world_normal, float3(0, 1, 0)));
}
```

### Layer Blending HLSL

```hlsl
// shaders/include/material_layers.hlsli
#ifndef UZ_MATERIAL_LAYERS_HLSLI
#define UZ_MATERIAL_LAYERS_HLSLI

#include "common.hlsli"

// Blend modes for material layers
float3 blend_multiply(float3 base, float3 layer) {
    return base * layer;
}

float3 blend_overlay(float3 base, float3 layer) {
    // Per-channel overlay: dark stays dark, bright stays bright
    float3 result;
    result.r = base.r < 0.5 ? (2.0 * base.r * layer.r) : (1.0 - 2.0 * (1.0 - base.r) * (1.0 - layer.r));
    result.g = base.g < 0.5 ? (2.0 * base.g * layer.g) : (1.0 - 2.0 * (1.0 - base.g) * (1.0 - layer.g));
    result.b = base.b < 0.5 ? (2.0 * base.b * layer.b) : (1.0 - 2.0 * (1.0 - base.b) * (1.0 - layer.b));
    return result;
}

float3 blend_additive(float3 base, float3 layer) {
    return base + layer;
}

// Apply a single layer to PBR parameters
void apply_layer(inout float3 albedo, inout float roughness, inout float metallic,
                 float3 layer_albedo, float layer_roughness, float layer_metallic,
                 float mask, float intensity, uint blend_mode) {
    float alpha = mask * intensity;
    if (alpha < 0.001) return;

    // Albedo blending
    float3 blended_albedo;
    if (blend_mode == 0)      blended_albedo = blend_multiply(albedo, layer_albedo);
    else if (blend_mode == 1) blended_albedo = blend_overlay(albedo, layer_albedo);
    else if (blend_mode == 2) blended_albedo = blend_additive(albedo, layer_albedo);
    else                      blended_albedo = layer_albedo;  // REPLACE

    albedo    = lerp(albedo, blended_albedo, alpha);
    roughness = lerp(roughness, layer_roughness, alpha);
    metallic  = lerp(metallic, layer_metallic, alpha);
}

#endif // UZ_MATERIAL_LAYERS_HLSLI
```

### Runtime Layer Control (Lua API)

```lua
-- Set layer intensity at runtime (e.g., character enters rain)
local mat = self.node:get_material(0)
mat:set_layer_intensity("wetness", 0.8)

-- Animate layer fade-in over time
function on_update(dt)
    local wet = mat:get_layer_intensity("wetness")
    if self.in_rain and wet < 1.0 then
        mat:set_layer_intensity("wetness", math.min(wet + dt * 0.3, 1.0))
    elseif not self.in_rain and wet > 0.0 then
        mat:set_layer_intensity("wetness", math.max(wet - dt * 0.1, 0.0))
    end
end

-- Toggle blood splatter on hit
function on_event_damage(event)
    mat:set_layer_intensity("blood", 1.0)
    -- Fade out over 5 seconds via timer
    ctx:schedule_after(5.0, function()
        mat:set_layer_intensity("blood", 0.0)
    end)
end
```

---

## Texture Management

### Texture Import Settings (In-Panel)

When a texture is assigned to a slot, the Texture Slots panel shows an expandable section with import settings. Changes here trigger a reimport via the asset pipeline.

| Setting | Values | Default | Description |
|---------|--------|---------|-------------|
| Color Space | sRGB / Linear | Auto (sRGB for albedo/emissive, linear for others) | Gamma interpretation |
| Mip Generation | On / Off | On | Generate full mip chain |
| Mip Filter | Box / Kaiser / Linear | Box (albedo), Linear (normals) | Downsampling filter |
| Compression | BC1 / BC3 / BC4 / BC5 / BC7 / None | Auto per slot type | Block compression format |
| Max Size | 256 / 512 / 1024 / 2048 / 4096 | 2048 | Maximum resolution (downscales if source is larger) |
| Flip Y | On / Off | Off | Vertical flip for DirectX convention |
| Premultiply Alpha | On / Off | Off | Premultiply alpha channel into RGB |

### Texture Channel Packing Editor

The channel packing editor lets artists combine multiple grayscale textures into a single multi-channel texture, reducing texture bind count and memory bandwidth.

**Standard Packing Presets**:

| Packed Texture | R | G | B | A | Format | Register |
|----------------|---|---|---|---|--------|----------|
| `ORM` (default) | Ambient Occlusion | Roughness | Metallic | (unused / 1.0) | BC7 UNORM | t2 |
| `MRAO` | Metallic | Roughness | AO | Opacity | BC7 UNORM | t2 |
| `Normal+Height` | Normal X | Normal Y | Height | (unused) | BC7 UNORM | t1 |
| `Emissive+Mask` | Emissive R | Emissive G | Emissive B | Emission Mask | BC7 SRGB | t5 |

Channel packing UI:

```
CHANNEL PACKING EDITOR
+--------------------------------------------------+
| Output: ORM_packed.png                           |
| Format: BC7_UNORM  Size: 1024x1024              |
|                                                  |
|  Output R: [AO Map      v]  [ao_baked.png    ]  |
|  Output G: [Roughness   v]  [rough_map.png   ]  |
|  Output B: [Metallic    v]  [metal_mask.png  ]  |
|  Output A: [Constant    v]  [1.0             ]  |
|                                                  |
|  [Preview]  [Pack & Save]                        |
+--------------------------------------------------+
```

When channel packing is used, the shader variant system activates `PACKED_ORM` (or `PACKED_MRAO`, etc.) so the pixel shader unpacks the correct channels.

### UDIM Tile Viewer

For high-resolution characters that use UDIM tiling (multiple UV tiles, each with its own texture), the editor provides a UDIM overview:

```
UDIM TILES (hero_knight body)
+--------+--------+--------+
| 1001   | 1002   | 1003   |
| Head   | Torso  | Arms   |
| 2048x  | 2048x  | 2048x  |
+--------+--------+--------+
| 1011   | 1012   |
| Legs   | Hands  |
| 2048x  | 1024x  |
+--------+--------+
```

UDIM tiles are referenced in `.uzmat` as:

```ini
albedo_texture = "res://textures/hero_body.<UDIM>.png"
; Engine resolves: hero_body.1001.png, hero_body.1002.png, etc.
```

### Texture Set Grouping

Textures that belong together (e.g., all maps for a head material) can be grouped as a "Texture Set" for organizational purposes:

```
TEXTURE SETS
+------------------------------------------+
| > Head_Set                               |
|   Albedo:    hero_head_albedo.png        |
|   Normal:    hero_head_normal.png        |
|   ORM:       hero_head_orm.png           |
|   Emissive:  (none)                      |
|                                          |
| > Armor_Set                              |
|   Albedo:    armor_albedo.png            |
|   Normal:    armor_normal.png            |
|   ORM:       armor_orm.png              |
|   Tint Mask: armor_tint_channels.png     |
+------------------------------------------+
```

Texture sets are defined in a sidecar `.uztexset` file:

```ini
[uztextureset version=1 name="Head_Set"]
albedo  = "res://textures/hero_head_albedo.png"
normal  = "res://textures/hero_head_normal.png"
orm     = "res://textures/hero_head_orm.png"
```

Dragging a texture set onto a material slot auto-assigns all matching textures.

---

## LOD Material Management

### LOD Chain Strategy

Each LOD level can reference a different material instance, allowing progressive material simplification for performance:

| LOD | Typical Configuration | Texture Resolution | Shader Complexity |
|-----|----------------------|-------------------|-------------------|
| LOD0 | Full PBR: all texture maps, normal mapping, all layers | Full (2048x2048) | Full uber-shader |
| LOD1 | Simplified PBR: albedo + ORM packed, no separate maps | Half (1024x1024) or mip bias +1 | Reduced variant (fewer texture fetches) |
| LOD2 | Minimal: vertex color or flat color, no texture sampling | N/A (vertex-colored) | Minimal shader |
| LOD3 | Impostor or culled | Billboard / none | Single-quad shader or culled |

### LOD Material Instances

LOD material instances inherit from the base material and override the `lod_` prefixed settings:

```ini
; materials/skin_head_lod1.uzmat
[uzmaterial version=1]
parent = "res://materials/skin_head.uzmat"
shader = "res://shaders/builtin/pbr.hlsl"

; Override textures to lower resolution (or use mip bias)
lod_texture_mip_bias = 1       ; force mip level +1 (half resolution)
lod_disable_layers   = true    ; skip all material layers for perf

; Optionally disable specific texture slots
normal_texture       = ""      ; remove normal map at LOD1
```

```ini
; materials/skin_head_lod2.uzmat
[uzmaterial version=1]
parent = "res://materials/skin_head.uzmat"
shader = "res://shaders/builtin/pbr_notex.hlsl"

; Flat color from vertex colors, no textures
albedo          = [0.85, 0.65, 0.55]     ; average skin tone
roughness       = 0.7
metallic        = 0.0
```

### LOD Crossfade (Dithered)

To avoid visible LOD pops, the renderer supports dithered crossfade between LOD levels. When a mesh is transitioning between two LODs, both are rendered simultaneously with complementary dither patterns that sum to full coverage.

The shader variant system adds `LOD_CROSSFADE` when dithering is active:

```hlsl
#ifdef LOD_CROSSFADE
// Dithered alpha test for LOD transition
// lod_fade is [0..1], 0 = fully this LOD, 1 = fully next LOD
float dither = frac(dot(floor(input.pos.xy), float2(1.0/4.0, 3.0/4.0)) + g_Time * 0.1);
clip(lod_fade - dither);
#endif
```

### LOD Material Assignment Table

The LOD-to-material mapping is stored per submesh in the mesh asset's metadata:

```ini
; In the .uzscene node, or .uzmesh metadata
[lod_materials slot=0]
lod0 = "res://materials/skin_head.uzmat"
lod1 = "res://materials/skin_head_lod1.uzmat"
lod2 = "res://materials/skin_head_lod2.uzmat"

[lod_materials slot=3]
lod0 = "res://materials/armor_torso.uzmat"
lod1 = "res://materials/armor_torso_lod1.uzmat"
lod2 = "res://materials/armor_generic_lod2.uzmat"
```

---

## File Format (.uzmat)

### Format Rules

- **INI-style** with `[section]` headers and `key = value` pairs.
- **Human-readable** and **AI-editable** (LLMs can generate and modify `.uzmat` files).
- **UTF-8** encoding, line endings `\n` or `\r\n`.
- Comments start with `;` (semicolon).
- Vectors use bracket notation: `[0.8, 0.3, 0.1]`.
- Boolean values: `true` / `false`.
- Paths use `res://` prefix for project-relative paths.
- Section order does not matter, but the convention is: header, parameters, textures, variants, layers.

### Grammar

```
file         = header params? textures? variants? layers*
header       = "[uzmaterial" kvpairs "]"
kvpairs      = (key "=" value)*        ; on the [section] line
params       = (param_key "=" value NL)*
textures     = (texture_key "=" path NL)*
variants     = "[variants]" NL (define_key "=" value NL)*
layers       = "[layer" kvpairs "]" NL (layer_key "=" value NL)*

value        = number | bool | string | vector
vector       = "[" number ("," number)* "]"
path         = quoted_string           ; "res://..."
```

### Example: Character Skin Material

```ini
; =============================================================
; materials/characters/hero_knight/skin_head.uzmat
; Character skin material for hero knight head mesh piece.
; Parent: skin_base provides default PBR skin parameters.
; This instance overrides textures for the head specifically.
; =============================================================

[uzmaterial version=1 name="HeroKnight_Skin_Head"]
shader = "res://shaders/builtin/pbr.hlsl"
parent = "res://materials/characters/skin_base.uzmat"

; --- PBR parameter overrides ---
albedo              = [0.85, 0.65, 0.55]    ; warm skin base tint
roughness           = 0.55                   ; skin is slightly glossy
metallic            = 0.0                    ; skin is dielectric
normal_scale        = 1.2                    ; slightly exaggerated pore detail
ao_intensity        = 0.8                    ; soften baked AO
alpha_mode          = OPAQUE

; --- Texture assignments ---
albedo_texture      = "res://textures/characters/hero_knight/head_albedo.png"
normal_texture      = "res://textures/characters/hero_knight/head_normal.png"
roughness_texture   = "res://textures/characters/hero_knight/head_roughness.png"
ao_texture          = "res://textures/characters/hero_knight/head_ao.png"

; --- Subsurface scattering hint (future shader feature) ---
; sss_radius        = [1.0, 0.3, 0.1]       ; red scatters most (skin)
; sss_intensity     = 0.5

; --- Dye system ---
tint_texture        = "res://textures/characters/hero_knight/head_tint_mask.png"
tint_0_hue          = 0.0           ; no hue shift by default
tint_0_saturation   = 0.0
tint_0_value        = 0.0

; --- UV ---
uv_channel          = 0
uv_offset           = [0.0, 0.0]
uv_scale            = [1.0, 1.0]

; --- Shader variant overrides ---
[variants]
ENABLE_DYE_SYSTEM   = 1

; --- Material layers ---
[layer name="dirt" type=dirt enabled=false]
intensity           = 0.0
blend_mode          = multiply
albedo_texture      = "res://textures/shared/dirt_overlay.png"
roughness_offset    = 0.15
mask_type           = curvature
mask_threshold      = 0.3
mask_scale          = 2.0
uv_scale            = [3.0, 3.0]

[layer name="blood" type=blood enabled=false]
intensity           = 0.0
blend_mode          = additive
albedo_texture      = "res://textures/shared/blood_splatter.png"
roughness_offset    = 0.25
mask_type           = texture
mask_texture        = "res://textures/shared/blood_mask.png"
mask_channel        = 0
uv_scale            = [1.0, 1.0]

[layer name="wetness" type=wetness enabled=false]
intensity           = 0.0
blend_mode          = overlay
albedo_tint         = [0.7, 0.7, 0.75]      ; darken and cool
roughness_offset    = -0.35                   ; wet = smoother
mask_type           = height
mask_height_min     = 0.0
mask_height_max     = 0.4
```

### Example: Layered Environment Material

```ini
; =============================================================
; materials/environment/stone_wall_mossy.uzmat
; Layered stone wall material with moss and wetness.
; Demonstrates multi-layer environment workflow.
; =============================================================

[uzmaterial version=1 name="StoneWall_Mossy"]
shader = "res://shaders/builtin/pbr.hlsl"

; --- Base PBR parameters ---
albedo              = [0.6, 0.58, 0.55]     ; neutral stone gray
roughness           = 0.85                   ; rough stone
metallic            = 0.0                    ; dielectric
normal_scale        = 1.0
ao_intensity        = 1.0
alpha_mode          = OPAQUE

; --- Texture assignments ---
albedo_texture      = "res://textures/environment/stone_wall_albedo.png"
normal_texture      = "res://textures/environment/stone_wall_normal.png"
roughness_texture   = "res://textures/environment/stone_wall_roughness.png"
ao_texture          = "res://textures/environment/stone_wall_ao.png"

; --- UV ---
uv_channel          = 0
uv_scale            = [2.0, 2.0]    ; tile 2x on this mesh

; --- Shader variant overrides ---
[variants]
ENABLE_LAYERS       = 1
MAX_LAYERS          = 3

; --- Layer: Moss (always present on this wall) ---
[layer name="moss" type=dirt enabled=true]
intensity           = 0.65
blend_mode          = overlay
albedo_texture      = "res://textures/shared/moss_albedo.png"
normal_texture      = "res://textures/shared/moss_normal.png"
roughness_offset    = -0.1                   ; moss is slightly smoother than stone
mask_type           = curvature
mask_threshold      = 0.2                    ; accumulates in crevices
mask_scale          = 1.5
uv_scale            = [4.0, 4.0]            ; moss tiles at higher frequency

; --- Layer: Wetness (runtime-controllable) ---
[layer name="wetness" type=wetness enabled=false]
intensity           = 0.0
blend_mode          = overlay
albedo_tint         = [0.6, 0.6, 0.65]
roughness_offset    = -0.5                   ; very smooth when wet
mask_type           = height
mask_height_min     = 0.0
mask_height_max     = 1.2                    ; puddles accumulate at bottom

; --- Layer: Ice (runtime-controllable, e.g., frost spell) ---
[layer name="ice" type=ice enabled=false]
intensity           = 0.0
blend_mode          = overlay
albedo_texture      = "res://textures/shared/ice_albedo.png"
normal_texture      = "res://textures/shared/ice_normal.png"
roughness_offset    = -0.7                   ; ice is very smooth
metallic_offset     = 0.1                    ; slight metallic sheen
mask_type           = noise
mask_scale          = 3.0
mask_offset         = 0.0                    ; noise threshold
uv_scale            = [2.0, 2.0]
```

### Full .uzmat Key Reference

| Key | Section | Type | Description |
|-----|---------|------|-------------|
| `version` | header | int | Format version (currently 1) |
| `name` | header | string | Human-readable material name |
| `shader` | root | path | Path to the master shader `.hlsl` file |
| `parent` | root | path | Path to parent material instance (optional) |
| `albedo` | root | float3 | Base color multiplier (linear) |
| `roughness` | root | float | Roughness value [0..1] |
| `metallic` | root | float | Metallic value [0..1] |
| `normal_scale` | root | float | Normal map intensity [0..2] |
| `ao_intensity` | root | float | AO map intensity [0..1] |
| `emission` | root | float3 | Emissive color (HDR) |
| `alpha_mode` | root | enum | OPAQUE / MASK / BLEND |
| `alpha_cutoff` | root | float | Cutoff for MASK mode [0..1] |
| `albedo_texture` | root | path | Albedo map |
| `normal_texture` | root | path | Normal map |
| `roughness_texture` | root | path | Roughness map |
| `metallic_texture` | root | path | Metallic map |
| `ao_texture` | root | path | Ambient occlusion map |
| `emission_texture` | root | path | Emissive map |
| `opacity_texture` | root | path | Opacity mask |
| `tint_texture` | root | path | Dye system tint channel mask (RGBA) |
| `tint_N_hue` | root | float | Hue shift for tint channel N [-180..180] |
| `tint_N_saturation` | root | float | Saturation adjust for channel N [-1..1] |
| `tint_N_value` | root | float | Value adjust for channel N [-1..1] |
| `uv_channel` | root | int | UV channel index [0..3] |
| `uv_offset` | root | float2 | UV offset |
| `uv_scale` | root | float2 | UV tiling scale |
| `lod_texture_mip_bias` | root | int | Forced mip level bias for LOD instances |
| `lod_disable_layers` | root | bool | Disable all layers (LOD perf optimization) |
| `intensity` | layer | float | Layer blend intensity [0..1] |
| `blend_mode` | layer | enum | multiply / overlay / additive / replace |
| `albedo_texture` | layer | path | Layer color overlay texture |
| `normal_texture` | layer | path | Layer normal detail texture |
| `albedo_tint` | layer | float3 | Layer color tint (used if no texture) |
| `roughness_offset` | layer | float | Additive roughness modifier [-1..1] |
| `metallic_offset` | layer | float | Additive metallic modifier [-1..1] |
| `mask_type` | layer | enum | texture / curvature / height / vertex_color / noise |
| `mask_texture` | layer | path | Explicit mask texture |
| `mask_channel` | layer | int | Channel of mask texture [0=R, 1=G, 2=B, 3=A] |
| `mask_scale` | layer | float | UV tiling of the mask / noise frequency |
| `mask_threshold` | layer | float | Procedural mask threshold [0..1] |
| `mask_height_min` | layer | float | Height mask minimum (world Y) |
| `mask_height_max` | layer | float | Height mask maximum (world Y) |
| `uv_scale` | layer | float2 | Per-layer texture UV tiling |
| `uv_offset` | layer | float2 | Per-layer texture UV offset |

---

## Real-Time Preview Features

### Animated Character Preview

The 3D preview viewport supports displaying the character in various poses:

| Mode | Description | Use Case |
|------|-------------|----------|
| **T-Pose** | Default bind pose, arms extended | Best for inspecting material coverage, UV seams |
| **A-Pose** | Arms at 45 degrees (natural rest) | Better for shoulder/armpit material inspection |
| **Idle** | Loops the first animation clip tagged `idle` | Verify materials under motion, skin deformation |
| **Custom Clip** | Select any `.uzanim` file from dropdown | Inspect specific poses (combat, crouch, etc.) |
| **Turntable** | Auto-rotates the model, any pose | Quick visual review from all angles |

The animation system uses the full GPU skinning pipeline (morph targets + bone palette) so the material preview is pixel-identical to in-game rendering.

### Before/After Comparison (A/B Mode)

1. Artist clicks "Set A" to capture the current material state.
2. Continues editing parameters, textures, or layers.
3. Toggles A/B in the viewport to see the original ("A") vs current ("B") side-by-side.
4. A vertical split slider can be dragged across the viewport.

Implementation: "A" state is a snapshot of the material's resolved cbuffer + texture bindings. The engine renders two passes into the same viewport with a stencil mask for the split.

### Texture Channel Isolation

Each texture slot in the Texture Slots panel has channel buttons `[R] [G] [B] [A]` that, when clicked, render the mesh with only that channel visible as a grayscale output. This is essential for:

- Inspecting individual channels of packed ORM textures
- Verifying normal map X/Y/Z quality
- Checking mask textures before use in layers
- Debugging alpha channel issues

The isolation is done by setting a temporary pixel shader override:

```hlsl
// Preview override: show single channel as grayscale
float4 PSMain_ChannelIsolate(VSOut i) : SV_TARGET {
    float4 tex = SelectedTexture.Sample(Sampler, i.uv);
    float  val = tex[g_ChannelIndex]; // 0=R, 1=G, 2=B, 3=A
    return float4(val, val, val, 1.0);
}
```

### Specular Highlight Probe

Overlay mode that renders a small, perfect sphere next to the character using the same material. The sphere provides a controlled-geometry reference for evaluating:

- Roughness response (sharp vs diffuse highlight)
- Metallic appearance (colored vs white specular)
- Fresnel effect at grazing angles
- Normal map influence (sphere has no normal map by default)

---

## Implementation Notes

### C++ Class Hierarchy

```
MaterialSystem
 |
 +-- MaterialSchemaRegistry
 |    Stores MaterialSchema per compiled shader.
 |    Updated on shader compile/hot-reload.
 |    Key: shader_path -> MaterialSchema
 |
 +-- MaterialRegistry
 |    Owns all loaded MaterialInstance objects.
 |    Handles inheritance resolution.
 |    Key: material_path -> MaterialInstance*
 |    Emits events on material change (for preview refresh).
 |
 +-- MaterialInstanceCache
 |    GPU-side cache of resolved material cbuffers.
 |    Double-buffered: current frame + previous frame.
 |    Invalidated when any parameter in the chain changes.
 |    Key: material_handle -> GpuBuffer* (cbuffer upload)
 |
 +-- TextureSlotBinder
 |    Binds resolved textures to descriptor heap slots.
 |    Handles fallback textures (white, flat normal, black).
 |    Key: (material_handle, slot_index) -> descriptor_index
 |
 +-- MaterialLayerEvaluator
 |    Evaluates layer stack at runtime.
 |    Uploads per-layer data to a StructuredBuffer.
 |    Manages procedural mask computation (curvature, height).
 |
 +-- DyeSystemManager
      Manages per-character dye channel overrides.
      Uploads tint parameters to material cbuffer.
      Supports batch updates for color-variant characters.
```

### Key C++ Structures

```cpp
// src/renderer/material/material_registry.h

class MaterialRegistry {
public:
    // Load a .uzmat file (and its parent chain)
    Handle<MaterialInstance> load(const char* path);

    // Create a new material instance with the given parent
    Handle<MaterialInstance> create_instance(const char* name,
                                             Handle<MaterialInstance> parent);

    // Get resolved material data for rendering
    const MaterialInstance::Resolved& get_resolved(Handle<MaterialInstance> mat);

    // Set a parameter override (triggers re-resolve)
    void set_param(Handle<MaterialInstance> mat, StringId name, const float* value, uint32_t count);

    // Set a texture override
    void set_texture(Handle<MaterialInstance> mat, StringId name, Handle<Texture> tex);

    // Layer operations
    void add_layer(Handle<MaterialInstance> mat, const MaterialLayer& layer);
    void remove_layer(Handle<MaterialInstance> mat, uint32_t index);
    void set_layer_intensity(Handle<MaterialInstance> mat, StringId layer_name, float intensity);
    void reorder_layers(Handle<MaterialInstance> mat, uint32_t from, uint32_t to);

    // Hot-reload: re-resolve all instances that reference a changed shader
    void on_shader_recompiled(StringId shader_path);

    // Serialization
    void save(Handle<MaterialInstance> mat, const char* path);

private:
    HashMap<StringId, MaterialInstance*>  materials_;
    HandlePool<MaterialInstance>          pool_;
    MaterialSchemaRegistry*              schemas_;
    Arena*                               arena_;
};
```

```cpp
// src/renderer/material/texture_slot_binder.h

class TextureSlotBinder {
public:
    // Bind all textures for a material to the descriptor heap
    // Returns the GPU descriptor handle for the SRV table
    D3D12_GPU_DESCRIPTOR_HANDLE bind(Handle<MaterialInstance> mat,
                                      ID3D12DescriptorHeap* heap,
                                      uint32_t heap_offset);

    // Fallback textures (loaded once at init)
    Handle<Texture> white_1x1;       // default albedo, AO
    Handle<Texture> flat_normal;     // (0.5, 0.5, 1.0) default normal
    Handle<Texture> black_1x1;       // default emissive, metallic
    Handle<Texture> default_rough;   // 0.5 roughness

private:
    TextureStreamer* streamer_;
};
```

### Texture Streaming Integration

Materials reference textures by path. The TextureStreamer loads textures on demand and manages mip streaming:

1. When a material is loaded, referenced textures are queued for streaming.
2. Until the full texture is resident, a low-resolution placeholder (64x64, always in memory) is used.
3. The `TextureSlotBinder` checks residency status and binds the highest available mip.
4. When a material is no longer referenced by any visible mesh, its textures become candidates for eviction.

LOD material instances with `lod_texture_mip_bias` clamp the maximum streamed mip level, reducing memory for distant objects.

### VSCode WebView Integration

The Material Editor runs as a VSCode WebView panel with three communication channels:

```
+---------------------------+          +-------------------+
|   VSCode Extension        |   IPC    |   UzEngine.exe    |
|                           |<-------->|                   |
|  Material Editor WebView  |          |  DX12 Renderer    |
|  (HTML/CSS/TypeScript)    |          |  MaterialRegistry |
|                           |          |  JPEG Preview      |
+---------------------------+          +-------------------+
     |                                        |
     |  1. Parameter changes -> IPC ->        |
     |     material.setParam                  |
     |                                        |
     |  2. <- JPEG frame stream <-            |
     |     (preview viewport)                 |
     |                                        |
     |  3. Schema requests -> IPC ->          |
     |     material.getSchema                 |
```

The parameter panel, texture slot panel, and layer stack are all rendered in the WebView using a reactive framework (Preact or Svelte, lightweight). The 3D viewport is a `<canvas>` element receiving JPEG frames from the engine, identical to the main scene viewport.

### DX12 Render Pass for Preview

The material preview uses an isolated render context:

```cpp
// src/renderer/preview/material_preview_pass.h

struct MaterialPreviewPass {
    // Isolated scene: single mesh + skybox + IBL
    Handle<MeshBuffer>       preview_mesh;      // the character or prop being edited
    Handle<MaterialInstance>  active_material;   // currently selected slot's material
    Handle<Texture>           ibl_cubemap;       // current IBL environment preset
    Handle<Texture>           ibl_irradiance;    // pre-convolved irradiance
    Handle<Texture>           ibl_prefiltered;   // pre-filtered specular mips

    // Camera
    float orbit_yaw;
    float orbit_pitch;
    float orbit_distance;
    float3 orbit_target;

    // Render to an offscreen RT, then readback for JPEG
    void render(ID3D12GraphicsCommandList* cmd, const FrameContext& ctx);

    // View mode overrides
    ViewMode view_mode;    // PBR, WIREFRAME, ALBEDO_ONLY, NORMAL_ONLY, etc.
    int      channel_isolate;  // -1 = off, 0=R, 1=G, 2=B, 3=A
    bool     show_specular_probe;
};
```

### IPC Handlers (material.*)

```
material.open          { path }
                   --> { ok, schema, instance_data, layers }

material.save          { path }
                   --> { ok }

material.setParam      { path, name, value }
                   --> { ok }
                       (triggers preview refresh)

material.setTexture    { path, slot, texture_path }
                   --> { ok }
                       (triggers variant recompile if HAS_*_TEX changes)

material.getSchema     { shader_path }
                   --> { params: [...], texture_slots: [...] }

material.createInstance { name, parent_path, shader_path? }
                   --> { ok, path }

material.addLayer      { path, layer_config }
                   --> { ok }

material.removeLayer   { path, layer_index }
                   --> { ok }

material.setLayerIntensity { path, layer_name, intensity }
                   --> { ok }

material.reorderLayers { path, from_index, to_index }
                   --> { ok }

material.setPreviewMesh { mesh_path, skeleton_path? }
                   --> { ok }

material.setPreviewIBL  { preset }      ; "studio", "outdoor", "night", "lava", "snow"
                   --> { ok }

material.setPreviewMode { mode }         ; "pbr", "wireframe", "albedo", "normal", etc.
                   --> { ok }

material.setPreviewAnim { anim_path }    ; or "tpose", "apose", "idle"
                   --> { ok }

material.getSlots       { mesh_path }
                   --> { slots: [{ index, name, material_path, uv_channel }] }

material.assignSlot     { mesh_path, slot_index, material_path }
                   --> { ok }

material.packChannels   { output_path, channels: { r: path, g: path, b: path, a: path } }
                   --> { ok, output_path }
```

### Performance Budget

| Operation | Budget | Notes |
|-----------|--------|-------|
| Parameter change to preview | < 16ms | Single cbuffer upload + 1 frame render |
| Texture assignment to preview | < 200ms | Texture load (if not cached) + variant recompile (if needed) |
| Layer toggle to preview | < 16ms | Enable/disable flag, no recompile |
| Full material load + resolve | < 5ms | Parse `.uzmat` + walk parent chain + resolve |
| Schema extraction (DXC reflect) | < 50ms | Done once per shader compile, cached |
| Channel packing (CPU) | < 500ms | DirectXTex processing, 2048x2048 worst case |

---

## Related Specifications

- [Shader System](../specs/shader-system.md) -- Shader compilation, hot-reload, material-shader binding
- [Shader Variants](../specs/shader-variants.md) -- Variant keys, PSO cache, auto-detection of defines
- [Shader Library](../specs/shader-library.md) -- Shared HLSL includes, BRDF, lighting, global cbuffer
- [Asset Pipeline](../specs/asset-pipeline.md) -- Texture import, compression, `.uzmesh` format
- [Renderer](../specs/renderer.md) -- DX12 pipeline, G-Buffer layout, PBR parameters
- [Skeletal Mesh](../specs/skeletal-mesh.md) -- Skinned vertex layout, bone palette, morph targets
- [GPU Instancing](../specs/gpu-instancing.md) -- Instanced draw calls, `StructuredBuffer<float4x4>`
- [Screen-Space Effects](../specs/screen-space-effects.md) -- SSAO, SSR, TAA post-process
- [Shader Editor](shader-editor.md) -- Node-based shader graph editor (separate tool)
