# UI System: Game Runtime

> **Status**: Planning
> **Phase**: 2
> **Priority**: High — required for all game interfaces (HUDs, menus, dialogue)

---

## Overview

The UI System enables game developers to author player-facing interfaces in **HTML + CSS** and interact with them through C++ game code and **Lua scripting**. All game UI (HUDs, pause menus, inventory screens, dialogue) is built on this system.

The system is **declarative**: HTML/CSS define the layout and styling, while game code controls the **data** (health value, inventory count) and **logic** (button click handlers, show/hide transitions).

**Key Design**:
- **No JavaScript**. Zero client-side logic in UI documents.
- **No DOM API**. Data flows one-way: game → `ui_set_property()` → CSS `var(--name)`.
- **Stable C ABI**. UI library (`uz_ui_html.dll`) loads at runtime with no C++ cross-DLL calls.
- **GPU-composited**. UIPass renders on top of the 3D scene via a dedicated render pass (sort_key=800).
- **Lua-first API**. Game code primarily uses Lua bindings; C++ is available but uncommon.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ game.dll (game scripting, event handlers)                   │
│                                                              │
│  ctx.ui:load("res://ui/hud.html") → UiDocument*           │
│  ctx.ui:set_property(hud, "health", 0.73)                  │
│  ctx.ui:on_event(hud, "btn_pause", callback)              │
└────────────────────┬────────────────────────────────────────┘
                     │ C ABI calls
                     ▼
┌─────────────────────────────────────────────────────────────┐
│ uz_ui_html.dll (shared UI library)                          │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐  │
│  │ HTML/CSS Parser & Layout Engine                     │  │
│  │  - Parse HTML subset (no <script>, no DOM API)      │  │
│  │  - Flexbox layout, custom properties (--vars)       │  │
│  │  - Box model, transitions, hit testing              │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌─────────────────────────────────────────────────────┐  │
│  │ Drawing Subsystem                                   │  │
│  │  - Generate paint commands (quads, text, images)    │  │
│  │  - Font SDF atlas (msdfgen)                         │  │
│  │  - Video decoder backend (Media Foundation)         │  │
│  └─────────────────────────────────────────────────────┘  │
└────────────────────┬────────────────────────────────────────┘
                     │ DX12 draw list
                     ▼
┌─────────────────────────────────────────────────────────────┐
│ Runtime UI Integration (src/ui/)                            │
│                                                              │
│  UIPass (RenderGraph, sort_key=800)                        │
│   - Convert paint commands → DX12 pipeline state objects   │
│   - Render text (SDF), images, video frames               │
│   - Apply custom HLSL shaders to elements                 │
│   - Blend on top of 3D scene                              │
└────────────────────┬────────────────────────────────────────┘
                     │
                     ▼
          Final composite to swap chain
```

---

## C ABI Library: `uz_ui_html.dll`

The UI library is exposed as a **stable C interface** (no C++ name mangling, no virtual functions across DLL boundary).

### Document Lifecycle

```c
// uz_ui_html.h

typedef struct UiDocument UiDocument;

// Load a HTML document from a resource path
UiDocument* ui_load_document(const char* res_path);

// Unload and free all resources
void ui_unload_document(UiDocument* doc);

// Query document state
bool ui_is_loaded(UiDocument* doc);
```

**Usage**:
```lua
-- In game.dll Lua script
local hud = ctx.ui:load("res://ui/hud.html")
if hud then
    ctx.ui:show(hud)
end
```

### Data Binding

Game code drives all runtime data through **CSS custom properties**. There is no two-way binding — data flows one-way: C++ game code → `ui_set_property()` → CSS `var(--name)` → element styles.

```c
// Set a custom property on the document root
void ui_set_property(UiDocument* doc, const char* name, const char* value);

// Example: health = 0.75 (0–1 range)
ui_set_property(hud_doc, "health", "0.75");

