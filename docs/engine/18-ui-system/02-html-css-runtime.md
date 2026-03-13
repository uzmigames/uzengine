# UI System — HTML/CSS Runtime Engine

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/ui/html_engine.h/.cpp`, `uz_ui_html.dll`
> **Depends on**: DX12 backend, font atlas (SDF), input system

## Purpose

The HTML/CSS runtime is a lightweight browser engine embedded in the game. It handles:
- **HTML parsing** (DOM tree construction)
- **CSS parsing & cascade** (specificity, inheritance, computed styles)
- **Layout algorithm** (flexbox, grid, absolute positioning)
- **Rendering** (SDF text, texture atlases, z-ordering)

Not a full browser — no JavaScript execution, no Network requests. Just DOM + CSS + rendering.

## Architecture

```
Game Code
    │
    ├── UIRoot::SetProperty("health", "50%")
    ├── UIRoot::GetElement("#health-bar")->SetClass("damaged")
    │
    ▼
HTML Engine
    │
    ├── DOM Tree
    │   ├── <div id="hud">
    │   │   ├── <div class="health-bar">
    │   │   │   └── <div class="health-fill">
    │   │   └── <div class="ammo-counter">50</div>
    │   │
    │   └── Mutation Observer (track changes)
    │
    ├── CSS Engine
    │   ├── Parse stylesheets
    │   ├── Compute cascade (inline > class > tag)
    │   ├── Resolve computed styles
    │   └── Update element layout
    │
    ├── Layout Engine (Flexbox)
    │   ├── Main axis sizing
    │   ├── Cross axis sizing
    │   ├── Flex wrap
    │   └── Absolute positioning
    │
    └── Renderer (DX12)
        ├── Build render batches
        ├── Render text (SDF font atlas)
        ├── Render rectangles
        └── Output to swap chain
```

## HTML Parsing

### Supported Tags

Minimal HTML subset (no `<script>`, no `<iframe>`):

```
Block Elements:  <div>, <p>, <header>, <footer>, <section>, <article>, <nav>
Inline Elements: <span>, <a>, <strong>, <em>, <button>, <input>, <label>
Media:           <img>, <canvas> (game texture refs)
Lists:           <ul>, <ol>, <li>
Forms:           <input>, <textarea>, <select>, <button>
Semantic:        <h1>...<h6>
```

### DOM API

```cpp
class HtmlElement {
public:
    std::string tag_name;
    std::string id;
    std::set<std::string> classes;

    // ─── Content ────────────────────────────────────────────────────
    std::string GetText() const;
    void SetText(StringView text);
    HtmlElement* GetFirstChild() const;
    HtmlElement* GetLastChild() const;
    std::vector<HtmlElement*> GetChildren() const;

    // ─── DOM Manipulation ───────────────────────────────────────────
    HtmlElement* AppendChild(std::unique_ptr<HtmlElement> child);
    HtmlElement* InsertBefore(std::unique_ptr<HtmlElement> child, HtmlElement* before);
    void RemoveChild(HtmlElement* child);
    void Clear();

    // ─── Class & ID ─────────────────────────────────────────────────
    void AddClass(StringView cls);
    void RemoveClass(StringView cls);
    bool HasClass(StringView cls) const;

    void SetId(StringView id_str);

    // ─── Attributes ─────────────────────────────────────────────────
    void SetAttribute(StringView name, StringView value);
    std::string GetAttribute(StringView name) const;
    bool HasAttribute(StringView name) const;

    // ─── Inline Styles ──────────────────────────────────────────────
    void SetStyle(StringView property, StringView value);
    std::string GetStyle(StringView property) const;

    // ─── Query ──────────────────────────────────────────────────────
    HtmlElement* QuerySelector(StringView selector) const;  // first match
    std::vector<HtmlElement*> QuerySelectorAll(StringView selector) const;
    HtmlElement* GetElementById(StringView id) const;

    // ─── Event Listeners ────────────────────────────────────────────
    using EventListener = std::function<void(const HtmlEvent&)>;
    void AddEventListener(StringView event_name, EventListener listener);
    void RemoveEventListener(StringView event_name);
};
```

### Document Root

```cpp
class HtmlDocument {
public:
    HtmlElement* GetRoot() { return root.get(); }
    HtmlElement* GetBody() { return body; }

    void ParseHtml(StringView html_source);
    std::string SerializeHtml() const;  // for debugging

    void Update(float delta_time);      // process mutations, recompute layout
    void Render(DX12RenderPass& pass);  // draw to screen
};
```

## CSS Parsing

### Supported Properties

```
Text:     color, font-size, font-weight, font-family, text-align, line-height, letter-spacing
Box:      width, height, padding, margin, border, border-radius, background-color
Layout:   display (flex, block, grid, none), flex-direction, justify-content, align-items
Visual:   opacity, box-shadow, text-shadow, filter (blur, brightness)
Position: position (absolute, relative), top, left, right, bottom, z-index
```

### CSS Cascade

Specificity: `inline style > id selector > class selector > tag selector > universal`

```cpp
struct ComputedStyle {
    // Colors
    Color text_color = {1, 1, 1, 1};
    Color background_color = {0, 0, 0, 0};  // transparent

