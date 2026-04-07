## 1. HTML Subset Parser

- [ ] 1.1 Create `runtime/engine/ui/html_parser.h` — HtmlParser: Parse(html_string) → UIElement tree. UIElement: tag, id, classes (vector<string>), attributes (map), children, inline_style, text_content.
- [ ] 1.2 Create `runtime/engine/ui/html_parser.cpp` — Tokenizer (open tag, close tag, text, attribute). Recursive descent parser. Supported tags: div, span, p, h1-h6, img, button, input[range], progress, canvas. Stripped: script, iframe, style (CSS loaded separately), link. Self-closing: img, input, progress.
- [ ] 1.3 Create `runtime/engine/ui/ui_document.h/.cpp` — UiDocument: Load(html_path, css_path) → parsed tree + styles. Reload() for hot-reload. GetElementById(id), GetElementsByClass(class).

## 2. CSS Parser & Style Resolution

- [ ] 2.1 Create `runtime/engine/ui/css_parser.h/.cpp` — CssParser: Parse(css_string) → vector<CssRule>. CssRule: selector (string), declarations (map<string, string>). Support: class selectors (.foo), id selectors (#bar), tag selectors (div), descendant combinators (div .child), pseudo-classes (:hover, :active, :focus).
- [ ] 2.2 Create `runtime/engine/ui/style_resolver.h/.cpp` — StyleResolver: Resolve(element, rules) → ComputedStyle. ComputedStyle: display (flex/block/none), width/height (px/%), padding, margin, border, background_color, color, font_size, flex_direction, justify_content, align_items, gap, opacity, overflow, position. Cascade: specificity-based.
- [ ] 2.3 Add CSS custom properties — `--variable-name: value;` declarations. `var(--variable-name)` in values. Inherited down the tree. Key for data binding (phase6-6.1.1).

## 3. Flexbox Layout Engine

- [ ] 3.1 Create `runtime/engine/ui/layout_engine.h/.cpp` — LayoutEngine: ComputeLayout(root_element, viewport_size) → LayoutResult per element { x, y, width, height, content_rect, padding_rect, border_rect, margin_rect }. Implements CSS box model.
- [ ] 3.2 Implement flex layout — flex-direction (row/column), justify-content (start/end/center/space-between/space-around), align-items (start/end/center/stretch), flex-wrap, flex-grow, flex-shrink, flex-basis, gap. Main axis + cross axis algorithm.
- [ ] 3.3 Implement percentage + auto sizing — width: 50% resolves to parent width * 0.5. height: auto fits content. min-width, max-width, min-height, max-height constraints.

## 4. SDF Font System

- [ ] 4.1 Create `runtime/engine/ui/sdf_font.h/.cpp` — SdfFont: Load(.uzfont atlas + metrics). GlyphInfo: atlas_rect (UV), advance, bearing, size. MeasureText(text, font_size) → width, height. GenerateQuads(text, font_size, position) → vector<UIQuad> for rendering.
- [ ] 4.2 Create font atlas baking tool — Offline: msdfgen generates SDF atlas (512x512 RGBA8) + glyph metrics JSON. Integrated into uzcook pipeline. Default font: Inter or Noto Sans.
- [ ] 4.3 Create SDF text shader — `runtime/shaders/runtime/ui/ui_text_ps.hlsl`: samples SDF atlas, smoothstep for anti-aliasing, color from constant buffer, alpha blending.

## 5. UI Render Pass

- [ ] 5.1 Create `runtime/engine/ui/ui_pass.h/.cpp` — UIPass: render graph graphics pass (sort_key=800). Collects UiDrawList from UiDocument layout: rects (background-color), text (SDF quads), images (texture quads). Batches by texture to minimize draw calls. Alpha blend, no depth test.
- [ ] 5.2 Create UI shaders — `ui_rect_vs/ps.hlsl` (colored/rounded rects with optional border), `ui_image_vs/ps.hlsl` (textured quads), `ui_text_vs/ps.hlsl` (SDF text). All share fullscreen orthographic projection.
- [ ] 5.3 Create `runtime/engine/ui/ui_draw_list.h` — UiDrawList: vector<UiDrawCmd>. UiDrawCmd: type (Rect/Text/Image), vertex_offset, index_count, texture_handle, clip_rect. Single VB+IB for all UI geometry per frame.

## 6. UIRoot Scene Node

- [ ] 6.1 Create UIRoot component — UIRoot: html_path, css_path, ui_layer (int, sort order), is_modal (bool), auto_show (bool). Loaded UiDocument attached to entity. Multiple UIRoot entities = multiple UI layers.

## 7. Hot-Reload

- [ ] 7.1 Add .html/.css to FileWatcher — On file change, reload UiDocument. Re-parse HTML, re-resolve styles, re-layout. No engine restart needed.

## 8. Testing

- [ ] 8.1 Unit tests for HtmlParser — parse div tree, attributes, text content, stripped tags.
- [ ] 8.2 Unit tests for CssParser — selectors, declarations, specificity ordering.
- [ ] 8.3 Unit tests for flexbox layout — row/column, justify, align, wrap, gap calculations.
- [ ] 8.4 Unit tests for SdfFont — text measurement, quad generation.
- [ ] 8.5 Integration test: load HTML + CSS, compute layout, verify element positions.

## 9. Demo

- [ ] 9.1 Create `demos/ui_basic/` — simple HUD: health bar (div width %), score text, centered title. Rendered over 3D scene. Headless + screenshot.

## 10. Documentation

- [ ] 10.1 Update `docs/engine/18-ui-system/` with HTML subset, CSS support, layout engine, SDF fonts.
- [ ] 10.2 Document supported HTML tags and CSS properties.

## 11. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 11.1 Update or create documentation covering the implementation
- [ ] 11.2 Write tests covering the new behavior
- [ ] 11.3 Run tests and confirm they pass