// HTML reads it via CSS variable:
// <div class="health-bar">
//   <div class="fill" style="width: calc(var(--health) * 100%)"></div>
// </div>
```

**Property value types**:
- **Numbers**: `"0.75"`, `"100"`, `"-5.2"`
- **Percentages**: `"50%"`, `"100%"`
- **Colors**: `"#ff6600"`, `"rgba(255, 102, 0, 0.9)"`
- **Enums**: `"visible"`, `"hidden"`, `"playing"`, `"paused"` (application-defined)
- **Strings**: `"Player Name Here"` (when used in `content: var(--text)`)

**Example — HUD binding**:
```lua
-- Update HUD every frame with player state
function update_hud(hud_doc)
    local player = ctx.scene:find_by_tag("player")[1]
    ctx.ui:set_property(hud_doc, "health", tostring(player.health / player.max_health))
    ctx.ui:set_property(hud_doc, "ammo", tostring(player.ammo))
    ctx.ui:set_property(hud_doc, "level", tostring(player.level))
end
```

### Per-Frame Update

```c
// Update document state: run animations, decode video frames
void ui_tick(UiDocument* doc, float delta_s);
```

Must be called once per frame by the Runtime before rendering.

### Draw List Retrieval

```c
typedef struct {
    float x, y, width, height;
    uint32_t color;  // RGBA
} UiQuad;

typedef struct {
    // Array of draw commands: quads, text, images, video
    const UiQuad* quads;
    int quad_count;

    const UiTextCommand* text_commands;
    int text_command_count;

    const UiImageCommand* image_commands;
    int image_command_count;

    // ... more command types
} UiDrawList;

const UiDrawList* ui_get_draw_list(UiDocument* doc, int viewport_w, int viewport_h);
```

The draw list is valid until the next `ui_tick()` call.

### Input Routing

```c
// Inject mouse position and button state
// buttons: bitmask of UI_BUTTON_LEFT, UI_BUTTON_RIGHT, UI_BUTTON_MIDDLE
void ui_inject_mouse(UiDocument* doc, float x, float y, int buttons);

// Inject keyboard input
// vkey: Virtual-Key Code (VK_RETURN, VK_ESCAPE, VK_SPACE, etc.)
// action: UI_KEY_PRESS or UI_KEY_RELEASE
void ui_inject_key(UiDocument* doc, int vkey, int action);
```

**Input capture behavior**:
- If a UI element is **modal** (e.g., pause menu), it captures all input.
- If **passthrough** (e.g., HUD), input flows to game world after UI processes it.
- **Modal** flag is set in CSS (`--modal: true`) or via C++ API.

### Event Callbacks

UI events (click, hover, slider change, input value) are dispatched back to game code via callback:

```c
typedef struct {
    const char* element_id;     // id="btn-pause", id="health-fill"
    const char* element_class;  // class="button", "progress-bar"
    int event_type;             // UI_EVENT_CLICK, UI_EVENT_CHANGE, UI_EVENT_HOVER
    float value;                // For range/progress: 0.0–1.0
    const char* text_value;     // For text input: entered text
} UiEvent;

typedef void (*UiEventCallback)(UiDocument* doc, UiEvent* event, void* userdata);

void ui_set_event_callback(UiDocument* doc, UiEventCallback cb, void* userdata);
```

**Example C callback**:
```c
void on_hud_event(UiDocument* doc, UiEvent* event, void* userdata) {
    GameContext* ctx = (GameContext*)userdata;

    if (strcmp(event->element_id, "btn-pause") == 0 &&
        event->event_type == UI_EVENT_CLICK) {
        // Push to command queue (don't block in callback)
        ctx->cmd_queue->push(CMD_PAUSE);
    }

    if (strcmp(event->element_class, "slider") == 0 &&
        event->event_type == UI_EVENT_CHANGE) {
        // Handle slider value change
        ctx->cmd_queue->push({CMD_SET_VOLUME, event->value});
    }
}
```

**Events dispatched on Main thread** — game code must not block in the callback. Push to command queue and return immediately.

### Font Registration

```c
// Register a font family at startup
void ui_register_font(const char* family_name, const char* res_path);