    // Text
    float font_size = 16.0f;
    std::string font_family = "Arial";
    FontWeight font_weight = FontWeight::Normal;  // Normal, Bold, Light
    TextAlign text_align = TextAlign::Left;

    // Box
    Vec4 padding = {0, 0, 0, 0};   // top, right, bottom, left
    Vec4 margin = {0, 0, 0, 0};
    Vec4 border = {0, 0, 0, 0};
    float border_radius = 0.0f;

    // Layout
    Display display = Display::Block;
    FlexDirection flex_direction = FlexDirection::Row;
    FlexWrap flex_wrap = FlexWrap::NoWrap;
    JustifyContent justify_content = JustifyContent::FlexStart;
    AlignItems align_items = AlignItems::Stretch;

    // Visual
    float opacity = 1.0f;
    int z_index = 0;

    // Position
    Position position = Position::Static;
    float top = 0.0f, left = 0.0f, right = 0.0f, bottom = 0.0f;
};
```

### Inline Style Override

```cpp
// Highest specificity — overrides everything
element->style.color = "red";  // equivalent to style="color: red"

// Cascade resolution
float ComputeProperty(HtmlElement* element, StringView property) {
    // 1. Check inline style
    if (element->HasInlineStyle(property)) {
        return element->GetInlineStyle(property);
    }

    // 2. Check matching stylesheets (highest specificity first)
    for (auto& rule : matching_rules) {
        if (rule.HasProperty(property)) {
            return rule.GetProperty(property);
        }
    }

    // 3. Inherit from parent (if property is inherited)
    if (IsInheritedProperty(property)) {
        return ComputeProperty(element->parent, property);
    }

    // 4. Use default
    return GetDefaultValue(property);
}
```

## Layout Algorithm — Flexbox

### Flex Container & Items

```cpp
struct FlexContainer {
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify_content;
    AlignItems align_items;
    float gap = 0.0f;
};

struct FlexItem {
    float flex_grow = 0.0f;
    float flex_shrink = 1.0f;
    float flex_basis = -1.0f;  // -1 = auto
};
```

### Layout Steps

1. **Determine flex container's main size** (width for row, height for column)
2. **Size flex items** in main axis (grow/shrink based on available space)
3. **Size flex items** in cross axis (stretch to container or content-based)
4. **Align items** in main axis (justify-content)
5. **Align items** in cross axis (align-items)

Example: horizontal button row:

```
Container: width=800, display=flex, flex-direction=row, gap=10
├── Button 1: width=100, flex-grow=0
├── Button 2: width=100, flex-grow=0
├── Button 3: width=100, flex-grow=1  ← grows to fill space
├── Button 4: width=100, flex-grow=0
└── Button 5: width=100, flex-grow=0

Available space = 800 - (100*4) - (gap*4) = 400
Button 3 grows by 400 → width = 500

Final layout:
[100] [10] [100] [10] [500] [10] [100] [10] [100]
```

### Computed Box Sizes

```cpp
struct Box {
    float x, y;           // position
    float width, height;  // content size
    float margin[4];      // outside space
    float border[4];      // border width
    float padding[4];     // inside space

    float GetMarginBoxWidth() const {
        return width + padding[1] + padding[3] + border[1] + border[3] + margin[1] + margin[3];
    }

    float GetContentBoxWidth() const {
        return width;  // content only, excluding padding/border/margin
    }
};
```

## Rendering Pipeline

### Batch Construction

Elements are rendered in **z-order** (document order + z-index):

```cpp
void HtmlDocument::Render(DX12RenderPass& pass) {
    // Collect all elements with computed layout
    std::vector<RenderElement> elements;
    CollectRenderElements(root.get(), elements);

    // Sort by z-index (back to front)
    std::sort(elements.begin(), elements.end(),
              [](const RenderElement& a, const RenderElement& b) {
                  if (a.z_index != b.z_index) return a.z_index < b.z_index;
                  return a.document_order < b.document_order;  // tie-break
              });

    // Build render batches
    std::vector<RenderBatch> batches;
    for (const auto& elem : elements) {
        if (elem.style.display == Display::None) continue;

        // Background rectangle
        if (elem.style.background_color.a > 0.0f) {
            batches.back().rects.push_back({
                .position = elem.box.GetPosition(),
                .size = elem.box.GetSize(),
                .color = elem.style.background_color,
                .border_radius = elem.style.border_radius,
                .z_order = elem.z_index,
            });
        }

        // Text (if element has text content)
        if (!elem.text.empty()) {
            batches.back().text_runs.push_back({
                .text = elem.text,
                .position = elem.box.GetContentPosition(),
                .font_size = elem.style.font_size,
                .color = elem.style.text_color,
                .font_id = GetFontId(elem.style.font_family),
                .z_order = elem.z_index + 0.1f,  // text on top of background
            });
        }
    }

    // Render each batch
    for (const auto& batch : batches) {
        pass.DrawRectangles(batch.rects);
        pass.DrawTextRuns(batch.text_runs);
    }
}
```

### Texture References

Images use game textures (not web URLs):

```html
<!-- In HTML: asset paths resolve via VFS -->
<img src="res://textures/icon.png" width="32" height="32">

