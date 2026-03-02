# UI System Architecture

> **Status**: Implementation Reference
> **Phase**: 2
> **Priority**: High — foundational for all game UI rendering

---

## Overview

The UI system is implemented as a **portable C library** (`uz_ui_html.dll`) with minimal coupling to the engine core. This document describes:

- How `uz_ui_html.dll` is loaded and versioned
- The render pipeline integration (DX12, render graph)
- Memory management and resource pooling
- Hot-reload mechanics
- Input routing and focus management

The system is designed for **AI-assisted development**, with plain-text HTML/CSS files that are easy to modify and hot-reload without recompilation.

---

## Module Organization

```
UzEngine/
├── src/
│   ├── ui/                          # UI integration layer
│   │   ├── ui_pass.h/cpp            # RenderGraph pass
│   │   ├── ui_animator.h/cpp        # Animation system
│   │   ├── ui_shader.h/cpp          # Custom HLSL bindings
│   │   ├── ui_material.h/cpp        # Material overrides
│   │   ├── ui_canvas.h/cpp          # Paint API
│   │   ├── ui_layout.h/cpp          # Layout state cache
│   │   ├── ui_input_router.h/cpp    # Input routing
│   │   └── ui_manager.h/cpp         # Document lifecycle
│   │
│   ├── runtime/
│   │   ├── game_context.h           # Exposes ctx.ui binding
│   │   └── lua_ui_binding.cpp       # Lua ↔ C ABI bridge
│   │
│   └── core/
│       ├── render_graph.h           # UIPass integration point
│       └── ipc_server.cpp           # IPC handlers (ui.*)
│
└── third_party/
    └── uz_ui_html/
        ├── include/
        │   └── uz_ui_html.h         # C ABI header
        └── lib/
            └── uz_ui_html.dll       # Precompiled library
```

---

## C ABI Library: uz_ui_html.dll

### Loading & Versioning

The UI library is loaded at engine startup with **semantic versioning**:

```cpp
// src/runtime/game_context.cpp

struct UzUIApi {
    // Function pointers
    UiDocument* (*ui_load_document)(const char* res_path);
    void (*ui_unload_document)(UiDocument* doc);
    void (*ui_tick)(UiDocument* doc, float delta_s);
    const UiDrawList* (*ui_get_draw_list)(UiDocument* doc, int w, int h);
    void (*ui_set_property)(UiDocument* doc, const char* name, const char* value);
    // ... more functions
};

class GameContext {
private:
    HMODULE uz_ui_module = nullptr;
    UzUIApi ui_api = {};

public:
    bool load_ui_library() {
        uz_ui_module = LoadLibraryA("uz_ui_html.dll");
        if (!uz_ui_module) {
            LOG_ERROR("Failed to load uz_ui_html.dll");
            return false;
        }

        // Load version info
        auto get_version = (uint32_t(*)())GetProcAddress(uz_ui_module, "ui_get_version");
        uint32_t version = get_version();
        uint16_t major = (version >> 16) & 0xFFFF;
        uint16_t minor = version & 0xFFFF;

        LOG_INFO("Loaded uz_ui_html.dll v{}.{}", major, minor);

        // Load all function pointers
        ui_api.ui_load_document =
            (decltype(ui_api.ui_load_document))GetProcAddress(uz_ui_module, "ui_load_document");
        // ... load all other functions

        return true;
    }

    bool unload_ui_library() {
        if (uz_ui_module) {
            FreeLibrary(uz_ui_module);
            uz_ui_module = nullptr;
            return true;
        }
        return false;
    }
};
```

### C ABI Header

All C functions use **stdcall** or **cdecl** calling convention (no C++ name mangling):