// Example:
ui_register_font("UzDefault", "res://fonts/inter.ttf");
ui_register_font("Mono",      "res://fonts/jetbrains_mono.ttf");
```

Fonts are rasterized to **SDF (Signed Distance Field)** atlas using **msdfgen** at load time (or pre-baked during Cook stage for shipping).

---

## HTML Subset

No JavaScript. No DOM API. Only a predefined set of HTML elements.

| Element | Purpose | Example |
|---------|---------|---------|
| `<div>`, `<span>` | Layout containers | `<div class="panel">content</div>` |
| `<p>`, `<h1>`–`<h6>` | Text blocks | `<h1>Level 1</h1>` |
| `<img>` | Images (PNG, JPEG, SVG) | `<img src="res://ui/icon.png">` |
| `<video>` | Video playback | `<video src="res://video/intro.mp4" autoplay loop>` |
| `<button>` | Clickable, fires click event | `<button id="btn-pause">Pause</button>` |
| `<input type="range">` | Slider widget | `<input type="range" min="0" max="1" step="0.1">` |
| `<progress>` | Progress bar | `<progress value="0.75"></progress>` |
| `<canvas>` | C++ paint API | `<canvas id="minimap" width="256" height="256">` |

### Data Binding: CSS Custom Properties

Game code sets properties via `ui_set_property(doc, "health", "0.75")`, and HTML reads them via `var(--health)`:

```html
<div class="hud">
  <div class="health-bar">
    <div class="fill" style="width: calc(var(--health) * 100%)"></div>
    <span class="label">Health: calc(var(--health) * 100)%</span>
  </div>
