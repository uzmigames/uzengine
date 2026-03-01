# Phase 2 — UI System: Implementation Tasks

## Deliverables

- `uz_ui_html.dll` — shared UI library (used by Runtime and Build)
- `src/ui/` integration layer — UIPass, UIAnimator, UIShader, UICanvas
- Lua bindings — `ctx.ui` API (load, show, hide, set_property, on_event, etc.)
- UIRoot scene node type — attach UI documents to scenes in `.uzscene`
- `docs/specs/ui-system.md` — design spec (completed in Phase 1)
- Hot-reload support — `.html` and `.css` file watching
- Test coverage: 85%+

---

## Task Breakdown

### 1. Create `uz_ui_html.dll` C ABI Library

**Responsibility**: Parse HTML subset, layout engine, paint command generation

**Subtasks**:
1. Define C ABI interface (`uz_ui_html.h`)
   - Document lifecycle: `ui_load_document()`, `ui_unload_document()`
   - Data binding: `ui_set_property()`, `ui_get_property()`
   - Per-frame update: `ui_tick()`
   - Draw list retrieval: `ui_get_draw_list()`
   - Input injection: `ui_inject_mouse()`, `ui_inject_key()`
   - Event callbacks: `ui_set_event_callback()`
   - Font registration: `ui_register_font()`

2. HTML parser (subset)
   - Tokenizer for HTML elements: `<div>`, `<p>`, `<button>`, `<img>`, `<video>`, `<canvas>`, etc.
   - Attribute parser (id, class, src, style, etc.)
   - No JavaScript, no `<script>` tags, no DOM API
   - Error recovery (report on stderr)

3. CSS parser
   - Box model (margin, padding, border, width, height)
   - Flexbox (`display: flex`, flex-direction, gap, align-items, justify-content)
   - Custom properties (`--var-name: value`)
   - Transitions (`transition: opacity 0.3s ease`)
   - `calc()` expressions
   - Media queries (not supported; generate warning)

4. Layout engine
   - Box model measurements (Flexbox)
   - Paint command generation (quads, text, images)
   - Clipping and z-order
   - Hit testing for input (ray → element)

5. Font system
   - Font registration at startup
   - SDF rasterization using msdfgen
   - Glyph atlas texture packing
   - Per-element font styling (family, size, color, weight)

6. Input & event dispatch
   - Mouse position tracking, button state
   - Keyboard input (focus, blur, key events)
   - Element-specific event firing (click, change, hover)
   - Callback invocation (with userdata)

7. Video widget backend
   - `<video>` element support
   - Media Foundation integration (Windows)
   - Frame decoding per `ui_tick()`
   - GPU texture output (double-buffered)

8. Unit tests (doctest)
   - HTML parsing (valid/invalid markup)
   - CSS layout (flexbox, margins, padding)
   - Font rendering (glyph metrics)
   - Input hit testing
   - Custom properties data binding

**Files**:
- `src/ui/uz_ui_html.h` — C ABI header (public)
- `src/ui/uz_ui_html/` — Implementation directory
  - `html_parser.h/cpp`
  - `css_parser.h/cpp`
  - `layout_engine.h/cpp`
  - `paint_commands.h/cpp`
  - `font_system.h/cpp`
  - `video_decoder.h/cpp`
  - `input_handler.h/cpp`
  - `event_dispatcher.h/cpp`
- `src/ui/uz_ui_html_test.cpp` — Unit tests

**Acceptance criteria**:
- Parses HTML subset correctly, rejects JavaScript
- CSS box model and Flexbox implemented
- Custom properties (`--var`) accessible via CSS `var()` function
- Input hit testing works for buttons and interactive elements
- Video playback decodes frames correctly (H.264/H.265)
- All public C ABI functions exported without mangling
- 85% unit test coverage
- Performance: Document load < 20ms, `ui_tick()` < 1ms

---

### 2. Create Runtime UI Integration Layer (`src/ui/`)

**Responsibility**: Convert UI paint commands to DX12 draw calls, manage scene UI nodes

**Subtasks**:
1. UIPass (RenderGraph pass)
   - Integrate into RenderGraph (sort_key = 800, runs after PostProcess)
   - For each visible UI document (sorted by sort_key):
     - Call `ui_get_draw_list(doc, viewport_w, viewport_h)`
     - For each draw command: create DX12 draw call
   - Blend results alpha-over onto current render target
   - Performance target: < 2ms @ 1080p

2. UIDocument management
   - Load/unload documents
   - Track visibility state
   - Manage sort keys
   - Modal input capture

3. UIAnimator (C++ keyframe animations)
   - Bind to elements by ID
   - Animate CSS properties with easing curves
   - Support keyframe definitions
   - Play/stop/reset controls

