# Tool: Visual UI Editor

> **Status**: Planning
> **Phase**: 4 (Editor module, requires Phase 2 UI system)
> **Priority**: Medium — accelerates UI authoring; HTML/CSS text editing is always available as fallback
> **Rulebook Task**: `phase4-ui-editor`

---

## Overview

The Visual UI Editor is a **WYSIWYG HTML+CSS editor** embedded in the UzEngine VSCode extension as a WebviewPanel. It lets game developers visually position, style, and preview UI documents without leaving the editor.

The editor operates on **plain `.html` and `.css` files** — the same files consumed by `uz_ui_html.dll` at runtime. There is no proprietary binary format. Editing in the visual tool and editing in a text editor are fully interchangeable.

**Key Design Principles**:
- **Round-trip safe**: text editing and visual editing never corrupt each other.
- **Live preview**: the rendered result is identical to what the game shows, because it uses the same `uz_ui_html.dll` renderer.
- **No lock-in**: the visual editor generates standard HTML+CSS; it adds no proprietary attributes or custom elements.
- **No JavaScript**: follows the same constraint as the UI runtime — all logic stays in `game.dll`.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ VSCode Extension (TypeScript)                                   │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ UIEditorPanel (WebviewPanel)                            │   │
│  │  - Preview canvas (live render via JPEG stream)         │   │
│  │  - Element tree panel (DOM-like outline)                │   │
│  │  - Properties panel (CSS property inspector)            │   │
│  │  - Toolbar (select, add element, resize, z-order)       │   │
│  └─────────────────┬───────────────────────────────────────┘   │
│                    │ MCP IPC                                     │
│                    ▼                                             │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │ UIEditorServer (runs inside render.exe --editor)      │   │
│  │  - Loads HTML file via uz_ui_html.dll (headless)        │   │
│  │  - Renders to offscreen target → JPEG → WebSocket       │   │
│  │  - Accepts edit commands: move, resize, style, add, del │   │
│  │  - Writes changes back to .html / .css files            │   │
│  └─────────────────┬───────────────────────────────────────┘   │
└────────────────────┼────────────────────────────────────────────┘
                     │ DX12 (headless offscreen)
                     ▼
          uz_ui_html.dll (same renderer as game runtime)
```

### Component Responsibilities

| Component | Location | Responsibility |
|-----------|----------|---------------|
| `UIEditorPanel` | VSCode extension TypeScript | WebviewPanel UI: canvas, panels, toolbar, input |
| `UIEditorServer` | `render.exe --editor` | Headless render, file I/O, edit command execution |
| `uz_ui_html.dll` | Shared DLL | HTML/CSS parsing, layout, SDF font rendering |
| `UIPass` (headless) | `src/ui/ui_pass.h` | DX12 offscreen render to JPEG for preview |

---

## Preview Pipeline

The live preview uses the same JPEG streaming pipeline as the 3D viewport:

```
1. UIEditorServer opens .html file via ui_load_document()
2. On every edit:
   a. Apply edit to DOM/CSS representation (in-memory)
   b. Call ui_tick(doc, 0.0f)
   c. Call ui_get_draw_list(doc, preview_w, preview_h)
   d. UIPass (headless) renders draw list → DX12 offscreen texture
   e. Readback texture → JPEG (libjpeg-turbo, Q=85)
   f. WebSocket → UIEditorPanel canvas in VSCode Webview
3. Panel displays result at ~30 FPS refresh rate (preview, not game framerate)
```

**Preview viewport sizes** (user-selectable in toolbar):
- 1920x1080 (Full HD)
- 2560x1440 (2K)
- 3840x2160 (4K)
- Custom (width x height input)

**Data binding preview**: The properties panel includes a "Preview Variables" section where developers can set CSS custom property values (e.g., `--health = 0.5`) to see how the UI looks at different game states without running the game.

---

## Editor Panels

### 1. Preview Canvas

The central panel showing the live rendered result.

```
┌──────────────────────────────────────────────────────┐
│ [1920x1080 ▼] [Fit ▼] [Variables: --health=0.75 ✎]  │  ← toolbar
├──────────────────────────────────────────────────────┤
│                                                      │
│   ┌──────────────────────────────────────────────┐  │
│   │         [Live JPEG preview render]           │  │
│   │                                              │  │
│   │   ┌─────────────────┐  ← selected element   │  │
│   │   │ [8-handle bbox] │    with resize handles │  │
│   │   └─────────────────┘                        │  │
│   └──────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