</div>
```

The value is stored as a CSS custom property on the document root element, so all descendants can access it.

---

## CSS Support

Standard CSS box model and layout engine. **No media queries** (viewport is fixed in-game).

| Feature | Status | Notes |
|---------|--------|-------|
| Box model (margin, padding, border) | ✅ | Standard CSS |
| Flexbox (display, gap, align-items, justify-content) | ✅ | Full support |
| Grid (display: grid) | 🔄 | Phase 2+ |
| Custom properties (`--var`, `var()`) | ✅ | Data binding |
| Transitions (`transition: opacity 0.3s ease`) | ✅ | CSS-driven animations |
| `@keyframes` | ⚠️ | C++ `UIAnimator` only (not in HTML) |
| Media queries | ❌ | Not applicable (fixed viewport) |
| `calc()` | ✅ | Numeric expressions |
| `url(res://...)` for images | ✅ | Resource path references |
| `font-family` (SDF fonts) | ✅ | Registered at startup |
| Pseudo-classes (`:hover`, `:active`) | ✅ | Built-in state tracking |
| Pseudo-elements (`::before`, `::after`) | 🔄 | Phase 2+ |

**Example — Flexbox layout**:
```css
.hud {
    display: flex;
    flex-direction: row;
    gap: 16px;
    padding: 20px;
    background-color: rgba(0, 0, 0, 0.8);
}

.health-bar {
    flex: 1;
    height: 30px;
    border: 2px solid #fff;
}

.health-bar .fill {
    height: 100%;
    width: var(--health) * 100%;  /* Bound to game data */
    background-color: #00ff00;
    transition: width 0.2s ease-out;
}
```

---

## Layering & Z-Order

All UI documents have a **sort_key** that controls render order:

```
UIDocument sort_key meanings:
  0–100:     World UI (displayed in 3D space, e.g., floating damage numbers)
  100–500:   HUDs (always on screen, not modal)
  500–900:   Dialog boxes, pause menus (modal overlays)
  900+:      System overlays (developer console, FPS meter)
```

**In `.uzscene` attachment**:
```ini
[node id=10 type=UIRoot name="HUD" parent=0]
ui_document = "res://ui/hud.html"
ui_layer = 500
ui_auto_show = true
```

**Via Lua API**:
```lua
local hud = ctx.ui:load("res://ui/hud.html")
ctx.ui:set_sort_key(hud, 500)
```

The renderer composites all documents in sort_key order at the end of the frame (UIPass, sort_key=800 in RenderGraph).

---

## Scene Integration: UIRoot Node

UI documents can be attached to scene nodes via a special **UIRoot** node type:

```ini
[node id=10 type=UIRoot name="HUD" parent=0]
ui_document = "res://ui/hud.html"
ui_layer = 500
ui_auto_show = true
modal = false
```

**UIRoot properties**:
- `ui_document`: Path to the HTML file (e.g., `res://ui/hud.html`)
- `ui_layer`: Sort key for rendering order
- `ui_auto_show`: If `true`, document is shown immediately on scene load
- `modal`: If `true`, UI captures all input; if `false`, input passes through

When a scene is loaded, all UIRoot nodes are processed:
1. HTML document is loaded via `ui_load_document()`
2. If `ui_auto_show=true`, document is shown immediately
3. Game code can bind event callbacks at scene startup

---

## Lua API

### Load/Show/Hide

```lua
-- Load a HTML document from a resource path
local hud = ctx.ui:load("res://ui/hud.html")

-- Show or hide the document (controls visibility in render)
ctx.ui:show(hud)
ctx.ui:hide(hud)

-- Check if loaded
if ctx.ui:is_loaded(hud) then
    -- ...
end

-- Unload and free resources
ctx.ui:unload(hud)
```

### Data Binding

```lua
-- Set a custom property value
ctx.ui:set_property(hud, "health", tostring(player.health / player.max_health))

-- Set multiple properties at once
ctx.ui:set_properties(hud, {
    health = 0.75,
    ammo = 30,
    level = 5,
})

-- Get a property value (returns string)
local health_str = ctx.ui:get_property(hud, "health")
```

### Event Binding

```lua
-- Register a callback for a specific element event
ctx.ui:on_event(hud, "btn-pause", function(event_data)
    -- event_data.event_type: "click", "change", "hover"
    -- event_data.value: for range inputs, slider value 0.0–1.0
    -- event_data.text_value: for text inputs

    if event_data.event_type == "click" then
        ctx.scene:pause()
    end
end)

-- Register callback for all events on a class
ctx.ui:on_event_class(hud, "menu-button", function(event_data)
    print("Button clicked:", event_data.element_id)
end)
```

### Layering

```lua
-- Get/set the render sort key
local sort_key = ctx.ui:get_sort_key(hud)
ctx.ui:set_sort_key(hud, 500)

-- Set modal state (input capture)
ctx.ui:set_modal(hud, true)  -- UI captures all input
ctx.ui:set_modal(hud, false) -- Input passes through
```

### Animation Control

```lua
-- Play CSS transitions
ctx.ui:set_property(hud, "visibility", "hidden", {
    transition_duration = 0.3,
    transition_timing = "ease-out",
})
```

---

## C++ Integration Layer (`src/ui/`)

The Runtime includes a thin C++ integration layer that converts UI paint commands into DX12 draw calls.

### `UIPass` — Render Integration

```cpp
// src/ui/ui_pass.h

class UIPass : public RenderPass {
    void execute(RenderGraph::Context& ctx) override;

private:
    void render_ui_document(const UiDocument* doc, ID3D12GraphicsCommandList* cmd);
    void upload_quad_buffer(std::span<const UiQuad> quads);
    void render_text_atlas(const UiTextCommand& cmd);
};
```

**Execution flow**:
1. For each visible UI document (sorted by sort_key):
   - Call `ui_get_draw_list(doc, viewport_w, viewport_h)`
   - For each draw command in the list:
     - **Quad**: Upload vertex buffer, bind PSO, draw
     - **Text**: Bind SDF font atlas, bind text shader PSO, draw
     - **Image**: Bind image texture, bind quad PSO, draw
     - **Video**: Bind current decoded frame, bind quad PSO, draw
     - **Custom shader**: Bind UIShader PSO, draw
2. Blend results alpha-over onto the current render target (no depth test)

### `UIAnimator` — C++ Keyframe Animations

CSS `@keyframes` are intentionally **not supported** in HTML files. Animations that require timing, easing curves, or game-event-driven playback are driven by C++ through `UIAnimator`:

```cpp
// src/ui/ui_animator.h

class UIAnimator {
public:
    // Bind to an element by ID
    UIAnimator(UiDocument* doc, std::string_view element_id);

    // Animate a CSS property with easing
    void animate(std::string_view property,
                 std::initializer_list<std::pair<float, std::string>> keyframes,
                 const EasingCurve& easing = EaseOut::Cubic);

    // Play/stop/reset
    void play();
    void stop();
    void reset();

    // Check state
    bool is_playing() const;
    float elapsed() const;
};
```

**Example — Damage flash**:
```cpp
// In game.dll, when player takes damage
void player_take_damage(Player* p, int damage) {
    UIAnimator flash(hud_doc, "damage-indicator");
    flash.animate("opacity", {
        {0.0f, "1.0"},   // start fully opaque
        {0.5f, "1.0"},   // hold
        {1.0f, "0.0"},   // fade out
    }, EaseOut::Cubic);
    flash.play();
}
```

### `UIShader` — Custom HLSL on Elements

Game code can bind custom HLSL pixel shaders to UI elements via CSS class:

```cpp
// src/ui/ui_shader.h

class UIShader {
public:
    // Bind a shader to all elements with a CSS class
    static void bind_class(std::string_view class_name, std::string_view hlsl_res_path);

    // Shader is compiled and cached; reloads with hot-reload
};
```

**Usage**:
```cpp
// At startup
UIShader::bind_class("scanline", "res://shaders/ui/scanline.hlsl");
UIShader::bind_class("glow", "res://shaders/ui/glow.hlsl");
```

**HTML**:
```html
<div class="hud-text scanline">Level 5</div>
<div class="health-bar glow">...</div>
```

**Shader input** (standard UI vertex output + element style):
```hlsl
// res://shaders/ui/scanline.hlsl
cbuffer PerElement : register(b0) {
    float4 tint;
    float2 uv_min, uv_max;
};

float4 main(VS_OUTPUT vs_out) : SV_TARGET {
    float4 base = vs_out.color * tint;

    // Add scanlines
    float y = frac(vs_out.position.y / 2.0);
    float scanline = (y < 0.5) ? 1.0 : 0.5;

    return base * scanline;
}
```

### `UIMaterial` — Material Overrides

Game code can override UI element appearance (tint, texture, UV scroll):

```cpp
// src/ui/ui_material.h

class UIMaterial {
public:
    void set_tint(const DirectX::XMFLOAT4& color);
    void set_texture(std::string_view element_id, GpuTexture* texture);
    void set_uv_scroll(std::string_view element_id, float u_speed, float v_speed);

    void apply(UiDocument* doc);
};
```

**Example — Dynamic minimap**:
```cpp
// Every frame, update minimap texture from render target
UIMaterial minimap_material;
minimap_material.set_texture("minimap-canvas", render_target_texture);
minimap_material.apply(hud_doc);
```

### `UICanvas` — C++ Paint API

The `<canvas>` element enables procedural drawing in C++:

```cpp
// src/ui/ui_canvas.h

class UICanvas {
public:
    UICanvas(UiDocument* doc, std::string_view canvas_id);

    void clear(const DirectX::XMFLOAT4& color);
    void draw_rect(float x, float y, float w, float h, const DirectX::XMFLOAT4& color);
    void draw_circle(float x, float y, float radius, const DirectX::XMFLOAT4& color);
    void draw_line(float x1, float y1, float x2, float y2, float width, const DirectX::XMFLOAT4& color);
    void draw_text(std::string_view text, float x, float y, std::string_view font, float size);

    void commit();  // Flush to GPU texture
};
```

**Example — FPS graph**:
```cpp
UICanvas graph(hud_doc, "fps-graph");
graph.clear({0, 0, 0, 0.8});

for (int i = 0; i < frame_times.size(); i++) {
    float height = frame_times[i] * 60.0f;  // Scale to 0–60fps
    graph.draw_rect(i * 2, 100 - height, 2, height, {0, 1, 0, 1});
}

graph.commit();
```

---

## Font System

Fonts are **SDF-rasterized** at startup using **msdfgen** (multi-channel SDF) for crisp, scalable rendering.

### Runtime Registration

```lua
-- At startup (before loading any UI documents)
ctx.ui:register_font("UzDefault", "res://fonts/inter.ttf")
ctx.ui:register_font("Mono", "res://fonts/jetbrains_mono.ttf")
```

**Font parameters**:
- Font size: 64px (rasterized at Cook time, scales down smoothly at runtime)
- Padding: 4px (SDF border for high-quality downsampling)
- Distance field: MSDF (multi-channel) for best quality

### Build-Time Pre-Baking

During the **Cook** stage of `UzBuild.exe`:

```
Input:  res://fonts/inter.ttf
Output: .uzpak/fonts/inter.uzfont_cooked (pre-baked SDF atlas + metrics)
```

At game launch, fonts can be loaded from the cooked `.uzpak` without running msdfgen, saving ~1 second startup time.

---

## Event Types

| Event | Trigger | Data |
|-------|---------|------|
| `click` | User clicks an element | `element_id`, `element_class` |
| `change` | User changes an input value | `value` (0.0–1.0 for range), `text_value` (for text input) |
| `hover` | Mouse enters an element | `element_id`, `element_class` |
| `unhover` | Mouse leaves an element | `element_id`, `element_class` |
| `focus` | Input element gains focus | `element_id` |
| `blur` | Input element loses focus | `element_id` |

---

## Video Playback

The `<video>` element is backed by **Windows Media Foundation** for hardware-accelerated H.264/H.265 decoding:

```html
<video src="res://video/intro.mp4" autoplay loop muted width="1024" height="512"></video>
```

**Properties**:
- `autoplay` — start playing immediately on load
- `loop` — restart when finished
- `muted` — no audio (audio routed through `audio_engine` if needed)
- `width`, `height` — display size (not decode size)

**Behavior**:
- One frame decoded per `ui_tick()` call
- Frames stored in GPU texture (double-buffered)
- Supports: H.264, H.265 (DXVA2 / NVDEC)
- No audio — separate audio tracks must be played through the audio engine

---

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| `ui_load_document()` | ~5–20ms | HTML parse + layout (depends on complexity) |
| `ui_tick()` | ~1ms | Animation updates, video decode (hardware) |
| `ui_get_draw_list()` | ~1ms | Layout + paint command generation |
| `UIPass render` | ~2ms @ 1080p | DX12 draw calls for all visible documents |
| Font SDF rasterization | ~500ms | One-time at startup (pre-baked at Cook time) |
| Video frame decode | ~0.1ms | Hardware decoded, next frame streamed to texture |

**Target**: 60 FPS with up to 3 simultaneous UI documents on-screen (HUD + pause menu + tooltip).

---

## Hot-Reload

HTML and CSS files are watched for changes:

- **Editor**: Files saved → Engine reloads → Viewport updates (live preview)
- **Build**: Files changed → Next Cook stage includes new cooked data
- **Lua scripts**: Event callbacks recompiled on script reload

---

## `.uzscene` Integration

UI documents are attached to scenes via **UIRoot node type**:

```ini
[node id=10 type=UIRoot name="MainHUD" parent=0]
ui_document = "res://ui/hud.html"
ui_layer = 500
ui_auto_show = true
modal = false

[node id=11 type=UIRoot name="PauseMenu" parent=0]
ui_document = "res://ui/pause.html"
ui_layer = 800
ui_auto_show = false
modal = true
```

When the scene is loaded:
1. SceneManager finds all UIRoot nodes
2. For each node: `ui_load_document(ui_document)` → store UiDocument*
3. If `ui_auto_show=true`: `ctx.ui:show(doc)`
4. Game code binds event callbacks in scene setup

---

## Build-Time Integration

During the **Cook** stage of `UzBuild.exe`:

| Input | Output | Process |
|-------|--------|---------|
| `res://ui/*.html` | Validation + errors | Parse HTML subset, validate CSS |
| `res://fonts/*.ttf` | `.uzfont_cooked` | Rasterize SDF atlas with msdfgen |
| `res://ui/*.html` | `.uzui_cooked` | Pre-parse layout tree (skips HTML parse at runtime) |

The cooked data is embedded in `.uzpak` and loaded at game launch.

---

## Key Invariants

1. **No JavaScript**: UI documents are purely declarative.
2. **No DOM API**: No `document.getElementById()`, no `innerHTML`, no element creation at runtime.
3. **One-way data binding**: Game → `ui_set_property()` → CSS `var(--name)` → display.
4. **C ABI boundary**: All cross-DLL calls use the C ABI; no C++ headers cross `uz_ui_html.dll`.
5. **UIPass is the only UI-aware renderer**: All other RenderGraph passes are blind to UI.
6. **Main thread only**: UI events fire on the Main thread; game code must not block.
7. **SDF font rendering**: All text uses pre-rasterized SDF atlases for quality and performance.

---

## Phase Roadmap

| Phase | Features | Status |
|-------|----------|--------|
| **1** | Scene UIRoot nodes, basic event callbacks, data binding | Spec ✅, Impl pending |
| **2** | Full HTML/CSS rendering, SDF fonts, video playback, UIAnimator | Planned |
| **3** | Grid layout, pseudo-elements, custom video codecs, animated sprites in UI | Planned |
| **4** | VR world-space UI panels, accessibility (screen reader support), visual editor in VSCode | Planned |

---

## Related Documentation

- [../modules/ui/README.md](../modules/ui/README.md) — Module architecture overview
- [../modules/runtime/README.md](../modules/runtime/README.md) — Runtime integration, GameContext
- [../renderer/README.md](../renderer/README.md) — RenderGraph, UIPass, sort keys
- [../build/README.md](../build/README.md) — Cook stage, asset pipeline
- [../lua-scripting.md](../lua-scripting.md) — Lua API patterns, script lifecycle