```c
// uz_ui_html.h

#ifndef UZ_UI_HTML_H
#define UZ_UI_HTML_H

#include <stdint.h>
#include <stdbool.h>

// === Versioning ===

uint32_t ui_get_version(void);
// Returns: (major << 16) | minor

// === Document Lifecycle ===

typedef struct UiDocument UiDocument;

UiDocument* ui_load_document(const char* res_path);
void ui_unload_document(UiDocument* doc);
bool ui_is_loaded(UiDocument* doc);

// === Per-Frame Update ===

void ui_tick(UiDocument* doc, float delta_s);

// === Rendering ===

typedef struct {
    float x, y, width, height;
    uint32_t color;  // RGBA
} UiQuad;

typedef struct {
    const float* positions;     // x, y pairs
    int position_count;
    uint32_t color;
    float font_size;
} UiTextCommand;

typedef struct {
    const UiQuad* quads;
    int quad_count;

    const UiTextCommand* text_commands;
    int text_command_count;

    // ... more command types
} UiDrawList;

const UiDrawList* ui_get_draw_list(UiDocument* doc, int viewport_w, int viewport_h);

// === Data Binding ===

void ui_set_property(UiDocument* doc, const char* name, const char* value);
const char* ui_get_property(UiDocument* doc, const char* name);

// === Input ===

#define UI_BUTTON_LEFT   1
#define UI_BUTTON_RIGHT  2
#define UI_BUTTON_MIDDLE 4

void ui_inject_mouse(UiDocument* doc, float x, float y, int buttons);
void ui_inject_key(UiDocument* doc, int vkey, int action);

#define UI_KEY_PRESS  0
#define UI_KEY_RELEASE 1

// === Events ===

typedef struct {
    uint32_t struct_size;
    const char* element_id;
    const char* element_class;
    int event_type;
    float value;
    const char* text_value;
} UiEvent;

#define UI_EVENT_CLICK   1
#define UI_EVENT_CHANGE  2
#define UI_EVENT_HOVER   3
#define UI_EVENT_UNHOVER 4
#define UI_EVENT_FOCUS   5
#define UI_EVENT_BLUR    6

typedef void (*UiEventCallback)(UiDocument* doc, UiEvent* event, void* userdata);
void ui_set_event_callback(UiDocument* doc, UiEventCallback cb, void* userdata);

// === Fonts ===

void ui_register_font(const char* family_name, const char* res_path);

#endif
```

### Binary Stability

The C ABI is **stable and versioned**:

- **Major version change**: ABI-breaking (new `uz_ui_html_v2.dll`)
- **Minor version change**: ABI-compatible additions (function pointers at end)
- **Struct versioning**: New fields appended, struct size checked before access

Example of forward-compatible struct:

```c
typedef struct UiEvent {
    uint32_t struct_size;    // MUST be first field
    const char* element_id;
    int event_type;
    float value;
    // Future fields added here...
} UiEvent;

// Old code still works:
if (event->struct_size >= offsetof(UiEvent, element_id) + sizeof(const char*)) {
    const char* id = event->element_id;  // Safe to access
}
```

---

## Document Lifecycle

### Loading

```cpp
// src/ui/ui_manager.h

class UIManager {
private:
    std::unordered_map<std::string, UiDocument*> documents;
    GameContext* ctx = nullptr;

public:
    UiDocument* load_document(std::string_view res_path) {
        // 1. Resolve res:// path
        std::string disk_path = resolve_resource_path(res_path);

        // 2. Call C ABI
        UiDocument* doc = ui_api.ui_load_document(disk_path.c_str());
        if (!doc) {
            LOG_ERROR("Failed to load UI document: {}", res_path);
            return nullptr;
        }

        // 3. Cache document
        documents[std::string(res_path)] = doc;

        // 4. Setup event callback
        ui_api.ui_set_event_callback(doc, on_ui_event, this);

        return doc;
    }

private:
    static void on_ui_event(UiDocument* doc, UiEvent* event, void* userdata) {
        UIManager* self = (UIManager*)userdata;
        self->dispatch_event(doc, event);
    }

    void dispatch_event(UiDocument* doc, UiEvent* event) {
        // Route to Lua callbacks
        // (See Lua binding section below)
    }
};
```

### Unloading

