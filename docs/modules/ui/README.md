# Module: UI

> **Location**: `src/ui/`
> **Type**: Shared library (`uz_ui_html.dll`) + static integration layer
> **Language**: C++23
> **Authoring format**: HTML + CSS (standard web subset)

---

## Responsibility

The UI module renders a 2D overlay on top of the 3D scene for player-facing interfaces: HUDs, menus, buttons, video players, text, images, and interactive areas. It has no game logic — it only **displays state and dispatches input events** back to `game.dll`.

UI documents are authored in **HTML + CSS**. C++ controls animations, shader effects, and material overrides on UI elements.

---

## Architecture

```
game.dll
│  calls: ui_load_document("res://ui/hud.html")
│  calls: ui_set_property(doc, "health", "73")
│  calls: ui_tick(doc, delta_s)
│
uz_ui_html.dll  (shared, used by Runtime AND Build)
│  ├── HTML parser     — subset of HTML5 (no JS, no DOM API)
│  ├── CSS engine      — box model, flexbox, custom properties (--var)
│  ├── Layout engine   — measure → arrange → paint commands
│  ├── Font renderer   — SDF atlas (msdfgen), subpixel AA
│  ├── Image decoder   — PNG/JPEG/SVG to GPU texture
│  ├── Video widget    — <video> element, decoded frames → GPU texture
│  └── Hit tester      — ray from mouse → element → dispatch callback
│
C++ integration layer  (src/ui/ in Runtime / Build, links uz_ui_html.dll)
│  ├── UIPass          — reads paint commands → produces DX12 draw calls
│  ├── UIAnimator      — per-element C++ animation curves, keyframes
│  ├── UIShader        — custom HLSL shaders bound to elements via CSS class
│  └── UIMaterial      — material overrides for UI elements (tint, texture swap)
│
▼
RenderGraph  (UIPass, sort_key = 800)
    draws 2D quads on top of 3D scene
    outputs to: Swap chain / Readback buffer
```

---

## `uz_ui_html.dll` — Shared Library

This DLL is used by **both Runtime and Build**:

- **Runtime**: loads and renders UI at game runtime
- **Build (Cook stage)**: validates HTML/CSS, bakes SDF font atlases, pre-processes layouts into `.uzui_cooked` binary for faster runtime load in shipping

The DLL exposes a **stable C ABI** (no C++ name mangling, no virtual dispatch across DLL boundary):

```c
// uz_ui_html.h  — C ABI, safe to call from any module

typedef struct UiDocument UiDocument;
typedef struct UiEvent    UiEvent;

typedef void (*UiEventCallback)(UiDocument* doc, UiEvent* event, void* userdata);

// Document lifecycle
UiDocument* ui_load_document(const char* res_path);
void        ui_unload_document(UiDocument* doc);

// Data binding — sets CSS custom property value on root element
// HTML uses var(--health) to read it
void        ui_set_property(UiDocument* doc, const char* name, const char* value);

// Per-frame update — runs animations, transitions, video decode
void        ui_tick(UiDocument* doc, float delta_s);

// Layout + paint — outputs a list of draw commands (quads, text, images)
// Caller (UIPass) converts these to DX12 draw calls
const UiDrawList* ui_get_draw_list(UiDocument* doc, int viewport_w, int viewport_h);

// Input
void        ui_inject_mouse(UiDocument* doc, float x, float y, int buttons);
void        ui_inject_key(UiDocument* doc, int vkey, int action);

// Event subscription — called when HTML element fires click/hover/etc.
void        ui_set_event_callback(UiDocument* doc, UiEventCallback cb, void* userdata);

// Font registration (called once at startup)
void        ui_register_font(const char* family, const char* res_path);
```

---

## HTML Subset

Only the following HTML elements are supported. No JavaScript, no DOM API, no `<script>` tags.

