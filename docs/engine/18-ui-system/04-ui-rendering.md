# UI System — Rendering Pipeline

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/ui/ui_render_pass.h/.cpp`, `engine/ui/font_atlas.h`
> **Depends on**: DX12 backend (§3), HTML/CSS engine (§18.2)

## Purpose

UI rendering converts the laid-out DOM tree into GPU draw calls. This involves:
- **Font atlas rendering** — SDF (signed distance field) glyphs
- **Texture atlasing** — batch icons/images
- **Shape rendering** — rectangles with rounded corners, shadows
- **Z-ordering & batching** — minimize draw calls
- **Efficient updates** — only re-render dirty regions

## UIPass (DX12 Render Pass)

### Integration with RHI

```cpp
class UIPass : public RenderPass {
private:
    ResourceHandle<Pipeline> rect_pipeline;      // solid rectangles
    ResourceHandle<Pipeline> text_pipeline;      // text rendering
    ResourceHandle<Pipeline> textured_quad_pipeline;  // images

    ResourceHandle<GpuBuffer> vertex_buffer;     // dynamic VB for all UI geometry
    ResourceHandle<GpuBuffer> index_buffer;
    ResourceHandle<GpuBuffer> constant_buffer;   // MVP matrices

    FontAtlas font_atlas;
    TextureAtlas texture_atlas;

public:
    void Render(RenderContext& ctx, const HtmlDocument& document) override;
    void UpdateDirtyRegions(const HtmlDocument& document);
};
```

### Rendering Process

```cpp
void UIPass::Render(RenderContext& ctx, const HtmlDocument& document) {
    // 1. Update font/texture atlases if dirty
    UpdateAtlases(document);

    // 2. Collect render commands from DOM
    std::vector<RenderCommand> commands;
    CollectRenderCommands(document.root, commands);

    // 3. Sort by z-order (back-to-front)
    std::sort(commands.begin(), commands.end(),
              [](const RenderCommand& a, const RenderCommand& b) {
                  return a.z_order < b.z_order;
              });

    // 4. Build batches (minimize state changes)
    std::vector<RenderBatch> batches = BuildBatches(commands);

    // 5. Upload geometry to GPU
    size_t vertex_offset = 0;
    for (const auto& batch : batches) {
        UploadBatchGeometry(batch, vertex_offset);
    }

    // 6. Render each batch
    ctx.SetPipeline(rect_pipeline);
    for (const auto& batch : batches) {
        switch (batch.type) {
            case BatchType::Rectangles:
                RenderRectangles(ctx, batch);
                break;
            case BatchType::Text:
                RenderText(ctx, batch);
                break;
            case BatchType::TexturedQuads:
                RenderTexturedQuads(ctx, batch);
                break;
        }
    }
}
```

## Shape Rendering — Rectangles

### Rectangle Geometry

Axis-aligned rectangles with optional rounded corners and shadows:

```cpp
struct RectCommand {
    Vec2 position;                  // top-left
    Vec2 size;
    Color color;
    float border_radius = 0.0f;
    Color border_color = {0, 0, 0, 0};  // no border
    float border_width = 0.0f;

    struct Shadow {
        Vec2 offset = {2, 2};
        float blur_radius = 4.0f;
        Color color = {0, 0, 0, 0.3f};
    };
    std::optional<Shadow> shadow;

    int z_order = 0;
};
```

### GPU Implementation

For simplicity, rectangles are rendered as **axis-aligned quads** with a shader that handles rounded corners:

```hlsl
// Vertex shader
cbuffer TransformBuffer : register(b0) {
    float4x4 projection;
};

struct VS_Input {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR;
    float border_radius : TEXCOORD1;
};

struct PS_Input {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 local_pos : TEXCOORD0;
    float border_radius : TEXCOORD1;
};

PS_Input main(VS_Input input) {
    PS_Input output;
    output.position = mul(float4(input.position, 0, 1), projection);
    output.color = input.color;
    output.local_pos = input.position;  // position within rectangle
    output.border_radius = input.border_radius;
    return output;
}

// Pixel shader — rounded rectangle SDF
float4 RoundedRectPS(PS_Input input) : SV_TARGET {
    float2 p = input.local_pos;
    float2 size = /* rectangle size */;
    float r = input.border_radius;

    // Signed distance from point to rounded rectangle
    float2 d = abs(p - size * 0.5f) - (size * 0.5f - r);
    float dist = length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f) - r;

    // Anti-aliased edge
    float edge = smoothstep(-1.0f, 1.0f, -dist);

    return input.color * edge;
}
```

### Box Shadows

Shadows are rendered as larger, blurred rectangles behind the main rect:

```cpp
if (rect_cmd.shadow) {
    RectCommand shadow_rect = rect_cmd;
    shadow_rect.position += rect_cmd.shadow->offset;
    shadow_rect.color = rect_cmd.shadow->color;
    shadow_rect.z_order -= 1;  // render before main rect

    // Apply blur post-pass (optional: skip for performance)
    ApplyGaussianBlur(shadow_rect, rect_cmd.shadow->blur_radius);

    commands.push_back(shadow_rect);
}
```

## Font Atlas (SDF)

### Glyph Generation

Characters are rendered offline using SDF (Signed Distance Field), which allows:
- **Crisp rendering at any scale** — vector-like sharpness
- **Efficient memory** — one SDF atlas for all sizes
- **Outline/glow effects** — simple shader tweaks

SDF generation (offline, at import time):

```cpp
class FontAtlas {
private:
    // SDF atlas: 2048x2048 pixels, one SDF per character
    Texture sdf_texture;
    std::map<uint32_t, GlyphMetrics> glyphs;  // codepoint → atlas region

public:
    struct GlyphMetrics {
        Vec4 uv_rect;           // atlas region (u_min, v_min, u_max, v_max)
        Vec2 size;              // glyph size in pixels (at reference size)
        Vec2 bearing;           // offset from baseline
        float advance;          // x offset to next glyph
    };