```cpp
bool UIManager::unload_document(std::string_view res_path) {
    auto it = documents.find(std::string(res_path));
    if (it == documents.end()) {
        return false;
    }

    UiDocument* doc = it->second;

    // Cancel active animations
    for (auto& anim : active_animations) {
        if (anim.document == doc) {
            anim.cancelled = true;
        }
    }

    // Unload C ABI document
    ui_api.ui_unload_document(doc);

    documents.erase(it);
    return true;
}
```

---

## Render Pass Integration

### UIPass in RenderGraph

```cpp
// src/ui/ui_pass.h

class UIPass : public RenderPass {
public:
    UIPass(UIManager* ui_manager, AssetManager* asset_manager)
        : ui_manager(ui_manager), asset_manager(asset_manager) {}

    void execute(RenderGraph::Context& ctx) override {
        render_ui_documents(ctx);
    }

    void set_sort_key(uint16_t key) { sort_key = key; }

private:
    UIManager* ui_manager;
    AssetManager* asset_manager;
    uint16_t sort_key = 800;  // Render on top of 3D scene

    ID3D12PipelineState* quad_pso = nullptr;
    ID3D12PipelineState* text_pso = nullptr;
    ID3D12PipelineState* image_pso = nullptr;

    // Resource pools
    GPUBufferPool vertex_buffer_pool;
    GPUTexturePool texture_pool;
};
```

### Execution Flow

```cpp
void UIPass::execute(RenderGraph::Context& ctx) {
    auto& cmd = ctx.command_list;
    ID3D12RenderTargetView rtv = ctx.render_target;
    ID3D12DescriptorHeap heaps[] = {descriptor_heap};

    cmd->SetDescriptorHeaps(1, heaps);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT vp = {0, 0, (float)ctx.width, (float)ctx.height, 0, 1};
    D3D12_RECT scissor = {0, 0, (int)ctx.width, (int)ctx.height};
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &scissor);

    // Get all visible documents sorted by layer
    std::vector<UIDocument*> docs = ui_manager->get_visible_documents();
    std::sort(docs.begin(), docs.end(),
        [](const auto& a, const auto& b) {
            return ui_manager->get_sort_key(a) < ui_manager->get_sort_key(b);
        });

    // Render each document
    for (UiDocument* doc : docs) {
        const UiDrawList* draw_list = ui_api.ui_get_draw_list(
            doc, ctx.width, ctx.height);
        if (!draw_list) continue;

        render_draw_list(cmd, draw_list);
    }
}
```

### Drawing Primitives

```cpp
void UIPass::render_draw_list(ID3D12GraphicsCommandList* cmd,
                              const UiDrawList* draw_list) {
    // === Quads (rectangles, images) ===
    if (draw_list->quad_count > 0) {
        cmd->SetPipelineState(quad_pso);
        upload_quad_buffer(draw_list->quads, draw_list->quad_count);
        cmd->DrawInstanced(6, draw_list->quad_count, 0, 0);
    }

    // === Text (SDF rendering) ===
    if (draw_list->text_command_count > 0) {
        cmd->SetPipelineState(text_pso);
        for (int i = 0; i < draw_list->text_command_count; i++) {
            const auto& text_cmd = draw_list->text_commands[i];
            bind_sdf_atlas(cmd, text_cmd.font_size);
            // Upload positions and draw
            cmd->DrawInstanced(6, 1, 0, 0);
        }
    }

    // === Images ===
    if (draw_list->image_command_count > 0) {
        cmd->SetPipelineState(image_pso);
        for (int i = 0; i < draw_list->image_command_count; i++) {
            const auto& img_cmd = draw_list->image_commands[i];
            GpuTexture* texture = asset_manager->get_texture(img_cmd.texture_path);
            bind_texture(cmd, texture);
            cmd->DrawInstanced(6, 1, 0, 0);
        }
    }

    // === Video Frames ===
    if (draw_list->video_command_count > 0) {
        // Similar to images, but bind hardware-decoded frame
    }
}
```

### Blending & Composition

```cpp
// When setting render target, enable alpha blending
D3D12_BLEND_DESC blend_desc = {};
blend_desc.RenderTarget[0].BlendEnable = TRUE;
blend_desc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
blend_desc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
blend_desc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
blend_desc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
blend_desc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
blend_desc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

// No depth test (UI always on top)
D3D12_DEPTH_STENCIL_DESC depth_desc = {};
depth_desc.DepthEnable = FALSE;
depth_desc.StencilEnable = FALSE;
```