<!-- At render time: load texture and render as quad -->
if (elem.tag == "IMG") {
    TextureHandle tex = AssetManager::Load<Texture>(elem.src);
    batch.quads.push_back({
        .texture = tex,
        .position = elem.box.GetPosition(),
        .size = elem.box.GetSize(),
        .z_order = elem.z_index,
    });
}
```

## CSS Custom Properties (Variables)

Reactive binding uses CSS custom properties (aka CSS variables):

```css
:root {
    --health: 50%;
    --ammo: 24;
    --team-color: rgb(255, 0, 0);
    --is-damaged: true;
}

.health-bar {
    width: var(--health);
    opacity: var(--is-damaged, 1.0);  /* default to 1.0 */
}
```

Updating from C++:

```cpp
element->style.SetCustomProperty("--health", "75%");  // UI re-renders
```

## Performance Optimizations

### Dirty Flag System

Elements are marked "dirty" when properties change. Recompute only dirty branches:

```cpp
void HtmlElement::SetStyle(StringView property, StringView value) {
    if (computed_style.GetValue(property) == value) {
        return;  // no change
    }

    computed_style.SetValue(property, value);
    MarkDirty(DirtyFlag::Style);  // flag this element

    // Propagate dirty flag up tree (layout may change)
    if (IsLayoutProperty(property)) {
        MarkDirty(DirtyFlag::Layout);
        if (parent) parent->MarkDirty(DirtyFlag::Layout);
    }
}

void HtmlDocument::Update() {
    // Only recompute dirty elements
    if (IsDirty(DirtyFlag::Cascade)) {
        RecomputeCascade(root.get());
    }
    if (IsDirty(DirtyFlag::Layout)) {
        RecomputeLayout(root.get());
    }
    if (IsDirty(DirtyFlag::Render)) {
        RebuildRenderBatches();
    }
}
```

### Incremental Layout

When a child element changes size, only recompute ancestors (not siblings):

```cpp
void RecomputeLayout(HtmlElement* elem) {
    if (!elem->IsDirty(DirtyFlag::Layout)) return;

    // Recompute this element's layout
    ComputeBox(elem);

    // Recurse to children
    for (auto& child : elem->children) {
        RecomputeLayout(child.get());
    }

    elem->ClearDirty(DirtyFlag::Layout);
}
```

### Texture Caching

Font glyphs are cached in SDF atlas. Text doesn't re-render every frame:

```cpp
struct GlyphAtlas {
    std::map<uint32_t, GlyphInfo> glyphs;  // character code → atlas region
    Texture sdf_texture;                   // signed distance field atlas

    GlyphInfo GetGlyph(uint32_t codepoint, float font_size) {
        uint32_t key = (codepoint << 16) | (int)font_size;
        if (glyphs.count(key)) return glyphs[key];

        // Render glyph to atlas
        GlyphInfo glyph = RenderGlyphToAtlas(codepoint, font_size);
        glyphs[key] = glyph;
        return glyph;
    }
};
```

## Limits & Constraints

```cpp
struct HtmlEngineConfig {
    uint32_t max_elements = 10000;         // max DOM nodes
    uint32_t max_stylesheets = 100;
    uint32_t max_text_run_length = 5000;   // characters per text element
    float max_render_resolution = 4096.0f; // pixels
};
```

## Comparison

| Feature | Chrome | Firefox | UzEngine |
|---------|--------|---------|----------|
| **HTML parsing** | Full HTML5 | Full HTML5 | Subset (no script/iframe) |
| **CSS** | Full CSS3 | Full CSS3 | Essential (flex, grid, basic) |
| **JavaScript** | Yes | Yes | No (data binding via API) |
| **Layout** | Blink (Chromium) | Gecko | Custom flexbox |
| **Text rendering** | Native + HarfBuzz | Gecko | SDF atlas |
| **Code size** | ~15 MB | ~20 MB | ~500 KB |

## Rules

1. **NEVER** support `<script>` tags — use C++/Lua APIs for events instead
2. **ALWAYS** use CSS custom properties for reactive binding — cleaner than DOM manipulation
3. **NEVER** layout without a viewport size — always set document width/height
4. **ALWAYS** cache glyph atlas — avoid re-rendering text every frame
5. **NEVER** exceed max element count — flatten deeply nested documents