4. UIShader (custom HLSL per element)
   - Bind shader to CSS class
   - Compile and cache shaders
   - Support hot-reload
   - Pass material properties to shader

5. UIMaterial (material overrides)
   - Tint color
   - Texture swapping
   - UV scrolling
   - Dynamic updates

6. UICanvas (C++ paint API for `<canvas>` elements)
   - Clear, draw_rect, draw_circle, draw_line, draw_text
   - Render to GPU texture
   - Commit to `<canvas>` element

7. UIRoot scene node type
   - Parse `.uzscene` attachment: `ui_document`, `ui_layer`, `ui_auto_show`, `modal`
   - Load documents on scene initialization
   - Auto-show if `ui_auto_show=true`
   - Support property changes at runtime

8. Unit tests
   - UIPass rendering correctness
   - Document lifecycle
   - Event callback dispatch
   - UIAnimator easing curves

**Files**:
- `src/ui/ui_pass.h/cpp` — RenderGraph integration
- `src/ui/ui_document_manager.h/cpp` — Document lifetime management
- `src/ui/ui_animator.h/cpp` — Keyframe animation
- `src/ui/ui_shader.h/cpp` — Custom shader binding
- `src/ui/ui_material.h/cpp` — Material overrides
- `src/ui/ui_canvas.h/cpp` — C++ paint API
- `src/runtime/ui_root_node.h/cpp` — Scene node type
- `src/ui/ui_integration_test.cpp` — Integration tests

**Acceptance criteria**:
- UIPass integrates with RenderGraph without breaking existing rendering
- Documents load/unload without leaks
- 60 FPS with 3 simultaneous UI documents @ 1080p
- UIAnimator eases smoothly
- Event callbacks fire on Main thread
- UIRoot nodes parse from `.uzscene` correctly
- 80% unit test coverage
- No GPU memory leaks over extended runtime

---

### 3. Lua UI Bindings (`ctx.ui` API)

**Responsibility**: Expose UI system to Lua scripts

**API functions**:
- `ctx.ui:load(path)` → UiDocument* (returns handle)
- `ctx.ui:unload(doc)`
- `ctx.ui:show(doc)` / `ctx.ui:hide(doc)`
- `ctx.ui:is_loaded(doc)` → bool
- `ctx.ui:set_property(doc, name, value)` → void
- `ctx.ui:get_property(doc, name)` → string
- `ctx.ui:set_properties(doc, {name=value, ...})` → void
- `ctx.ui:on_event(doc, element_id, callback)` → void
- `ctx.ui:on_event_class(doc, class_name, callback)` → void
- `ctx.ui:set_sort_key(doc, key)` → void
- `ctx.ui:get_sort_key(doc)` → int
- `ctx.ui:set_modal(doc, bool)` → void
- `ctx.ui:register_font(family, res_path)` → void

**Subtasks**:
1. Create Lua binding layer
   - Wrap C ABI functions
   - Convert Lua values ↔ C values (strings, numbers, tables)
   - Error handling (return nil on invalid handle)

2. Document handle system
   - Map Lua userdata → C UiDocument*
   - Prevent use-after-free
   - GC integration (Lua finalizer calls `ui_unload_document()`)

3. Event callback forwarding
   - Lua callback → C struct passed to engine
   - On UI event: C event struct → Lua callback invocation
   - Argument unpacking (event_type, element_id, value)

4. Test coverage
   - Load/unload documents
   - Data binding (set/get properties)
   - Event callback registration and dispatch
   - Multiple documents with mixed modal states

**Files**:
- `src/scripting/lua_ui_api.h/cpp` — Binding implementation
- `src/scripting/lua_ui_api_test.cpp` — Tests

**Acceptance criteria**:
- All `ctx.ui` functions callable from Lua
- Properties bindable and readable back
- Event callbacks fire in Lua with correct arguments
- Document handles don't leak memory
- 85% test coverage

---

### 4. Hot-Reload Support

**Responsibility**: Detect `.html` and `.css` changes, reload documents live

**Subtasks**:
1. File watcher for `res://ui/` directory
   - Monitor `.html` and `.css` files
   - Trigger reload on save

2. Document reload process
   - Unload old document
   - Load new document from disk
   - Re-apply cached properties
   - Re-register event callbacks
   - Refresh viewport

3. Error handling
   - Parse errors in HTML/CSS → report in console + log
   - Keep old document if new one fails to load
   - Don't crash on bad input

4. Integration with VSCode preview
   - Hot-reload visible in JPEG stream
   - Editor can trigger reloads manually via IPC

**Files**:
- File watching code (reuse from shader hot-reload)
- Reload handler in UIDocumentManager