---

## Font System

### SDF (Signed Distance Field) Rendering

Fonts are **pre-rasterized at load time** using **msdfgen** (Multi-channel SDF):

```cpp
// src/ui/ui_font_system.h

class UIFontSystem {
private:
    struct FontAtlas {
        GpuTexture* sdf_texture;      // RGBA SDF atlas
        std::unordered_map<uint32_t, Glyph> glyphs;  // Codepoint → glyph
        float base_size = 64.0f;      // Rasterized at 64px
        float padding = 4.0f;         // SDF border padding
    };

    std::unordered_map<std::string, FontAtlas> font_atlases;

public:
    void register_font(std::string_view family_name, std::string_view res_path) {
        // 1. Load TTF file
        std::vector<uint8_t> font_data = load_file(res_path);

        // 2. Initialize msdfgen
        msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
        msdfgen::FontHandle* font = msdfgen::loadFont(ft, font_data.data(), font_data.size());

        // 3. Generate SDF atlas
        FontAtlas atlas;
        atlas.sdf_texture = generate_sdf_atlas(font, family_name, base_size, padding);

        // 4. Store glyph metrics
        for (uint32_t codepoint = 32; codepoint < 127; codepoint++) {
            Glyph glyph = extract_glyph_metrics(font, codepoint);
            atlas.glyphs[codepoint] = glyph;
        }

        font_atlases[std::string(family_name)] = atlas;

        // Cleanup
        msdfgen::destroyFont(font);
        msdfgen::deinitializeFreetype(ft);
    }

private:
    GpuTexture* generate_sdf_atlas(msdfgen::FontHandle* font,
                                   std::string_view family_name,
                                   float size, float padding) {
        // Use msdfgen to rasterize entire ASCII set into single atlas texture
        // Result: 2048x2048 RGBA (each channel = distance component)
        // Size: ~16 MB uncompressed, ~2 MB BC7-compressed
    }
};
```

### Glyph Rendering

Text is rendered using SDF shader:

```hlsl
// shaders/ui/ui_text.hlsl

cbuffer TextParams : register(b0) {
    float4 tint;
    float2 uv_min, uv_max;
    float font_size;
};

Texture2D sdf_atlas : register(t0);
SamplerState sdf_sampler : register(s0);

float4 main(VS_OUTPUT vs_out) : SV_TARGET {
    // Sample SDF texture
    float sdf = sdf_atlas.Sample(sdf_sampler, vs_out.uv).r;

    // Convert SDF to coverage (sharp edges)
    float smoothstep_range = fwidth(sdf);
    float coverage = smoothstep(0.5 - smoothstep_range, 0.5 + smoothstep_range, sdf);

    float4 base = vs_out.color * tint;
    return float4(base.rgb, base.a * coverage);
}
```

### CJK Font Support

For localization, per-locale fonts can override defaults:

```cpp
class UIFontSystem {
public:
    void set_locale_font(std::string_view locale, std::string_view family_name,
                         std::string_view res_path) {
        // Register font with locale prefix
        std::string locale_family = std::format("{}-{}", locale, family_name);
        register_font(locale_family, res_path);

        // Map locale to font
        font_locale_map[std::string(locale)] = locale_family;
    }

    const FontAtlas* get_font(std::string_view family_name, std::string_view locale) {
        // Try locale-specific font first
        auto locale_it = font_locale_map.find(std::string(locale));
        if (locale_it != font_locale_map.end()) {
            auto atlas_it = font_atlases.find(locale_it->second);
            if (atlas_it != font_atlases.end()) {
                return &atlas_it->second;
            }
        }

        // Fall back to default family
        auto atlas_it = font_atlases.find(std::string(family_name));
        if (atlas_it != font_atlases.end()) {
            return &atlas_it->second;
        }

        return nullptr;
    }
};
```

---

## Hot-Reload Mechanics

### File Watching