| Element | Purpose |
|---------|---------|
| `<div>`, `<span>` | Layout containers |
| `<p>`, `<h1>`–`<h6>` | Text blocks |
| `<img src="res://...">` | Images (PNG, JPEG, SVG) |
| `<video src="res://...">` | Video widget (looping game video) |
| `<button>` | Clickable element, fires `click` event |
| `<input type="range">` | Slider widget |
| `<progress value="0.73">` | Progress bar |
| `<canvas id="...">` | C++ can paint to this via `UICanvas` API |

**Data binding**: game code calls `ui_set_property(doc, "health", "73")` which sets `--health: 73` on the root element. HTML reads it via `var(--health)`.

```html
<!-- res://ui/hud.html -->
<div class="health-bar">
  <div class="fill" style="width: calc(var(--health) * 1%)"></div>
  <span class="label">var(--health)</span>
</div>
```

---

## CSS Support

Standard CSS box model with the following subset:

| Feature | Status |
|---------|--------|
| Box model (margin, padding, border) | ✅ |
| Flexbox (`display: flex`, gap, align, justify) | ✅ |
| Grid (`display: grid`) | Phase 2 |
| Custom properties (`--var`, `var()`) | ✅ |
| Transitions (`transition: opacity 0.3s ease`) | ✅ |
| Keyframe animations (`@keyframes`) | C++ `UIAnimator` only |
| Media queries | ❌ (not needed — viewport is fixed) |
| `calc()` | ✅ |
| `url(res://...)` for images | ✅ |
| `font-family` (registered SDF fonts) | ✅ |

---

## C++ Integration Layer (`src/ui/`)

```
src/ui/
├── ui_pass.h/cpp          — UIPass for RenderGraph (sort_key 800)
│                             converts UiDrawList → DX12 draw calls
├── ui_animator.h/cpp      — C++ keyframe animation bound to HTML elements
│                             by element ID; runs before ui_tick()
├── ui_shader.h/cpp        — HLSL shader binding for UI elements
│                             CSS class → custom HLSL pixel shader
├── ui_material.h/cpp      — tint, texture swap, UV scroll on UI elements
├── ui_canvas.h/cpp        — C++ paint API for <canvas> elements
│                             (procedural HUD drawing, minimap, etc.)
└── video_decoder.h/cpp    — <video> element backend
                              uses Media Foundation (Win32) to decode frames
                              outputs RGBA → GPU texture each frame
```

### `UIPass` — Render Integration

```
RenderGraph frame (sort_key = 800, runs after PostProcess, before Readback)
    1. ui_get_draw_list(doc, viewport_w, viewport_h)
    2. For each draw command in UiDrawList:
       - Quad: upload vertices → bind quad PSO → draw
       - Text: SDF atlas texture → SDF pixel shader → draw
       - Image: source texture → tinted quad PSO → draw
       - Video: current frame texture → quad PSO → draw
       - Custom shader: bind UIShader PSO → draw
    3. Blend: alpha-over onto current render target (no depth test)
```

### `UIAnimator` — C++ Animations

CSS `@keyframes` are intentionally not supported inside the HTML file. Animations that require timing, easing curves, or game-event-driven playback are driven by C++ through `UIAnimator`:

```cpp
// In game.dll
UIAnimator anim(doc, "health-fill");  // binds to element id="health-fill"
anim.animate("width", {
    {0.0f, "100%"},
    {0.3f, "73%"},    // eases to 73% over 0.3s
}, EaseOut::Cubic);
anim.play();
```

### `UIShader` — Custom HLSL on UI Elements

```cpp
// Bind a custom HLSL pixel shader to all elements with class "scanline"
UIShader::bind_class("scanline", "res://shaders/ui/scanline.hlsl");
```

The HLSL shader receives the standard UI vertex output + a `float2 uv` and `float4 tint` from the element's computed style.

---

## Font System

Fonts are SDF-rasterized at startup using **msdfgen** (multi-channel SDF). The atlas is stored in GPU memory.