    /// Get glyph metrics (from cache or generate)
    const GlyphMetrics& GetGlyph(uint32_t codepoint, float size);
};
```

### Text Rendering

Text is rendered using the SDF atlas:

```cpp
struct TextCommand {
    std::string text;
    Vec2 position;
    float font_size;
    Color color;
    std::string font_id;      // which font?
    float opacity = 1.0f;
    int z_order = 0;

    TextAlign align = TextAlign::Left;
    float max_width = 0.0f;   // 0 = no wrapping
};

void UIPass::RenderText(RenderContext& ctx, const TextCommand& cmd) {
    FontAtlas& atlas = GetFontAtlas(cmd.font_id);

    std::vector<Vertex> vertices;
    float x = cmd.position.x;
    float y = cmd.position.y;

    for (uint32_t codepoint : Utf8Decode(cmd.text)) {
        const auto& metrics = atlas.GetGlyph(codepoint, cmd.font_size);

        // Quad for glyph
        float x_min = x + metrics.bearing.x;
        float y_min = y - metrics.bearing.y;
        float x_max = x_min + metrics.size.x;
        float y_max = y_min + metrics.size.y;

        vertices.push_back({
            .position = {x_min, y_min},
            .uv = {metrics.uv_rect.x, metrics.uv_rect.y},
            .color = cmd.color,
        });
        vertices.push_back({
            .position = {x_max, y_min},
            .uv = {metrics.uv_rect.z, metrics.uv_rect.y},
            .color = cmd.color,
        });
        // ... other 2 vertices for quad

        x += metrics.advance * (cmd.font_size / atlas.reference_size);
    }

    UploadAndDrawGeometry(ctx, vertices, atlas.sdf_texture);
}
```

### SDF Rendering Shader

```hlsl
// Text rendering with SDF
Texture2D sdf_atlas : register(t0);
SamplerState sdf_sampler : register(s0);

float4 TextPS(PS_Input input) : SV_TARGET {
    float sdf_value = sdf_atlas.Sample(sdf_sampler, input.uv).r;

    // Threshold at 0.5 to get glyph outline
    float edge = smoothstep(0.35f, 0.65f, sdf_value);

    // Optional glow/outline
    float outline = smoothstep(0.2f, 0.4f, sdf_value);

    return input.color * edge;
}
```

## Texture Atlas

### Icon Batching

UI icons (16x16 to 128x128 sprites) are packed into a single atlas:

```cpp
class TextureAtlas {
private:
    // One large texture: 2048x2048, packed with all UI sprites
    Texture atlas_texture;
    std::map<std::string, AtlasRegion> regions;  // asset path → atlas uv

    struct AtlasRegion {
        Vec4 uv_rect;  // (u_min, v_min, u_max, v_max) normalized
        Vec2 size;     // original size in pixels
    };

public:
    /// Add texture to atlas (batched for next render)
    const AtlasRegion& AddTexture(const std::string& asset_path, const Texture& tex);

    /// Rebuild atlas (called once per frame if dirty)
    void Rebuild();
};
```

### Bin Packing

Textures are packed using a simple guillotine algorithm:

```cpp
struct PackNode {
    Vec2 position;
    Vec2 size;
    std::unique_ptr<PackNode> right;
    std::unique_ptr<PackNode> down;
    Texture* texture = nullptr;  // if leaf node
};

PackNode* InsertTexture(PackNode* node, const Texture& tex) {
    if (node->texture) return nullptr;  // occupied

    if (!node->right && !node->down) {
        // Leaf node — can we fit here?
        if (tex.width > node->size.x || tex.height > node->size.y) {
            return nullptr;
        }

        // Fit it
        node->texture = &tex;

        // Split: create right and down children
        if (node->size.x - tex.width > 0) {
            node->right = std::make_unique<PackNode>();
            node->right->position = {node->position.x + tex.width, node->position.y};
            node->right->size = {node->size.x - tex.width, node->size.y};
        }
        if (node->size.y - tex.height > 0) {
            node->down = std::make_unique<PackNode>();
            node->down->position = {node->position.x, node->position.y + tex.height};
            node->down->size = {node->size.x, node->size.y - tex.height};
        }

        return node;
    }

    // Try right first, then down
    PackNode* result = nullptr;
    if (node->right) result = InsertTexture(node->right.get(), tex);
    if (!result && node->down) result = InsertTexture(node->down.get(), tex);
    return result;
}
```

## Batching

### Render Command Batching

To minimize GPU state changes, commands are grouped by type and texture:

```cpp
struct RenderBatch {
    enum class Type { Rectangles, Text, TexturedQuads };
    Type type;