```cpp
// src/ui/ui_hot_reload.h

class UIHotReloadManager {
private:
    std::unordered_map<std::string, FileWatcher> watchers;
    GameContext* ctx = nullptr;

public:
    void watch_document(std::string_view res_path, UiDocument* doc) {
        std::string disk_path = resolve_resource_path(res_path);

        // Watch HTML and CSS files in same directory
        FileWatcher watcher;
        watcher.on_change = [this, res_path](const std::string& changed_file) {
            reload_document(res_path);
        };

        watchers[std::string(res_path)] = watcher;
    }

    void reload_document(std::string_view res_path) {
        LOG_INFO("Reloading UI document: {}", res_path);

        // Get existing document
        UIManager* ui_mgr = ctx->get_ui_manager();
        UiDocument* old_doc = ui_mgr->get_document(res_path);
        if (!old_doc) return;

        // Preserve animation state
        AnimationStateSnapshot snapshot = capture_animation_state(old_doc);

        // Unload old, load new
        ui_mgr->unload_document(res_path);
        UiDocument* new_doc = ui_mgr->load_document(res_path);

        // Restore animations
        restore_animation_state(new_doc, snapshot);

        LOG_INFO("UI document reloaded successfully");
    }

private:
    struct AnimationStateSnapshot {
        std::vector<ActiveAnimation> animations;
        std::unordered_map<std::string, std::string> properties;
    };

    AnimationStateSnapshot capture_animation_state(UiDocument* doc) {
        // Save active animations and element properties
        // So they can be restored after reload
    }

    void restore_animation_state(UiDocument* new_doc,
                                  const AnimationStateSnapshot& snapshot) {
        // Re-apply properties and restart animations
    }
};
```

### Editor Integration (IPC)

Editor receives hot-reload notifications:

```cpp
// src/runtime/ipc_server.cpp

void register_ui_ipc_handlers(IpcServer* server) {
    // Handler for "ui.reload" command from VSCode extension
    server->register_handler("ui.reload", [](const json& params) -> json {
        std::string res_path = params["path"];
        GameContext* ctx = GameContext::instance();

        ctx->get_ui_hot_reload_manager()->reload_document(res_path);

        return {
            {"status", "ok"},
            {"message", "UI document reloaded"}
        };
    });

    // Handler for "ui.screenshot" — capture current UI state
    server->register_handler("ui.screenshot", [](const json& params) -> json {
        // Render UI to texture, return as PNG
    });

    // Handler for "ui.inspect" — query element state
    server->register_handler("ui.inspect", [](const json& params) -> json {
        std::string selector = params["selector"];
        // Return element properties, position, size, etc.
    });
}
```

---

## Input Routing

### Input Pipeline

```
┌─────────────────────────────┐
│ Windows Message Loop        │
│ (WM_MOUSEMOVE, WM_KEYDOWN) │
└────────┬────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│ Input Manager               │
│ - Aggregate input           │
│ - Convert to engine format  │
└────────┬────────────────────┘
         │
         ▼
┌─────────────────────────────┐
│ UI Input Router             │
│ (UIInputRouter)             │
│ - Check modal state         │
│ - Inject to uz_ui_html      │
│ - Determine capture         │
└────────┬────────────────────┘
         │
         ├─→ If UI captures  → UI System processes event
         │
         └─→ If UI passes    → Game Input System
```

### Implementation