**Acceptance criteria**:
- `.html` changes reflected within 100ms in viewport
- `.css` changes reflected within 100ms
- Parse errors reported clearly
- Old document preserved on error

---

### 5. Build-Time Integration (Cook Stage)

**Responsibility**: Validate and pre-bake UI assets during Cook

**Subtasks**:
1. Headless validation
   - Load all `.html` files from `res://ui/`
   - Validate HTML/CSS syntax
   - Report errors to developer

2. Font SDF baking
   - For each registered font in `.uzproj`:
     - Rasterize SDF atlas using msdfgen
     - Save `.uzfont_cooked` binary

3. UI pre-parsing (optional optimization)
   - Parse HTML/CSS once during Cook
   - Save `.uzui_cooked` binary format
   - At game launch: skip HTML parser, load pre-parsed layout tree

4. Integration with `UzBuild.exe`
   - Cook stage processes UI files
   - Embeds cooked data in `.uzpak`

**Files**:
- Cook stage hooks in build system
- Integration with `uz_ui_html.dll` (headless mode)

**Acceptance criteria**:
- All `.html` files validated at Cook time
- Fonts pre-baked (no msdfgen at game launch)
- UI documents load faster with pre-parsed format
- No missing assets in shipping package

---

### 6. Documentation & Examples

**Subtasks**:
1. Spec (already done): `docs/specs/ui-system.md`

2. Example game UI files
   - `res://ui/hud.html` — Simple HUD (health bar, ammo counter)
   - `res://ui/hud.css` — Styling for HUD
   - `res://ui/pause_menu.html` — Pause menu with buttons
   - `res://ui/pause_menu.css` — Menu styling

3. Example Lua scripts
   - `scripts/ui_manager.lua` — Load/manage UI documents
   - `scripts/hud_update.lua` — Update HUD data binding
   - `scripts/menu_handler.lua` — Handle menu button clicks

4. Tutorial doc
   - Getting started with UI
   - HTML subset quick reference
   - CSS layout tips
   - Lua API examples
   - Event handling patterns

**Files**:
- `docs/guides/ui-authoring.md` — How to author game UI

**Acceptance criteria**:
- Comprehensive spec (already done)
- 2+ complete working examples
- Tutorial covers basic use cases
- Clear API documentation

---

### 7. Testing

**Test coverage target**: 85%+

**Test types**:

1. **Unit tests** (doctest)
   - HTML parser (valid/invalid HTML)
   - CSS parser (box model, flexbox, custom properties)
   - Layout engine (paint command generation)
   - Font rendering (glyph metrics)
   - Input hit testing
   - Event dispatch

2. **Integration tests** (CTest)
   - Load document → update properties → render → check output
   - Event callbacks fire correctly
   - Multiple documents with mixed modal states
   - Hot-reload preserves state

3. **Performance tests**
   - Document load time (< 20ms)
   - `ui_tick()` time (< 1ms)
   - UIPass render time (< 2ms @ 1080p)
   - Memory usage per document (< 5 MB)

4. **Regression tests**
   - Golden-image comparison (rendered UI output vs. baseline)
   - Lua API stability (function signatures don't change)

**Files**:
- Test suites for each module
- Performance benchmark suite
- Golden images for regression testing

**Acceptance criteria**:
- 85%+ line coverage
- All tests pass on main branch
- No performance regressions
- Load time reasonable for Cook time

---

## Task Order

1. **uz_ui_html.dll** (weeks 1–3)
   - HTML/CSS parsers, layout engine, font system
   - Basic testing

2. **Runtime Integration** (weeks 3–4)
   - UIPass, document manager, UIRoot node
   - Scene integration testing

3. **Lua Bindings** (week 5)
   - `ctx.ui` API, event callbacks
   - API testing

4. **Hot-Reload** (week 5)
   - File watcher integration
   - Live preview testing

5. **Build Integration & Examples** (week 6)
   - Cook stage validation, pre-baking
   - Example projects

6. **Polish & Docs** (week 6)
   - Comprehensive testing
   - Tutorial documentation
   - Performance optimization

---

## Success Criteria

- [x] Spec complete (`docs/specs/ui-system.md`)
- [ ] `uz_ui_html.dll` loads and parses HTML/CSS correctly
- [ ] UIPass renders UI documents without performance regression
- [ ] Lua `ctx.ui` API fully functional
- [ ] UIRoot scene nodes work end-to-end
- [ ] Hot-reload works for `.html` and `.css` files
- [ ] 85%+ test coverage
- [ ] 2+ example projects using UI system
- [ ] No GPU memory leaks over 1 hour runtime
- [ ] Tutorial documentation complete