    ResourceHandle<Texture> texture;  // for TexturedQuads
    size_t vertex_start;
    size_t vertex_count;
    size_t index_start;
    size_t index_count;

    int z_order_min, z_order_max;
};

std::vector<RenderBatch> BuildBatches(const std::vector<RenderCommand>& commands) {
    std::vector<RenderBatch> batches;
    RenderBatch* current_batch = nullptr;

    for (const auto& cmd : commands) {
        // Can we extend the current batch?
        if (current_batch &&
            current_batch->type == GetCommandType(cmd) &&
            current_batch->texture == GetCommandTexture(cmd)) {
            // Yes, extend
            current_batch->vertex_count += cmd.GetVertexCount();
            current_batch->index_count += cmd.GetIndexCount();
        } else {
            // No, start a new batch
            RenderBatch batch;
            batch.type = GetCommandType(cmd);
            batch.texture = GetCommandTexture(cmd);
            batch.vertex_start = total_vertices;
            batch.vertex_count = cmd.GetVertexCount();
            batches.push_back(batch);
            current_batch = &batches.back();
        }
    }

    return batches;
}
```

## Dirty Region Optimization

### Incremental Rendering

Only re-render regions that changed:

```cpp
class DirtyRegionTracker {
private:
    std::vector<Rect> dirty_regions;

public:
    void MarkDirty(const Rect& region) {
        // Merge overlapping regions to avoid overdraw
        dirty_regions.push_back(region);
        MergeOverlapping();
    }

    void Clear() { dirty_regions.clear(); }

    const std::vector<Rect>& GetDirtyRegions() const { return dirty_regions; }
};

void UIPass::UpdateDirtyRegions(const HtmlDocument& document) {
    // Collect dirty elements
    std::function<void(HtmlElement*)> collect_dirty = [&](HtmlElement* elem) {
        if (elem->IsDirty()) {
            Box& box = elem->GetLayoutBox();
            dirty_tracker.MarkDirty({box.x, box.y, box.width, box.height});
        }

        for (auto& child : elem->GetChildren()) {
            collect_dirty(child);
        }
    };

    collect_dirty(document.GetRoot());
}
```

### Scissor Rect Optimization

For dirty regions, render only within scissor rect:

```cpp
void UIPass::Render(RenderContext& ctx, const HtmlDocument& document) {
    for (const auto& dirty_rect : dirty_tracker.GetDirtyRegions()) {
        ctx.SetScissorRect(dirty_rect);
        RenderWithinRegion(ctx, document, dirty_rect);
    }

    dirty_tracker.Clear();
}
```

## Z-Ordering

### Layer Priority

Elements are rendered in order:
1. Document tree order (parent before children)
2. Z-index override (higher z-index on top)

```cpp
int ComputeZOrder(HtmlElement* elem) {
    // Base z-order = document traversal order (0, 1, 2, ...)
    int base_z = elem->GetDocumentOrder();

    // Add z-index
    int z_index = elem->GetComputedStyle().z_index;

    // Stacking context: if parent has z-index, children are relative to parent
    int final_z = base_z + (z_index * 1000);  // scale z-index to prevent overlap

    return final_z;
}
```

## Performance Targets

```
Resolution:     1920x1080
Text rendering: 60 FPS with 1000+ glyphs on screen
Shapes:         10,000 rectangles per frame
Texture atlas:  1024x1024 minimum, auto-grow to 4096x4096
Memory budget:  ~100 MB for UI system (fonts, textures, buffers)
```

## Comparison

| Aspect | ImGui | Godot | UE5 (UMG) | **UzEngine** |
|--------|-------|-------|-----------|------------|
| **Rendering** | Immediate mode | Retained mode (CanvasItem) | Retained mode (UWidget) | **Retained (DOM) + immediate renderer** |
| **Font atlas** | Freetype atlas | Freetype atlas | Freetype atlas | **SDF atlas** |
| **Batching** | Per-call | Automatic | Per-widget | **Command batching** |
| **SDF text** | No | No | No | **Yes** |
| **Dirty regions** | Not tracked | Tracked (quadtree) | Tracked | **Dirty rects** |

## Rules

1. **ALWAYS** use SDF fonts — crisp at any scale, memory efficient
2. **NEVER** upload full vertex buffer every frame — use streaming/ring buffer
3. **ALWAYS** batch render commands — minimize PSO changes
4. **NEVER** render without a scissor rect — prevent off-screen rendering
5. **ALWAYS** mark dirty regions when DOM changes — skip unaffected areas