```cpp
// At startup (runtime init)
ui_register_font("UzDefault", "res://fonts/inter.ttf");
ui_register_font("Mono",      "res://fonts/jetbrains_mono.ttf");
```

During Cook (Build stage), `UzBuild.exe cook` pre-bakes SDF atlases into the `.uzpak` to skip msdfgen at game launch.

---

## Event Dispatch

UI events (click, hover, slider change) flow back to `game.dll` via the C ABI callback:

```c
// game.dll registers this at scene load
ui_set_event_callback(hud_doc, on_ui_event, game_ctx);

// Fired on UI thread (Main thread) when button is clicked
void on_ui_event(UiDocument* doc, UiEvent* event, void* userdata) {
    if (strcmp(event->element_id, "btn-pause") == 0 &&
        event->type == UI_EVENT_CLICK) {
        GameContext* ctx = (GameContext*)userdata;
        ctx->scene_cmd_queue->push({CMD_PAUSE});
    }
}
```

Events are dispatched **on the Main thread** (same thread as UIPass). Game code must not block in the callback — push to `SceneCommand` queue and return.

---

## Video Playback

The `<video>` element is backed by `video_decoder.h/cpp` using Win32 Media Foundation:

```html
<video src="res://video/intro.mp4" autoplay loop muted></video>
```

- Decodes one frame per `ui_tick()` call if new frame is available
- Outputs RGBA to a `GpuTexture` (double-buffered)
- Supports: H.264, H.265 (hardware-decoded via MFT)
- No audio from UI video — route audio through `audio_engine` if needed

---

## Build Stage Integration

During the **Cook** stage of `UzBuild.exe`:

| Input | Output | Tool |
|-------|--------|------|
| `res://ui/*.html` | Validates HTML/CSS, reports errors | `uz_ui_html.dll` headless |
| `res://fonts/*.ttf` | `.uzfont_cooked` (SDF atlas, pre-baked) | `msdfgen` via `uz_ui_html.dll` |
| `res://ui/*.html` | `.uzui_cooked` (pre-parsed layout tree) | `uz_ui_html.dll` headless |

The `.uzui_cooked` format skips the HTML/CSS parser at game launch — only the layout engine runs.

---

## Key Invariants

1. **No JavaScript**. UI logic lives in `game.dll`. UI documents are declarative-only.
2. **No DOM API**. Data binding is one-way: `ui_set_property` → `var(--name)` in CSS.
3. **No game logic inside `uz_ui_html.dll`**. Events are dispatched to `game.dll` via C ABI callback.
4. **C ABI boundary**. `uz_ui_html.dll` is loaded at runtime; no C++ headers cross the DLL boundary.
5. **UIPass is the only UI-aware RenderGraph pass**. All other passes are blind to UI.
6. **Animations driven by C++** (`UIAnimator`) or CSS transitions. No `@keyframes` in HTML files.
7. **Same DLL for Runtime and Build**. Cook uses `uz_ui_html.dll` headlessly (no window, no GPU) for validation and pre-baking.

---

## Future: Visual UI Editor (Phase 2+)

A visual UI editor will be added to the **Editor module** as a VSCode WebviewPanel. It will:

- Show a live preview of the HTML/CSS layout (rendered via `uz_ui_html.dll` headless → JPEG → WebSocket)
- Allow drag-and-drop positioning of elements
- Edit CSS properties via a properties panel
- Generate plain `.html` + `.css` files (no proprietary binary format)
- Round-trip safe: editing the `.html` in text editor and reopening in visual editor preserves layout

---

## Related Docs

- [../renderer/README.md](../renderer/README.md) — UIPass sort_key=800 in RenderGraph
- [../runtime/README.md](../runtime/README.md) — `GameContext.load_ui` hook, game.dll event callbacks
- [../build/README.md](../build/README.md) — Cook stage: font baking, HTML validation
- [../../engine/hot-reload.md](../../engine/hot-reload.md) — `.html`/`.css` file watcher reload