**Interactions**:
- **Click** element: selects it (highlights in element tree, populates properties panel)
- **Drag** element: updates `left`, `top` (or flexbox `order`) via CSS
- **Drag resize handle**: updates `width`, `height` in CSS
- **Right-click**: context menu (cut, copy, paste, wrap in div, delete)
- **Ctrl+Z / Ctrl+Shift+Z**: undo / redo (uses the engine's `UndoStack`)

### 2. Element Tree

Left panel. Shows the HTML DOM hierarchy.

```
┌────────────────────────┐
│ Element Tree           │
├────────────────────────┤
│ ▼ <div class="hud">   │
│   ▼ <div class="bar"> │
│     ▶ <div .fill>     │  ← selected (highlighted)
│     ▶ <span .label>   │
│   ▶ <button #btn-pause│
└────────────────────────┘
```

**Interactions**:
- Click node: select element (syncs with canvas selection)
- Drag node: reorder elements (updates HTML source)
- Right-click: context menu (add child, delete, wrap in div)
- Double-click text node: inline text edit

### 3. Properties Panel

Right panel. Edits CSS properties and HTML attributes of the selected element.

```
┌──────────────────────────────────┐
│ Properties                       │
├──────────────────────────────────┤
│ Element: <div class="fill">      │
│ id: [            ] class: [fill] │
├──────────────────────────────────┤
│ Layout                           │
│ display:  [flex     ▼]           │
│ flex-dir: [row      ▼]           │
│ width:    [var(--health) * 100%] │
│ height:   [100%              ]   │
├──────────────────────────────────┤
│ Appearance                       │
│ background:  [#00ff00  ] [alpha] │
│ border:      [2px solid #fff   ] │
│ border-radius:[4px             ] │
├──────────────────────────────────┤
│ Transitions                      │
│ + Add transition                 │
│ width | 0.2s | ease-out          │
├──────────────────────────────────┤
│ Data Binding                     │
│ var(--health) → width            │
│ + Add binding                    │
└──────────────────────────────────┘
```

**Property groups**:
- **Identity**: `id`, `class`, element tag type
- **Layout**: `display`, `position`, `flex-*`, `grid-*`, `width`, `height`, `margin`, `padding`
- **Appearance**: `background`, `border`, `border-radius`, `opacity`, `overflow`
- **Typography**: `font-family`, `font-size`, `font-weight`, `color`, `text-align`, `line-height`
- **Transitions**: `transition` property builder (property name, duration, easing)
- **Data Binding**: shows which CSS custom properties (`--var`) are referenced in this element's styles

### 4. Toolbar

Top bar with global actions.

```
[File: res://ui/hud.html ▼] | [Select ▶] [Add ▼] | [Undo ↩] [Redo ↪] | [Preview ▶] | [1920x1080 ▼]
```

**Tools**:
- **Select** (default): click to select elements
- **Add**: dropdown with element types to insert (`<div>`, `<span>`, `<button>`, `<img>`, `<input type="range">`, `<progress>`, `<canvas>`)
- **Undo / Redo**: `Ctrl+Z` / `Ctrl+Shift+Z`
- **Preview**: opens a full-size preview window with mock variable values
- **Viewport size**: select resolution for preview canvas

---

## Edit Operations

All edit operations are encoded as commands sent from `UIEditorPanel` (TypeScript) to `UIEditorServer` (C++) via MCP IPC.

### MCP Tool: `ui_editor_move`

Move an element to a new position.

```json
{
  "tool": "ui_editor_move",
  "element_selector": "#health-fill",
  "x": 120,
  "y": 40
}
```

Updates element's `left` / `top` CSS properties (or flexbox `order` if parent is a flex container).

### MCP Tool: `ui_editor_resize`

Resize an element.

```json
{
  "tool": "ui_editor_resize",
  "element_selector": ".health-bar",
  "width": 320,
  "height": 30
}
```

### MCP Tool: `ui_editor_set_style`

Set one or more CSS properties on an element.

```json
{
  "tool": "ui_editor_set_style",
  "element_selector": ".fill",
  "properties": {
    "background-color": "#ff6600",
    "border-radius": "4px",
    "transition": "width 0.2s ease-out"
  }
}
```

### MCP Tool: `ui_editor_add_element`

Add a new HTML element as a child of the selected element.

```json
{
  "tool": "ui_editor_add_element",
  "parent_selector": ".hud",
  "tag": "button",
  "id": "btn-pause",
  "class": "menu-button",
  "text_content": "Pause"
}
```

### MCP Tool: `ui_editor_delete_element`

Remove an element and its children.

```json
{
  "tool": "ui_editor_delete_element",
  "element_selector": "#old-widget"
}
```

### MCP Tool: `ui_editor_set_attribute`

Set an HTML attribute (`id`, `class`, `src`, `type`, `value`).

```json
{
  "tool": "ui_editor_set_attribute",
  "element_selector": "img.icon",
  "attribute": "src",
  "value": "res://ui/icons/health.png"
}
```

### MCP Tool: `ui_editor_reorder`

Change element Z-order within its parent (moves DOM position).

```json
{
  "tool": "ui_editor_reorder",
  "element_selector": ".overlay",
  "direction": "up"
}
```

`direction`: `"up"` | `"down"` | `"to_front"` | `"to_back"`

### MCP Tool: `ui_editor_set_preview_variable`

Set a CSS custom property value for the preview (does not write to file).

```json
{
  "tool": "ui_editor_set_preview_variable",
  "name": "health",
  "value": "0.35"
}
```

### MCP Tool: `ui_editor_save`

Flush all pending edits to `.html` and `.css` files.

```json
{
  "tool": "ui_editor_save"
}
```

Autosave fires 2 seconds after the last edit (configurable in VSCode settings).

---

## File I/O and Round-Trip Safety

The editor reads and writes **plain HTML and CSS files**. It does not add proprietary attributes.

### Read

On open, `UIEditorServer` parses the `.html` file using `uz_ui_html.dll`'s HTML parser and builds an internal edit tree. The CSS stylesheet (`<link rel="stylesheet">` or `<style>`) is also parsed.

### Write

On save, the edit tree is serialized back to HTML and CSS using a **canonical formatter**:
- Attributes sorted alphabetically (`class` before `id` before `style`)
- Two-space indentation
- `style=""` attributes are removed if all styles are in the external CSS file
- Comments are preserved
- Custom data binding annotations (`data-bind="..."`) are never added — variables appear only in CSS `var(--name)` expressions

### Round-trip invariants

- A `.html` file saved by text editor and reopened in visual editor: layout is identical.
- A `.html` file saved by visual editor and opened in a text editor: readable, no noise.
- A `.html` file that was edited in both text editor and visual editor simultaneously: the file watcher detects external changes and prompts the user to reload.

---

## Data Binding Workflow

The visual editor provides a dedicated workflow for connecting CSS custom properties to element styles.

### Binding Panel

The "Data Binding" section of the Properties panel shows all `var(--name)` references in the selected element's computed styles:

```
Data Binding
┌──────────────────────────────────────┐
│ width = calc(var(--health) * 100%)   │  ← auto-detected from style
│ Preview value: [0.75    ]            │  ← editable slider/input
│                                      │
│ + Add var(--...)                     │
└──────────────────────────────────────┘
```

Clicking "+ Add var(--...)" opens a dialog:
1. Type the CSS expression (e.g., `calc(var(--ammo) / var(--max-ammo) * 100%)`)
2. Select which CSS property it applies to (`width`, `opacity`, `color`, etc.)
3. Editor writes the expression into the element's CSS rule

### Preview Variables Panel (Toolbar)

The toolbar shows a summary of all active CSS custom properties and lets you set preview values:

```
Variables: --health=0.75 | --ammo=12 | --score=4200  [✎ Edit All]
```

Clicking "Edit All" opens the Variables panel:

```
┌─────────────────────────────────────────────┐
│ Preview Variables (not saved to file)       │
├─────────────────────────────────────────────┤
│ --health    [0.75   ] (detected in hud.html) │
│ --ammo      [12     ] (detected in hud.html) │
│ --score     [4200   ] (detected in hud.html) │
│ --max-ammo  [30     ] (user-defined)         │
│                                              │
│ + Add variable                               │
└─────────────────────────────────────────────┘
```

The preview values are stored in `.vscode/ui-preview-vars.json` and are not committed (gitignored by default).

---

## Add Element Dialog

When the developer clicks "Add" in the toolbar and selects an element type, a dialog appears:

```
┌─────────────────────────────────────────────┐
│ Add Element                                 │
├─────────────────────────────────────────────┤
│ Type:  [<div>          ▼]                   │
│ id:    [               ]                    │
│ class: [               ]                    │
│                                             │
│ Preset:  [None ▼]                           │
│  - Health Bar                               │
│  - Ammo Counter                             │
│  - Pause Button                             │
│  - Score Display                            │
│  - Progress Bar                             │
│  - Icon + Label                             │
│                                             │
│             [Cancel]  [Add]                 │
└─────────────────────────────────────────────┘
```

**Presets** insert complete HTML+CSS snippets for common game UI patterns. Each preset is a plain `.html` partial in `res://ui/presets/`.

### Built-in Presets

| Preset | Description | Variables Exposed |
|--------|-------------|-------------------|
| Health Bar | Horizontal fill bar with label | `--health` (0–1) |
| Ammo Counter | Numeric display with icon | `--ammo`, `--max-ammo` |
| Score Display | Large numeric display | `--score` |
| Progress Bar | `<progress>` element styled | `--progress` (0–1) |
| Pause Button | Styled `<button>` that fires `click` event | — |
| Icon + Label | `<img>` + `<span>` row layout | `--icon-src`, `--label-text` |
| Minimap | `<canvas>` element for C++ painting | — |
| Notification Banner | Animated slide-in bar | `--message`, `--visible` |

Selecting a preset inserts the HTML partial and its associated CSS into the document and stylesheet respectively.

---

## Undo / Redo

The visual editor participates in the engine's `UndoStack` (`docs/specs/undo-redo.md`).

Each edit operation creates an `EditorCommand`:

```cpp
// src/editor/ui_editor_commands.h

struct UiSetStyleCommand : EditorCommand {
    std::string element_selector;
    std::string property;
    std::string old_value;
    std::string new_value;

    void execute() override;
    void undo() override;
    std::string describe() const override;  // "Set background-color on .fill"
};

struct UiMoveElementCommand : EditorCommand {
    std::string element_selector;
    float old_x, old_y;
    float new_x, new_y;

    void execute() override;
    void undo() override;
};
```

The `UndoStack` holds up to 100 commands. Undo/redo is available via `Ctrl+Z` / `Ctrl+Shift+Z` in the VSCode panel and via the toolbar buttons.

**Undo granularity**:
- Dragging an element: one undo step for the final position (not every pixel)
- Typing in a text field: one undo step per word boundary (debounced 500ms)
- Adding / deleting element: one undo step per operation
- Changing a style property: one undo step per property per field-blur

---

## Hot-Reload Integration

The visual editor is integrated with UzEngine's hot-reload system:

1. Developer edits `.html` or `.css` in text editor while visual editor is open.
2. File watcher (`src/engine/hot_reload.h`) detects the change.
3. `ui_load_document()` is called again → document reloads.
4. Preview canvas updates within one frame (~16ms).
5. Element tree panel updates to reflect new DOM.

**Conflict resolution**: if the visual editor has unsaved edits AND the file changes externally:
- A notification banner appears: "File changed externally. [Reload] [Keep my changes]"
- "Reload" discards unsaved visual edits and loads the file version.
- "Keep my changes" keeps the in-memory edits; file watcher is paused until next save.

---

## CSS File Management

The visual editor assumes one external CSS file per HTML document: the file named `<html-basename>.css` in the same directory.

Example:
```
res://ui/hud.html       ← edited by visual editor
res://ui/hud.css        ← external stylesheet, written by visual editor
```

**Inline styles vs external CSS**:
- Visual editor writes all property changes to the **external CSS file**, keyed by the element's selector.
- `style=""` attributes on elements are preserved but not generated by the editor.
- If an element has no `id` and no unique `class`, the editor assigns a generated class (`uz-el-<uuid4-short>`) and writes CSS rules under that class.

**Selector precedence**: the editor writes rules with the minimum specificity needed to target the element uniquely, following this priority:
1. `#id` if the element has a unique id
2. `.class` if the element has a unique class within its parent scope
3. Generated `.uz-el-<hash>` class (added to element, removed on next re-export if element gains id/class)

---

## UzEngine Integration Points

### MCP Server (`render.exe --editor`)

The editor mode exposes the following new MCP tools:

| MCP Tool | Description |
|----------|-------------|
| `ui.open` | Open a `.html` file in the visual editor |
| `ui.save` | Save pending edits to disk |
| `ui.get_element_tree` | Returns the current DOM tree as JSON |
| `ui.get_element_styles` | Returns computed CSS for a selector |
| `ui.set_style` | Set CSS property on element |
| `ui.add_element` | Add a new element |
| `ui.delete_element` | Delete an element |
| `ui.move` | Move element position |
| `ui.resize` | Resize element |
| `ui.reorder` | Change element Z-order |
| `ui.set_attribute` | Set HTML attribute |
| `ui.set_preview_var` | Set CSS custom property for preview |
| `ui.undo` | Undo last operation |
| `ui.redo` | Redo last undone operation |
| `ui.get_preview_frame` | Returns current JPEG preview frame (base64) |

### UIEditorServer (`src/editor/ui_editor_server.h`)

```cpp
// src/editor/ui_editor_server.h

class UIEditorServer {
public:
    // Open a UI document for editing (headless render)
    bool open_document(const char* res_path);

    // Apply an edit command; returns false if selector not found
    bool apply_command(const UIEditorCommand& cmd);

    // Get the current DOM tree as JSON (for element tree panel)
    std::string get_element_tree_json() const;

    // Get computed CSS for a selector
    std::string get_styles_json(const char* selector) const;

    // Set preview variable (not saved to file)
    void set_preview_variable(const char* name, const char* value);

    // Save current state to .html and .css files
    bool save();

    // Render and return a JPEG frame of the current state
    std::vector<uint8_t> render_preview(int width, int height, int jpeg_quality = 85);

    // Undo / redo
    bool undo();
    bool redo();

private:
    UiDocument* m_document = nullptr;
    UndoStack m_undo_stack;
    UIDocumentEditor m_editor;    // in-memory DOM + CSS edit model
    UIHeadlessPass m_pass;        // DX12 headless render
};
```

### UIDocumentEditor (`src/editor/ui_document_editor.h`)

Internal class that maintains an editable representation of the HTML+CSS:

```cpp
// src/editor/ui_document_editor.h

class UIDocumentEditor {
public:
    // Load from file
    bool load(const char* html_path);

    // Save to file (canonical format)
    bool save(const char* html_path);

    // DOM manipulation
    UIElementRef find(const char* selector);
    UIElementRef add_child(UIElementRef parent, const char* tag);
    void remove(UIElementRef element);
    void reorder(UIElementRef element, int direction);

    // Style manipulation
    void set_style(UIElementRef element, const char* property, const char* value);
    std::string get_style(UIElementRef element, const char* property) const;

    // Attribute manipulation
    void set_attribute(UIElementRef element, const char* name, const char* value);
    std::string get_attribute(UIElementRef element, const char* name) const;

    // Serialize to HTML/CSS strings for round-trip verification
    std::string serialize_html() const;
    std::string serialize_css() const;

private:
    HtmlDom m_dom;
    CssStylesheet m_stylesheet;
};
```

---

## VSCode Extension Integration

### Panel Activation

The visual editor panel is activated via:
- Command Palette: `UzEngine: Open UI Editor`
- Right-click on `.html` file in file explorer: `Open in UI Editor`
- Keyboard shortcut: `Ctrl+Shift+U` (when `.html` file is active editor)

### Webview Structure

The `UIEditorPanel` is a `vscode.WebviewPanel` with `retainContextWhenHidden: true`.

```
UIEditorPanel layout (CSS Grid):
┌─────────┬──────────────────────────┬──────────────┐
│ Element │    Preview Canvas        │  Properties  │
│ Tree    │    (JPEG stream)         │  Panel       │
│ (200px) │    (flex: 1)             │  (280px)     │
└─────────┴──────────────────────────┴──────────────┘
              ↑ Toolbar (36px)
```

### Communication Protocol

```typescript
// Extension → Webview
type EditorToWebviewMessage =
  | { type: 'preview_frame'; jpeg_base64: string; width: number; height: number }
  | { type: 'element_tree'; tree: ElementTreeNode[] }
  | { type: 'element_styles'; selector: string; styles: CssPropertyMap }
  | { type: 'selection_changed'; selector: string | null }
  | { type: 'undo_stack_state'; can_undo: boolean; can_redo: boolean }
  | { type: 'error'; message: string };

// Webview → Extension
type WebviewToEditorMessage =
  | { type: 'select_element'; selector: string }
  | { type: 'move_element'; selector: string; x: number; y: number }
  | { type: 'resize_element'; selector: string; width: number; height: number }
  | { type: 'set_style'; selector: string; property: string; value: string }
  | { type: 'add_element'; parent_selector: string; tag: string; id?: string; class?: string }
  | { type: 'delete_element'; selector: string }
  | { type: 'set_preview_var'; name: string; value: string }
  | { type: 'undo' }
  | { type: 'redo' }
  | { type: 'save' };
```

### Settings (`settings.json`)

```json
{
  "uzengine.uiEditor.autosaveDelay": 2000,
  "uzengine.uiEditor.defaultViewportWidth": 1920,
  "uzengine.uiEditor.defaultViewportHeight": 1080,
  "uzengine.uiEditor.previewQuality": 85,
  "uzengine.uiEditor.showGrid": false,
  "uzengine.uiEditor.gridSize": 8,
  "uzengine.uiEditor.snapToGrid": true
}
```

---

## Grid and Snap

When `showGrid` is enabled, the preview canvas overlays a grid at `gridSize`-pixel intervals.

When `snapToGrid` is enabled:
- Drag operations snap element position to the nearest grid point.
- Resize operations snap width/height to the nearest grid multiple.
- Snap can be overridden by holding `Alt` while dragging.

The grid values are written as exact pixel values in CSS (e.g., `left: 128px; top: 64px`). They are not aware of flexbox layout — if the element is inside a flex container, position values may be overridden by flexbox. In that case, the editor issues a warning: "This element is inside a flex container. Absolute positioning may not apply."

---

## Element Selection Model

Selection state is synchronized between the canvas, element tree, and properties panel.

```
Selected element state:
  - selector: string (CSS selector that uniquely identifies the element)
  - bounding box: { x, y, width, height } in preview pixels
  - computed styles: { property: value } map
  - HTML attributes: { name: value } map
  - children: ElementTreeNode[]
  - parent selector: string | null
```

**Multi-select**: `Ctrl+Click` adds elements to selection. When multiple elements are selected:
- The properties panel shows only properties with the same value across all selected elements.
- Editing a property applies to all selected elements.
- Move and resize operate on each element independently (not as a group).

---

## Accessibility Considerations

The editor itself must be accessible:
- All toolbar buttons have accessible labels and keyboard shortcuts.
- Element tree is navigable with arrow keys.
- Properties panel fields are standard HTML inputs with labels.
- Focus management: opening a dialog traps focus; closing restores it to the trigger element.

For UI documents being authored, the editor does not enforce accessibility rules (no `alt` text warnings, no color contrast checks) in Phase 4. These may be added in a future phase as optional linting.

---

## Performance Targets

| Operation | Target | Notes |
|-----------|--------|-------|
| Open `.html` file | < 500ms | Includes parse + first render |
| Preview refresh after edit | < 100ms | Headless render + JPEG + WebSocket |
| Element tree update | < 50ms | DOM serialization to JSON |
| Properties panel populate | < 20ms | Style lookup for selected element |
| Save to disk | < 200ms | Canonical serialization + file write |

---

## Phase Roadmap

| Sub-phase | Features |
|-----------|----------|
| **4.0** | Basic panel: preview canvas, element tree, properties panel (CSS subset) |
| **4.1** | Drag-to-move, resize handles, add/delete elements, undo/redo |
| **4.2** | Data binding panel, preview variables, presets library |
| **4.3** | Grid/snap, multi-select, context menu, reorder (Z-order) |
| **4.4** | CSS transition builder, UIAnimator integration, custom font preview |
| **4.5** | `<video>` element preview (play/pause in editor), `<canvas>` placeholder |

---

## Related Documentation

- [../specs/ui-system.md](../specs/ui-system.md) — full UI runtime spec, C ABI, data binding, UIRoot
- [../modules/ui/README.md](../modules/ui/README.md) — module architecture, UIPass, UIAnimator, UIShader
- [../specs/undo-redo.md](../specs/undo-redo.md) — EditorCommand pattern, UndoStack
- [../specs/editor-panels.md](../specs/editor-panels.md) — VSCode extension panel architecture
- [../engine/hot-reload.md](../engine/hot-reload.md) — file watcher, hot-reload system
- [../specs/console-commands.md](../specs/console-commands.md) — in-game console (separate from UI editor)