```cpp
// src/ui/ui_input_router.h

class UIInputRouter {
private:
    UIManager* ui_manager = nullptr;
    GameInputSystem* game_input = nullptr;

    // Track which UI document captured input
    UiDocument* modal_document = nullptr;

public:
    void on_mouse_move(float x, float y) {
        if (modal_document) {
            // Only modal UI receives input
            ui_api.ui_inject_mouse(modal_document, x, y, get_button_state());
        } else {
            // Route to topmost visible UI first
            std::vector<UiDocument*> docs = ui_manager->get_visible_documents();
            std::reverse(docs.begin(), docs.end());  // Top-to-bottom

            for (UiDocument* doc : docs) {
                ui_api.ui_inject_mouse(doc, x, y, get_button_state());

                if (should_capture_mouse(doc, x, y)) {
                    // This UI consumed the input
                    return;
                }
            }

            // No UI captured, pass to game
            game_input->on_mouse_move(x, y);
        }
    }

    void on_key_down(int vkey) {
        if (modal_document) {
            ui_api.ui_inject_key(modal_document, vkey, UI_KEY_PRESS);
        } else {
            // Try topmost UI first
            std::vector<UiDocument*> docs = ui_manager->get_visible_documents();
            for (auto it = docs.rbegin(); it != docs.rend(); ++it) {
                ui_api.ui_inject_key(*it, vkey, UI_KEY_PRESS);
                if (should_capture_key(*it, vkey)) {
                    return;
                }
            }

            // No UI captured, pass to game
            game_input->on_key_down(vkey);
        }
    }

    void set_modal(UiDocument* doc, bool modal) {
        if (modal) {
            modal_document = doc;
            game_input->set_enabled(false);
        } else {
            modal_document = nullptr;
            game_input->set_enabled(true);
        }
    }

private:
    bool should_capture_mouse(UiDocument* doc, float x, float y) {
        // Check if hit test is inside any UI element
        // (Requires extending C ABI with hit test function)
        return ui_api.ui_hit_test(doc, x, y) != nullptr;
    }

    bool should_capture_key(UiDocument* doc, int vkey) {
        // Always capture TAB and ESCAPE in UI context
        return (vkey == VK_TAB || vkey == VK_ESCAPE);
    }
};
```

---

## Memory Management

### Document Pool

```cpp
// src/ui/ui_document_pool.h

class UIDocumentPool {
private:
    static constexpr int MAX_DOCUMENTS = 16;
    std::array<UiDocument*, MAX_DOCUMENTS> documents = {};
    std::array<bool, MAX_DOCUMENTS> in_use = {};

    std::unordered_map<std::string, int> path_to_index;

public:
    UiDocument* allocate(std::string_view res_path) {
        // Find free slot
        int slot = -1;
        for (int i = 0; i < MAX_DOCUMENTS; i++) {
            if (!in_use[i]) {
                slot = i;
                break;
            }
        }

        if (slot < 0) {
            LOG_ERROR("UIDocumentPool exhausted (max {} documents)", MAX_DOCUMENTS);
            return nullptr;
        }

        // Load document
        std::string disk_path = resolve_resource_path(res_path);
        UiDocument* doc = ui_api.ui_load_document(disk_path.c_str());

        if (!doc) {
            LOG_ERROR("Failed to load UI document: {}", res_path);
            return nullptr;
        }

        in_use[slot] = true;
        documents[slot] = doc;
        path_to_index[std::string(res_path)] = slot;

        return doc;
    }

    void deallocate(std::string_view res_path) {
        auto it = path_to_index.find(std::string(res_path));
        if (it == path_to_index.end()) {
            return;
        }

        int slot = it->second;
        UiDocument* doc = documents[slot];

        ui_api.ui_unload_document(doc);
        in_use[slot] = false;
        path_to_index.erase(it);
    }

    int get_document_count() {
        int count = 0;
        for (bool used : in_use) {
            if (used) count++;
        }
        return count;
    }
};
```

### GPU Buffer Pooling

Reuse GPU buffers to reduce allocations:

```cpp
// src/ui/ui_gpu_buffer_pool.h

class GPUBufferPool {
private:
    std::vector<ComPtr<ID3D12Resource>> available_buffers;
    std::vector<ComPtr<ID3D12Resource>> in_use_buffers;
    size_t buffer_size = 1024 * 1024;  // 1 MB default

public:
    ComPtr<ID3D12Resource> acquire(size_t requested_size) {
        // Find buffer with sufficient capacity
        for (size_t i = 0; i < available_buffers.size(); i++) {
            ComPtr<ID3D12Resource> buf = available_buffers[i];
            if (buf->GetDesc().Width >= requested_size) {
                available_buffers.erase(available_buffers.begin() + i);
                in_use_buffers.push_back(buf);
                return buf;
            }
        }

        // Allocate new buffer
        ComPtr<ID3D12Resource> new_buffer = create_gpu_buffer(requested_size);
        in_use_buffers.push_back(new_buffer);
        return new_buffer;
    }

    void release(ComPtr<ID3D12Resource> buffer) {
        // Return buffer to available pool
        auto it = std::find(in_use_buffers.begin(), in_use_buffers.end(), buffer);
        if (it != in_use_buffers.end()) {
            in_use_buffers.erase(it);
            available_buffers.push_back(buffer);
        }
    }

    void trim(size_t max_unused_size) {
        // Remove buffers if pool exceeds size limit
        while (available_buffers.size() > 0 &&
               get_total_size() > max_unused_size) {
            available_buffers.pop_back();
        }
    }
};
```

---

## Lua Binding Layer

### ctx.ui Namespace

```cpp
// src/runtime/lua_ui_binding.cpp

void setup_ui_namespace(lua_State* L, GameContext* ctx) {
    lua_newtable(L);  // ctx.ui

    // ui:load(path) → doc handle
    lua_pushcfunction(L, [](lua_State* L) {
        const char* path = luaL_checkstring(L, 2);
        UIManager* ui_mgr = GameContext::instance()->get_ui_manager();
        UiDocument* doc = ui_mgr->load_document(path);

        if (doc) {
            lua_pushlightuserdata(L, doc);
            return 1;
        } else {
            lua_pushnil(L);
            return 1;
        }
    });
    lua_setfield(L, -2, "load");

    // ui:show(doc)
    lua_pushcfunction(L, [](lua_State* L) {
        UiDocument* doc = (UiDocument*)lua_touserdata(L, 2);
        UIManager* ui_mgr = GameContext::instance()->get_ui_manager();
        ui_mgr->show_document(doc);
        return 0;
    });
    lua_setfield(L, -2, "show");

    // ui:hide(doc)
    lua_pushcfunction(L, [](lua_State* L) {
        UiDocument* doc = (UiDocument*)lua_touserdata(L, 2);
        UIManager* ui_mgr = GameContext::instance()->get_ui_manager();
        ui_mgr->hide_document(doc);
        return 0;
    });
    lua_setfield(L, -2, "hide");

    // ui:set_property(doc, name, value)
    lua_pushcfunction(L, [](lua_State* L) {
        UiDocument* doc = (UiDocument*)lua_touserdata(L, 2);
        const char* name = luaL_checkstring(L, 3);
        const char* value = luaL_checkstring(L, 4);

        ui_api.ui_set_property(doc, name, value);
        return 0;
    });
    lua_setfield(L, -2, "set_property");

    // ui:tween(doc, selector, props, options)
    lua_pushcfunction(L, [](lua_State* L) {
        UiDocument* doc = (UiDocument*)lua_touserdata(L, 2);
        const char* selector = luaL_checkstring(L, 3);
        // ... parse props and options tables
        // Call animator->tween()
        return 0;
    });
    lua_setfield(L, -2, "tween");

    // ... more functions

    lua_setfield(L, -2, "ui");  // Store as ctx.ui
}
```

---

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| `ui_load_document()` | 5–20ms | HTML parse + layout (varies with complexity) |
| `ui_tick()` | 1ms | Animation updates, video decode (async) |
| `ui_get_draw_list()` | 1ms | Layout cache + paint cmd generation |
| `UIPass::execute()` | 2ms @ 1080p | DX12 calls for all visible documents |
| Font SDF rasterization | 500ms | One-time at startup (pre-baked at Cook time) |
| Document reload (hot) | 10–30ms | Parse + cache invalidation |

---

## Related Documentation

- [../specs/ui-system.md](../specs/ui-system.md) — UI system specification
- [../specs/ui-widget-library.md](../specs/ui-widget-library.md) — Widget reference
- [../specs/ui-animation.md](../specs/ui-animation.md) — Animation system
- [../renderer/README.md](../renderer/README.md) — Render graph details
- [../guides/ui-guide.md](../guides/ui-guide.md) — Practical UI guide

