# UI System — AAA Gap Analysis

**Author**: UI Engineer
**Date**: 2026-03-02
**Scope**: `docs/specs/ui-system.md`, `docs/specs/localization.md`, `docs/specs/input-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/engine-vision.md`, `docs/ROADMAP.md`
**Reference engines**: Coherent GT (used in Kingdom Come: Deliverance, Dying Light 2), Scaleform (used in Skyrim, Borderlands), Unreal Engine 5 UMG + CommonUI

---

## Executive Summary

The current UI system spec establishes a sound architectural foundation: a stable C ABI DLL, CSS custom property data binding, UIPass compositing at sort_key=800, and a minimal Lua API. However, measured against what Coherent GT, Scaleform, and UMG deliver for commercial AAA and AA games, the spec has **20 critical gaps** that would make it commercially inadequate. Several of these gaps are not merely missing features but represent architectural decisions that will be painful to retrofit later.

The most severe issues are: the absence of a focus/navigation system for gamepad, the lack of a widget library beyond bare HTML primitives, the one-way-only data binding model that will frustrate complex UI authoring, and the complete absence of UI state machine infrastructure. These four gaps alone would block shipping a game with a standard pause-menu flow on console.

---

## Gap 1: Gamepad Focus and Navigation System

**Severity**: CRITICAL — BLOCKER for console/gamepad release

### Why It Is Critical

Coherent GT ships a full focus graph with directional navigation (`ui-nav-up`, `ui-nav-down`, `ui-nav-left`, `ui-nav-right` CSS properties). Scaleform uses Flash's built-in focus model. UMG has `CommonUI` built on top of it, which is the gold standard for console AAA: every button, slider, and list item participates in a navigable tree driven by gamepad d-pad and left stick.

Without this, any game targeting console, or any PC game with full gamepad support (which is now expected for Steam Deck certification), will require developers to hand-implement focus management in Lua, including: tracking which element is focused, dispatching virtual mouse-click events when A is pressed, wrapping focus at list ends, and restoring focus when sub-menus close. This is a well-known pain point — Scaleform games in the 2010s suffered from exactly this because Scaleform's ActionScript focus system was poorly integrated with controller input.

### Technical Description

The system requires:

1. A **focusable flag** per element in HTML: `<button tabindex="0">` or a custom `uz-nav-focusable` attribute.
2. A **focus graph** computed at layout time: for each element, the nearest focusable neighbor in each cardinal direction. This can be computed using element bounding boxes after layout.
3. **Directional navigation input**: the input system (already specced with InputAction/InputMappingContext) delivers `IA_UINavUp`, `IA_UINavDown`, `IA_UINavLeft`, `IA_UINavRight`, `IA_UIConfirm`, `IA_UIBack` when a modal UI document is active.
4. **Focus visual**: the focused element receives a `:focus` CSS pseudo-class, which designers style with an outline or highlight shader.
5. **Focus memory**: when returning from a sub-menu, focus restores to the previously focused element in the parent menu.
6. A **focus API** in Lua: `ctx.ui:set_focus(doc, element_id)`, `ctx.ui:get_focused(doc) -> element_id`.

The C ABI needs one new function: `void ui_set_focus(UiDocument* doc, const char* element_id)` and a `UI_EVENT_FOCUS_CHANGED` event type.

### Impact If Absent

Any modal menu (pause, settings, inventory) is mouse-only. Console release is blocked. Steam Deck "Playable" rating requires full controller navigation. Developers will build fragile workarounds in Lua that break on every layout change.

**Complexity**: L
**Dependencies**: Input System (InputMappingContext for UI context), CSS pseudo-class `:focus`, UIRoot modal system

---

## Gap 2: Widget Library (Composable UI Components)

**Severity**: HIGH

### Why It Is Critical

Scaleform provides a library of pre-built ActionScript components (List, ScrollList, Dropdown, Tooltip, Modal). UMG ships with dozens of widgets including ListView, TileView, ComboBox, ProgressBar, Slider, CheckBox, RadioButton, SpinBox, EditableText. Coherent GT provides an HTML component library with React-style virtual DOM for common game UI patterns.

The current spec provides only the raw HTML elements: `<button>`, `<input type="range">`, `<progress>`, `<canvas>`. Every game that needs a settings screen with a scrollable list, a key-rebinding table, a checkbox grid for graphics options, or an inventory grid with slots must build these from scratch in HTML/CSS with manual Lua wiring. This is not scalable — every team member reinvents the same widgets with different bugs.

### Technical Description

UzEngine needs a **reusable widget library** delivered as a set of `.html` include-files or CSS components that can be imported into game UI documents:

```html
<!-- res://ui/widgets/scroll-list.html — a parameterized scrollable list -->
<!-- Usage: embed as <uz-scroll-list> or include via @import -->
```

Required widgets for a basic AAA game:

- **ScrollView**: scrollable container with scroll bar, touch/gamepad scroll support
- **ListView**: data-driven list of items, virtualized for large lists (1000+ items), selection state
- **GridView**: N-column grid (inventory, equipment slots), keyboard/gamepad navigation
- **DropdownSelect**: combo box, opens a popup list, selects value
- **CheckBox**: toggle with label, accessible
- **RadioGroup**: mutually exclusive option set
- **Slider with label**: `<input type="range">` + displayed numeric value + label
- **Modal dialog**: standard confirm/cancel overlay with backdrop
- **Tooltip**: hover-triggered popup with delay and auto-positioning
- **ProgressBar with label**: animated fill + percentage/fraction text
- **TabBar**: horizontal/vertical tab navigation
- **Notification toast**: timed pop-in/pop-out notification panel

The key architectural point: these widgets must have standard **CSS custom property APIs** so game code sets `--list-item-count`, `--selected-index`, `--item-label-N` etc. and the widget renders correctly without bespoke Lua wiring per instance.

### Impact If Absent

Each game team re-implements the same widgets independently, with inconsistent quality, accessibility, and gamepad support. Development time increases significantly for UI-heavy games. Common UI bugs (scroll position not restoring, focus lost on layout change) appear repeatedly.

**Complexity**: XL
**Dependencies**: Gap 1 (focus navigation), Gap 4 (responsive sizing), CSS Grid (Phase 2), data binding improvements

---

## Gap 3: UI Animation System — Tweening and Sequencing

**Severity**: HIGH

### Why It Is Critical

Coherent GT exposes a full CSS animation pipeline including `@keyframes`, `animation-delay`, `animation-iteration-count`, `animation-fill-mode`. Scaleform uses Flash's native timeline with tweening. UMG has `UWidgetAnimation` with a timeline editor, sequence support, and event tracks that fire Lua/Blueprint callbacks at specific keyframe times.

The current spec deliberately excludes `@keyframes` from HTML documents, routing all animations through C++ `UIAnimator`. This is a reasonable sandboxing decision but it creates a significant authoring friction: every animation — a button hover bounce, a screen flash, a damage indicator pulse — requires C++ code in `game.dll`, not declarative CSS. For a team using Lua-first workflow (as UzEngine targets), this means all non-trivial animations require a C++ rebuild cycle rather than the 60ms Lua hot-reload.

### Technical Description

Three layers of animation are needed:

**Layer 1 — CSS Transitions (already specced)**: `transition: width 0.3s ease-out`. This covers simple property interpolations. Status: specced and covered.

**Layer 2 — CSS Keyframes in HTML (missing)**: Allow `@keyframes` inside `<style>` blocks in HTML documents. This is the standard web animation model. The rationale for excluding them ("logic belongs in game.dll") is overly strict — a button idle pulse animation has no game logic. The restriction forces C++ for purely visual decisions.

**Layer 3 — Lua-driven tween API (missing)**: A timeline API accessible from Lua without requiring C++ `UIAnimator`. Proposed:

```lua
-- ctx.ui:tween(doc, element_id, property, from, to, duration, easing, [on_complete])
ctx.ui:tween(hud, "damage-vignette", "opacity", 0.0, 1.0, 0.1, "ease-out", function()
    ctx.ui:tween(hud, "damage-vignette", "opacity", 1.0, 0.0, 0.4, "ease-in")
end)
```

**Layer 4 — Animation sequences (missing)**: Chain multiple property animations with delays and callbacks. UMG Widget Animations do this with a timeline. Minimum viable: a Lua `sequence` builder:

```lua
local seq = ctx.ui:sequence(hud)
seq:at(0.0):tween("panel", "opacity", 0, 1, 0.2)
seq:at(0.0):tween("panel", "scale", 0.8, 1.0, 0.3, "ease-out-back")
seq:at(0.2):tween("title", "opacity", 0, 1, 0.15)
seq:play()
```

The C ABI addition required: `void ui_animate_property(UiDocument*, const char* element_id, const char* property, const char* from_value, const char* to_value, float duration_s, const char* easing, UiAnimCallback on_complete, void* userdata)`.

### Impact If Absent

Screen transitions, menu entrance/exit animations, damage feedback, and reward screen effects all require C++ code. This defeats the Lua-first authoring philosophy of UzEngine. UI will feel static and lifeless compared to competitors because designers cannot author animations without a programmer.

**Complexity**: L
**Dependencies**: CSS engine animation pipeline, Lua binding additions

---

## Gap 4: Multi-Resolution and Safe Area Support

**Severity**: HIGH for any console or cross-platform target

### Why It Is Critical

Coherent GT supports percentage-based layouts and safe area CSS variables (`env(safe-area-inset-top)`) for TV overscan. UMG has a full anchor system (0,0 to 1,1 with min/max anchors and offsets) that works identically across 720p, 1080p, 1440p, 4K, and ultrawide. The current spec explicitly excludes media queries ("not applicable — viewport is fixed") and gives no mechanism for safe area handling.

This assumption is false: even on a single PC, players run different resolutions. On console (future platform target mentioned in the roadmap), safe area is mandatory — some televisions cut off the outer 5-10% of the screen (action-safe zone). Fonts that look correct at 1080p may be too small at 1440p and too large at 720p.

### Technical Description

Required additions:

**1. Viewport-relative units**: `vw`, `vh`, `vmin`, `vmax` in the CSS engine. These allow `font-size: 2vh` which scales with viewport height automatically.

**2. Safe area CSS variables**: Inject into the document root at render time:
```css
/* Injected by UIPass at render time */
:root {
    --safe-area-top: 40px;       /* TV overscan, iOS notch */
    --safe-area-bottom: 34px;    /* iOS home indicator */
    --safe-area-left: 0px;
    --safe-area-right: 0px;
}
```
Game code sets these via: `ui_set_safe_area(doc, top_px, bottom_px, left_px, right_px)`.

**3. Anchor/alignment system**: For elements that must be pinned to screen corners/edges:
```css
.hud-bottom-right {
    position: absolute;
    right: calc(var(--safe-area-right) + 20px);
    bottom: calc(var(--safe-area-bottom) + 20px);
}
```
This works if `position: absolute` is supported within the CSS engine.

**4. Reference resolution scaling**: A `ui_reference_width` and `ui_reference_height` in `.uzproj` allowing designers to work at a canonical resolution (e.g., 1920x1080), with the layout engine scaling automatically to actual viewport.

### Impact If Absent

HUD elements clip off screen on TVs. UI designed at 1080p looks wrong at 4K (too small) or 720p (too large). Console certification fails without safe area compliance. Ultrawide monitors show UI elements at wrong positions.

**Complexity**: M
**Dependencies**: CSS engine (viewport units, position: absolute), UIPass (inject safe area variables), .uzproj config

---

## Gap 5: Rich Text and Inline Content Rendering

**Severity**: MEDIUM-HIGH

### Why It Is Critical

Scaleform provides ActionScript TextField with HTML subset (bold, italic, color, links, images inline). Coherent GT supports full CSS text-shadow, letter-spacing, word-spacing, text-transform, and inline images in text flow. UMG has a `RichTextBlock` widget with inline decorators for custom markup (item icons, color tags, ability icons) commonly used in RPGs and loot games.

The current spec covers basic text via `<p>`, `<h1>`-`<h6>`, and `<span>`. It does not specify: text-shadow, stroke/outline on text, mixed bold/italic in a single paragraph, inline images inside text, or any form of rich text markup.

### Technical Description

Required features:

**1. CSS text properties**: `text-shadow`, `letter-spacing`, `line-height`, `text-transform` (uppercase for stylized buttons), `text-overflow: ellipsis` (truncate long NPC names with "..."), `white-space: nowrap`.

**2. Inline HTML formatting**: `<strong>`, `<em>`, `<u>`, `<s>`, `<span style="color: ...">` within text paragraphs. These are trivial HTML elements but need the inline layout engine to handle mixed-font runs.

**3. Inline images in text**: `<img>` inside a `<p>` for ability icons next to ability names, currency icons next to gold amounts:
```html
<p>You gained <img src="res://ui/icons/gold.png" class="inline-icon"> 500 gold</p>
```

**4. Gradient text**: `background-clip: text` with `background: linear-gradient(...)` — used in countless AAA title screens.

**5. SDF text outline and shadow**: The SDF font atlas already supports this efficiently. The CSS properties `text-shadow` and `-uz-text-stroke: 2px #000` (custom extension) should expose it.

**6. Emoji support**: Unicode emoji rendering via a built-in emoji font (Noto Color Emoji or similar). Required for any social feature, chat, or modern UI pattern.

### Impact If Absent

All text is unstyled. NPC dialog boxes with colored character names are impossible. Item descriptions with inline icons require workaround layouts. Title screens and stat screens look unpolished. Games with chat features cannot render emoji.

**Complexity**: M
**Dependencies**: CSS engine inline layout, SDF renderer extensions, font atlas multi-face support

---

## Gap 6: Bidirectional Data Binding

**Severity**: MEDIUM-HIGH

### Why It Is Critical

The current spec is explicit: "One-way data binding: game → ui_set_property() → CSS var(--name) → display." This is correct for display-only elements (health bar, score, minimap). However, for interactive elements — text input fields, sliders, checkboxes, dropdown selects — game code also needs to **read the current UI state**, not just write to it.

UMG solves this with two-way binding via `TAttribute<T>` and `TBindingOf<T>`. Coherent GT uses a reactive model where JavaScript bindings auto-sync. Even without JavaScript, the UzEngine needs a way to ask the document: "what is the current value of the volume slider?" without waiting for a change event.

### Technical Description

The current event model (`UI_EVENT_CHANGE` fires when user changes slider) is one direction of this — game gets notified. But there are cases where game code needs to **set** the UI state and also **read** it synchronously:

1. **Settings screen open**: game reads current audio volume, calls `ui_set_property(doc, "volume-slider", "0.73")` to initialize slider. User navigates to the slider (focus). User cancels. Game needs to know that slider was not changed. Currently: game must track this state itself.

2. **Input field validation**: game needs to read the text field contents mid-edit to validate before accepting. Currently: game must maintain a shadow copy of what was last fired via `UI_EVENT_CHANGE`.

3. **Multi-element state query**: game needs to query whether a checkbox is checked without waiting for a click event.

Required C ABI additions:

```c
/* Read current value of an input element */
const char* ui_get_element_value(UiDocument* doc, const char* element_id);

/* Read current text content of an element */
const char* ui_get_element_text(UiDocument* doc, const char* element_id);

/* Read current CSS custom property value */
const char* ui_get_property(UiDocument* doc, const char* name);
```

Lua API:
```lua
local volume = ctx.ui:get_element_value(doc, "volume-slider")  -- "0.73"
local checked = ctx.ui:get_element_value(doc, "fullscreen-checkbox")  -- "true"/"false"
```

### Impact If Absent

Settings screens require developers to maintain parallel state in Lua for every interactive control. This shadow state gets out of sync with UI state on edge cases (focus loss, rapid changes). Common bug pattern in every Scaleform-era game.

**Complexity**: S
**Dependencies**: C ABI extension (additive, non-breaking), Lua binding

---

## Gap 7: UI State Machine for Menu Flow

**Severity**: HIGH

### Why It Is Critical

Every commercial game has a menu navigation graph: MainMenu → NewGame → CharacterCreation, or PauseMenu → Settings → AudioSettings → (back) → Settings → (back) → PauseMenu. UMG has a `FNavigationStack` via CommonUI. Coherent GT games typically implement this in JavaScript with a routing library. Scaleform games managed this in ActionScript with a screen manager class.

The current spec provides `show/hide` per document and `set_modal`. There is no built-in concept of: pushing a new screen on a stack, going back, knowing what screen was previous, or preventing duplicate pushes.

Without this, every game duplicates a menu state machine in Lua. This is error-prone: back-button behavior, transition animations that must play in reverse on pop, pause/resume of background audio when modal opens — all must be hand-coded.

### Technical Description

A `UINavigationStack` in the Lua API:

```lua
-- Stack-based menu navigation
local nav = ctx.ui:navigation_stack()

-- Push a new screen (shows it, may hide previous, plays enter transition)
nav:push("res://ui/settings.html", {
    transition = "slide-right",  -- built-in transition preset
    on_enter = function(doc)
        ctx.ui:set_property(doc, "volume", tostring(audio.volume))
    end,
    on_pop = function(doc, result)
        if result.accepted then
            audio.volume = tonumber(ctx.ui:get_element_value(doc, "volume-slider"))
        end
    end
})

-- Pop current screen (shows previous, plays exit transition)
nav:pop({ accepted = true })

-- Replace current screen (no back)
nav:replace("res://ui/game_over.html")

-- Clear stack (return to in-game HUD)
nav:clear()
```

The C++ implementation is a `std::vector<UiDocument*>` with load/unload lifecycle and transition sequencing.

### Impact If Absent

Every game reimplements menu navigation. Back button handling is inconsistent. Transitions are not coordinated. Scene loading from menus has race conditions with UI state. This is one of the most commonly reported polish issues in indie games.

**Complexity**: M
**Dependencies**: Gap 3 (transition animations), Lua API, UIRoot scene integration

---

## Gap 8: 3D World-Space UI (Billboard Panels)

**Severity**: MEDIUM (BLOCKER for specific game genres)

### Why It Is Critical

UMG supports world-space widget components (`UWidgetComponent` with `Screen` vs `World` space modes). Coherent GT supports 3D panels rendered to textures and applied to geometry. Scaleform had this via `GFxMovieTexture`. World-space UI is required for: NPC name plates floating above characters, health bars on enemies, interactive terminals in the game world, VR UI panels.

The current spec has a Z-order layering system (0–1000) and mentions "Phase 4: VR world-space UI panels" but provides no intermediate solution for world-space overlay UI that is critical for even non-VR games.

### Technical Description

Two sub-features are needed, at different complexity levels:

**Sub-feature A — Screen-space billboards at world positions (M complexity)**: Render a UI element at the 2D screen position that corresponds to a 3D world position. Useful for floating name plates and enemy health bars.

```lua
-- UIRoot with world_anchor instead of fixed screen position
-- UIPass projects world_pos to screen each frame
ctx.ui:set_world_anchor(nameplate_doc, node_ref, Vec3(0, 2.0, 0))  -- 2m above node
```

The UIPass already runs after 3D rendering, so the view-projection matrix is available to do the projection.

**Sub-feature B — Render-to-texture UI panels (XL complexity)**: Render a UI document to an off-screen texture (not to the main render target). The texture can then be applied to a 3D mesh in the scene as a material. This enables: computer terminals, VR panels, TV screens showing UI.

```lua
local terminal_ui = ctx.ui:load_to_texture("res://ui/terminal.html", 512, 512)
-- Returns a GpuTexture handle that can be used in a UIMaterial or directly in a material
material_node:set_texture("screen", terminal_ui:texture())
```

This requires UIPass to support an off-screen render target path, not just the main framebuffer composite.

### Impact If Absent

Sub-feature A absence: enemy health bars, NPC name plates, and interaction prompts must be implemented with separate meshes or billboard geometry in 3D, which is far more complex. Sub-feature B absence: interactive screens, VR UI, and world-embedded UI are impossible.

**Complexity**: A=M, B=XL
**Dependencies**: A=RenderGraph (world-to-screen projection in UIPass), B=off-screen render target, texture sharing with material system

---

## Gap 9: UI Profiler and Editor Inspector

**Severity**: MEDIUM (critical for production development)

### Why It Is Critical

Coherent GT ships a Chromium DevTools-compatible inspector that shows the live element tree, computed CSS properties, and paint timings. UMG has the Widget Reflector that shows the widget tree, visibility states, and hit test geometry. Without tooling, debugging UI layout bugs (why is this element not receiving click events? why is this health bar the wrong size?) requires print-debugging CSS variables — extremely slow.

The current spec mentions a visual UI editor in Phase 2+ but does not specify a runtime inspector or profiler.

### Technical Description

Two tools are needed:

**Tool 1 — UI Inspector (IPC-accessible)**: In editor mode, expose IPC commands that return the live document state:

```
ui.getElementTree   { doc_id }  →  JSON tree of all elements with computed styles, bounds, visibility
ui.getComputedStyle { doc_id, element_id }  →  all computed CSS properties for element
ui.getDrawList      { doc_id }  →  raw draw commands count, timing for last frame
ui.hitTest          { doc_id, x, y }  →  which element is at screen position
ui.getProperty      { doc_id, name }  →  current CSS custom property value
```

The VSCode extension renders this as a panel similar to browser DevTools Elements tab.

**Tool 2 — UI Frame Profiler**: Per-document, per-frame timings:
- Layout time (ms)
- Paint command count
- GPU draw call count from UIPass
- Texture memory used by this document

Exposed via `engine.status` IPC extension or a dedicated `ui.profile` command.

**Tool 3 — Overlay debug**: `uz_debug_ui_bounds=1` renders colored outlines around all UI element bounding boxes in the viewport. Helps diagnose hit test misses and layout bugs without leaving the game viewport.

### Impact If Absent

UI bugs that take minutes in other engines take hours in UzEngine. Layout issues, hit-test failures, and CSS property propagation bugs are diagnosed with guess-and-check. Slows all UI development significantly.

**Complexity**: M
**Dependencies**: C ABI additions (read-back functions), IPC server extension, VSCode panel

---

## Gap 10: Drag and Drop System

**Severity**: MEDIUM (BLOCKER for inventory/crafting UIs)

### Why It Is Critical

UMG has `OnDragDetected`, `OnDrop`, `OnDragEnter`, `OnDragLeave` with a `UDragDropOperation` object carrying arbitrary payload data. Coherent GT supports HTML5 drag events. Scaleform games implementing inventory dragging used custom mouse-tracking code. Any RPG, survival, crafting, or card game needs drag-and-drop for inventory management, equipment slots, spellbook, hotbar, etc.

The current event system (`UI_EVENT_CLICK`, `UI_EVENT_CHANGE`, `UI_EVENT_HOVER`) does not include drag events.

### Technical Description

Required C ABI additions:

```c
/* New event types */
#define UI_EVENT_DRAG_START   5
#define UI_EVENT_DRAG_MOVE    6
#define UI_EVENT_DRAG_END     7
#define UI_EVENT_DROP         8
#define UI_EVENT_DRAG_ENTER   9
#define UI_EVENT_DRAG_LEAVE  10

/* Extended UiEvent for drag operations */
typedef struct {
    const char* element_id;
    const char* element_class;
    int event_type;
    float value;
    const char* text_value;
    /* Drag-specific */
    float drag_x, drag_y;         /* current drag position (screen coords) */
    float drag_start_x, drag_start_y;
    const char* drag_source_id;   /* element_id where drag originated */
} UiEvent;  /* Extended, must remain ABI-compatible */
```

CSS support required: `cursor: grab`, `cursor: grabbing`, `user-select: none` (prevent text selection during drag).

Lua API:
```lua
ctx.ui:on_event(doc, "item-slot", function(ev)
    if ev.event_type == "drag_start" then
        self.dragged_item = ev.element_id
        ctx.ui:set_property(doc, "drag-icon", item_icon(self.dragged_item))
        ctx.ui:show(drag_ghost_doc)  -- show a ghost element following the cursor
    elseif ev.event_type == "drop" then
        local target_slot = ev.element_id
        swap_items(self.dragged_item, target_slot)
    end
end)
```

**Important ABI note**: Extending `UiEvent` with drag fields must be done by adding fields at the end of the struct to preserve ABI compatibility with existing callbacks.

### Impact If Absent

Inventory systems, equipment screens, crafting grids, hotbar customization, card games — all impossible without custom hit-testing in Lua. This represents a large class of game UI patterns that are commercially expected.

**Complexity**: M
**Dependencies**: C ABI extension, input system (mouse delta tracking), CSS cursor property

---

## Gap 11: Accessibility System

**Severity**: MEDIUM (legal requirement in some markets)

### Why It Is Critical

Accessibility is legally required for games sold in the European Union (European Accessibility Act, June 2025 enforcement) and increasingly in North America. UMG has accessibility metadata on all widgets. Coherent GT supports ARIA attributes passthrough to platform assistive technology APIs. Screen reader support, high-contrast mode, and subtitle/text scaling are table stakes for any game targeting a broad audience.

The current spec makes no mention of accessibility. Phase 4 mentions "screen reader support" as a single bullet without design.

### Technical Description

Required features in priority order:

**1. Text scaling**: A global `--ui-text-scale` CSS variable injected by the engine, settable by the player:
```lua
ctx.ui:set_global_property("text-scale", "1.5")  -- 150% for low-vision players
```
All UI fonts that use `font-size: calc(var(--base-size) * var(--ui-text-scale))` will scale automatically.

**2. High-contrast mode**: A body-level CSS class `uz-high-contrast` toggled by engine:
```lua
ctx.ui:set_body_class(doc, "high-contrast", true)
```
UI documents define alternate styles:
```css
.high-contrast .health-bar { background: black; border: 3px solid white; }
```

**3. ARIA attributes for screen readers**: Allow `aria-label`, `aria-role`, `aria-live`, `aria-describedby` on HTML elements. `uz_ui_html.dll` collects these and makes them available via a new C ABI function:
```c
/* Returns ARIA tree as JSON for accessibility service integration */
const char* ui_get_accessibility_tree(UiDocument* doc);
```

**4. Reduced motion**: Inject `--prefers-reduced-motion: 1` when player enables this setting. CSS transitions and UIAnimator check this:
```css
@media (prefers-reduced-motion: reduce) {  /* or via CSS variable */
    .animated { transition: none; }
}
```

**5. Color-blind modes**: Engine applies a post-process color matrix to UIPass output: `ui_set_color_filter(doc, ColorFilter::Deuteranopia)`.

### Impact If Absent

Legal exposure in EU markets. Exclusion of players with disabilities. Platform certification may fail (Sony requires basic accessibility on PS5 games). Negative press coverage.

**Complexity**: M
**Dependencies**: CSS engine (global variables, body classes), UIPass (color filter shader), IPC settings

---

## Gap 12: UI Theming System

**Severity**: MEDIUM

### Why It Is Critical

UMG has a `UWidgetStyleSheet` (experimental in UE5.4+) and robust `FSlateStyle` for editor theming. Coherent GT games commonly use CSS variable overrides for theming. Any game with an in-game settings screen that allows players to choose UI themes, or any game with a character/faction that affects UI skin, needs a theming system.

### Technical Description

The CSS custom property system is the right foundation, but the spec does not define a **theme loading mechanism**. Required:

**1. Theme files**: `.uzcss` files containing only CSS custom property overrides:

```css
/* res://ui/themes/sci-fi.uzcss */
:root {
    --primary-color: #00ffcc;
    --secondary-color: #003322;
    --font-family: "Mono";
    --border-radius: 0px;
    --panel-opacity: 0.9;
}
```

**2. Theme switching API**:
```lua
ctx.ui:load_theme("res://ui/themes/sci-fi.uzcss")  -- applies to all documents
ctx.ui:load_theme("res://ui/themes/fantasy.uzcss", {doc=hud_doc})  -- doc-specific
```

**3. Theme persistence**: Theme name saved in player prefs (accessible via save system).

**4. Dark/light mode**: A specific two-theme pattern with `--color-scheme: dark|light` variable.

### Impact If Absent

Changing UI visual style requires editing every HTML/CSS file. No support for player-chosen themes. Games with faction-based UI customization (Destiny-style) must duplicate entire HTML files.

**Complexity**: S
**Dependencies**: CSS engine (theme file loading, cascade override), save system (persist selection)

---

## Gap 13: Localization Integration in UI

**Severity**: HIGH

### Why It Is Critical

The localization spec (`docs/specs/localization.md`) defines `ctx.locale:t("ui.menu.play")` in Lua. The UI system spec defines HTML documents with static text. There is **no defined mechanism** for HTML documents to display localized text. The specs do not cross-reference each other on this point.

This is a critical integration gap: how does `<button>Play</button>` become `<button>Jogar</button>` in Portuguese? Three possible answers exist (none specced):

1. Game code calls `ui_set_property(doc, "btn-play-label", ctx.locale:t("ui.menu.play"))` and HTML uses `var(--btn-play-label)` in `content`. This means every string is a CSS property, which becomes unmanageable at scale.

2. HTML documents are regenerated per locale at Cook time (static localization, not runtime-switchable).

3. A template system: HTML uses `{{ui.menu.play}}` and `uz_ui_html.dll` resolves keys via a locale callback at parse time.

### Technical Description

The recommended approach is Option 3 combined with a runtime re-render trigger:

**C ABI addition**:
```c
/* Locale resolver callback — called by uz_ui_html.dll when it encounters {{key}} */
typedef const char* (*UiLocaleResolver)(const char* key, void* userdata);
void ui_set_locale_resolver(UiDocument* doc, UiLocaleResolver resolver, void* userdata);

/* Trigger re-layout with new locale (after locale change event) */
void ui_invalidate_locale(UiDocument* doc);
```

**HTML template syntax**:
```html
<button id="btn-play">{{ui.menu.play}}</button>
<p class="subtitle">{{ui.menu.subtitle}}</p>
```

**Lua wiring**:
```lua
-- At document load
ctx.ui:set_locale_resolver(hud_doc, function(key)
    return ctx.locale:t(key)
end)

-- When locale changes
ctx.event_bus:subscribe("locale.changed", function()
    ctx.ui:invalidate_locale(hud_doc)
end)
```

This approach means the HTML is authored once, locale switching works at runtime, and the CSS property namespace is not polluted with string values.

### Impact If Absent

Localization is disconnected from UI. Every localized string requires manual `ui_set_property` calls per string per document. A settings screen with 30 labels needs 30 property calls. Runtime locale switching requires full document reload. This is unsustainable at scale.

**Complexity**: M
**Dependencies**: Localization system (Phase 3), C ABI extension, EventBus integration

---

## Gap 14: Input Text Fields

**Severity**: MEDIUM

### Why It Is Critical

The current spec has `<input type="range">` (slider) but no `<input type="text">`. Text input is required for: player name entry, chat (multiplayer), search boxes in settings/asset pickers, save file naming, cheat code entry, console command input (already specced but needs the underlying widget).

The console command spec references `<input id="input" type="text">` in the console HTML, confirming this is expected to work — but it is not in the supported HTML elements table in `ui-system.md`. This is a direct inconsistency in the existing spec.

### Technical Description

`<input type="text">` requires:

1. **Text cursor rendering**: blinking caret at insertion point, rendered via UIPass.
2. **Selection highlight**: colored background on selected text region.
3. **IME support**: Input Method Editor for CJK languages (essential given Japanese is a specced locale). This is a Win32 `ImmGetContext`/`ImmSetCandidateWindow` integration.
4. **Clipboard**: Ctrl+C/Ctrl+V, platform clipboard API (`OpenClipboard`/`SetClipboardData`).
5. **Virtual keyboard on console**: Trigger platform OS keyboard (PSN/XBL keyboard API) when the element gains focus on a gamepad-only session.
6. **Password masking**: `<input type="password">` shows dots instead of characters.
7. **Max length**: `maxlength` attribute.

C ABI event additions:
```c
#define UI_EVENT_TEXT_INPUT  11  /* fired on every character entry, text_value = full current text */
#define UI_EVENT_TEXT_SUBMIT 12  /* fired on Enter, text_value = submitted text */
```

### Impact If Absent

No player name entry, no in-game chat, no search boxes, no console command input (contradicting the console-commands spec). The console spec references `<input type="text">` but it is not in the HTML element list — this is an existing internal inconsistency.

**Complexity**: M (L if full IME support)
**Dependencies**: Platform IME APIs (Win32), clipboard API, input system keyboard events

---

## Gap 15: Minimap and Radar Widget

**Severity**: MEDIUM

### Why It Is Critical

Minimap and radar are among the most common game UI elements. The spec mentions `<canvas>` elements with `UICanvas` C++ paint API, and `UIMaterial` for render-to-texture minimap updates. However, there is no higher-level minimap abstraction, no world-to-minimap coordinate transform API, no fog-of-war mask, no icon/marker system.

### Technical Description

A `UIMinimapCanvas` C++ class that wraps `UICanvas` with:

```cpp
class UIMinimapCanvas : public UICanvas {
public:
    UIMinimapCanvas(UiDocument* doc, std::string_view canvas_id);

    // Define world space bounds that map to the canvas
    void set_world_bounds(float world_x, float world_z, float world_width, float world_height);

    // Set center (follows player)
    void set_center(float world_x, float world_z);
    void set_rotation(float degrees);  // rotate minimap with camera

    // Render a render target as the base map (top-down ortho render)
    void set_map_texture(GpuTexture* ortho_render_target);

    // Add/update/remove icons (player, enemies, objectives)
    void add_marker(uint32_t id, float world_x, float world_z, const char* icon_res_path, float size);
    void update_marker(uint32_t id, float world_x, float world_z);
    void remove_marker(uint32_t id);

    // Fog of war
    void set_fog_mask(GpuTexture* fog_texture);

    void commit();
};
```

Lua access:
```lua
local minimap = ctx.ui:get_minimap_canvas(hud_doc, "minimap-canvas")
minimap:set_center(player.position.x, player.position.z)
minimap:update_marker(player_id, player.position.x, player.position.z)
minimap:commit()
```

### Impact If Absent

Every game with a minimap builds this from scratch in C++ by subclassing `UICanvas`. Common bugs include incorrect world-to-screen coordinate transforms and marker icon scaling. No shared implementation means no QA baseline.

**Complexity**: M
**Dependencies**: UICanvas, render-to-texture (for base map), GpuTexture sharing with material system

---

## Gap 16: Responsive Design — Reference Resolution Scaling

**Severity**: MEDIUM

### Why It Is Critical

This partially overlaps Gap 4 but focuses specifically on **automatic scaling of the entire UI document** when the viewport size changes, rather than individual element anchoring.

CSS `media queries` are excluded from the spec (correct decision for a fixed-resolution game), but there is no alternative for adapting UI to aspect ratio changes. A game designed for 16:9 at 1920x1080 must handle: 16:10 monitors (slightly taller), 21:9 ultrawide (much wider), 4:3 (old TVs in console compatibility modes).

### Technical Description

**Approach**: A `ui_scale_mode` property on UIRoot documents:

```ini
[node id=100 type=UIRoot name="HUD" parent=0]
ui_document = "res://ui/hud.html"
ui_reference_width = 1920
ui_reference_height = 1080
ui_scale_mode = "expand"   ; or "fit", "stretch", "fixed"
```

Scale modes:

- `fit`: Scale down uniformly if viewport is smaller. Add letterboxing/pillarboxing.
- `expand`: Scale up if viewport is larger, UI fills more screen real estate.
- `stretch`: Non-uniform scale to fill viewport exactly (usually wrong).
- `fixed`: No scaling, UI renders at reference resolution and is centered.

The UIPass applies a 2D transform to the entire document draw list based on the ratio of actual viewport to reference resolution.

### Impact If Absent

UI elements overlap or leave gaps on non-standard aspect ratios. Ultrawide users see stretched or poorly positioned HUD. 4:3 mode may clip UI elements. No guidance for designers on how to author for multiple resolutions.

**Complexity**: S
**Dependencies**: UIPass (apply 2D scale transform to draw list), .uzscene UIRoot properties

---

## Gap 17: Scrollable Text and Virtual List

**Severity**: MEDIUM

### Why It Is Critical

Scrollable text areas appear in: item descriptions, quest logs, dialog subtitles, end-game credits, patch notes screens, skill trees, talent descriptions. Virtual (windowed) lists are required for performance when displaying large collections (100+ inventory items, achievement lists, friend lists).

CSS `overflow: auto` with a scrollbar is standard web behavior. The current spec does not list `overflow` as a supported CSS property. Without it, text containers that exceed their bounds simply clip, with no scroll mechanism.

### Technical Description

Required CSS properties: `overflow: hidden`, `overflow: auto`, `overflow: scroll`, `overflow-y: auto`.

When `overflow: auto` is active and content overflows:
1. A scrollbar element is generated (styled via `::-webkit-scrollbar` pseudo-element or a custom `uz-scrollbar` selector).
2. Mouse wheel events and gamepad right-stick Y-axis scroll the content.
3. Gamepad focus navigation scrolls automatically when the focused element is outside the visible area.

**Virtual list**: For lists of 100+ items, a `uz-virtual-list` custom element (or a C++ `UIVirtualList` helper) that only renders the visible items:

```html
<uz-virtual-list id="item-list" item-height="60" visible-count="8">
    <!-- Items injected via ui_set_property(doc, "list-item-count", "1000") -->
    <!-- and ui_set_property(doc, "list-item-0-label", "Sword of Doom") etc. -->
</uz-virtual-list>
```

The virtual list implementation in C++ renders only the `visible-count` items that fall within the scroll viewport, updating item content as scroll position changes.

### Impact If Absent

Long item descriptions clip. Achievement lists with 100 entries cause layout performance issues. Scrollable quest logs are impossible without custom C++ canvas drawing. Credits screens require manual scrolling logic in Lua.

**Complexity**: M (overflow CSS) + L (virtual list)
**Dependencies**: CSS engine (overflow), input system (scroll events), UIAnimator (scroll inertia)

---

## Gap 18: UI Hot-Reload Without Frame Glitch

**Severity**: MEDIUM (developer experience)

### Why It Is Critical

The spec mentions hot-reload but does not describe the mechanism for **glitch-free** reload. Naive hot-reload (destroy document, recreate) causes a 1-2 frame gap where no UI is rendered, visible as a flash. Coherent GT hot-reload is transparent because it patches the running layout tree. UMG's UI hot-reload recompiles the `UMG Blueprint` and re-runs the `NativeConstruct`.

The spec also does not specify how CSS custom property state (all `ui_set_property` values) is preserved across reload, or how event callback registrations survive a reload.

### Technical Description

The hot-reload mechanism must:

1. **Parse the new HTML/CSS in a shadow document** while the current document continues to render.
2. **Swap atomically at frame boundary**: old document renders its final draw list, then new document takes over from frame N+1 with no gap.
3. **Transfer CSS property state**: before swapping, all current property values from the old document are applied to the new document. This preserves `--health`, `--score`, etc. across reload.
4. **Rebind event callbacks**: The Lua `ctx.ui:on_event` registrations are associated with the document path, not the document pointer. On reload, registrations are automatically re-applied to the new document.

C ABI internal: `ui_hot_reload(UiDocument* old_doc, const char* new_path)` returns a new `UiDocument*` with state transferred. The caller (UIRoot) swaps its pointer atomically.

### Impact If Absent

Hot-reload causes visible frame flashes, making it useless for iterating on animations and transitions. Event callbacks must be manually re-registered after each reload. CSS property state is lost, requiring game code re-initialization. Developers avoid triggering hot-reload, negating its value.

**Complexity**: M
**Dependencies**: UIPass (atomic document swap), Lua binding (path-based callback registry)

---

## Gap 19: UI Automated Testing

**Severity**: MEDIUM

### Why It Is Critical

UMG supports automated UI testing via Unreal's Functional Test framework. Coherent GT tests are done with Selenium-style element querying on the live game. Scaleform testing was notoriously difficult, causing many shipped UI bugs. As UzEngine targets 95% test coverage project-wide (per CLAUDE.md), the UI system needs testability infrastructure.

### Technical Description

Required testing capabilities:

**1. Headless document rendering**: `uz_ui_html.dll` already supports headless mode for the Cook stage. This mode should be extended to support full layout and hit-test queries without a GPU:

```c
/* Headless mode: layout and query without GPU */
UiDocument* ui_load_document_headless(const char* html_string);
/* Renders layout, populates element tree — no GPU draw list */
void ui_layout_headless(UiDocument* doc, int width, int height);
/* Query element bounds without rendering */
UiRect ui_get_element_bounds(UiDocument* doc, const char* element_id);
/* Query computed CSS property */
const char* ui_get_computed_style(UiDocument* doc, const char* element_id, const char* property);
```

**2. Input simulation for testing**:
```c
/* Simulate events in headless mode */
void ui_simulate_click(UiDocument* doc, float x, float y);
void ui_simulate_key(UiDocument* doc, int vkey);
void ui_simulate_text(UiDocument* doc, const char* text);
```

**3. Snapshot testing**: `ui_render_to_png(UiDocument* doc, int width, int height, const char* output_path)` for pixel-perfect regression testing.

**4. IPC test commands** (editor mode):
```
ui.simulate_click   { doc_id, x, y }
ui.simulate_key     { doc_id, vkey }
ui.get_element_bounds { doc_id, element_id }
```

### Impact If Absent

UI is undertested. CSS layout regressions are caught only by manual QA. Hot-reload introduces visual bugs that are not detected until someone looks at the screen. 95% coverage requirement cannot be met for UI code.

**Complexity**: M
**Dependencies**: Headless mode in uz_ui_html.dll, IPC extension, test framework integration

---

## Gap 20: HUD Subsystem Separation

**Severity**: LOW-MEDIUM

### Why It Is Critical

The current spec treats all UI documents identically (UIRoot nodes, same `show/hide` API). In practice, HUD documents (always visible, non-modal, updated every frame) have very different requirements from menu documents (shown on demand, modal, rarely updated):

- **HUD**: must never block frame, can skip layout if no properties changed (dirty-flag optimization), should be cached in a pre-rendered texture for expensive static elements.
- **Menus**: can afford expensive layout on first show, need focus system, need transition animations.

Treating them the same means either: HUD gets menu overhead (unnecessary layout recalculation every frame), or menus miss HUD optimizations.

### Technical Description

A `ui_hint` property on UIRoot documents:

```ini
[node id=100 type=UIRoot name="HUD" parent=0]
ui_document = "res://ui/hud.html"
ui_layer = 500
ui_hint = "hud"   ; "hud", "menu", "overlay" — affects rendering strategy
```

Internal implications:
- `hud`: UIPass uses a dirty-flag on `ui_set_property` calls. If no properties changed since last `ui_tick()`, the previous draw list is reused (skip layout + paint). Significant CPU savings for complex HUDs.
- `menu`: UIPass re-layouts on every frame (needed for animations). Also activates focus system.
- `overlay`: Rendered into a cached texture, blended as a single quad. Used for static HUD frames (health bar background, ammo counter frame) that never change.

Additionally, a separate `CUIHud` ECS component archetype alongside `CUIRoot` would allow different iteration/update strategies in the UIPass without branching.

### Impact If Absent

HUD with 10 custom properties recalculates layout every frame even when no properties changed. At 60 FPS, this is 60 unnecessary layout passes per second per HUD document. Profiling will reveal `ui_get_draw_list` as a hot spot in the render thread.

**Complexity**: S
**Dependencies**: UIPass dirty-flag tracking, C ABI extension (ui_set_dirty_flag), UIRoot properties

---

## Severity Summary

| # | Gap | Severity | Complexity | Phase Recommendation |
|---|-----|----------|------------|----------------------|
| 1 | Gamepad Focus/Navigation | CRITICAL | L | Phase 2 (with UI system) |
| 2 | Widget Library | HIGH | XL | Phase 2 start, Phase 3 complete |
| 3 | Animation System (tween/sequence) | HIGH | L | Phase 2 |
| 4 | Multi-Resolution / Safe Area | HIGH | M | Phase 2 |
| 5 | Rich Text Rendering | MEDIUM-HIGH | M | Phase 2 |
| 6 | Bidirectional Data Binding | MEDIUM-HIGH | S | Phase 2 (additive ABI) |
| 7 | UI State Machine (nav stack) | HIGH | M | Phase 2 |
| 8 | 3D World-Space UI | MEDIUM | A=M, B=XL | A=Phase 3, B=Phase 4 |
| 9 | UI Profiler / Inspector | MEDIUM | M | Phase 2 (dev tooling) |
| 10 | Drag and Drop | MEDIUM | M | Phase 3 |
| 11 | Accessibility | MEDIUM | M | Phase 3 |
| 12 | Theming System | MEDIUM | S | Phase 2 |
| 13 | Localization Integration | HIGH | M | Phase 3 (depends on locale spec) |
| 14 | Text Input Fields | MEDIUM | M-L | Phase 2 (console spec requires it) |
| 15 | Minimap / Radar Widget | MEDIUM | M | Phase 3 |
| 16 | Responsive Scaling | MEDIUM | S | Phase 2 |
| 17 | Scrollable Text / Virtual List | MEDIUM | M+L | Phase 2 (scroll) / Phase 3 (virtual) |
| 18 | Glitch-Free Hot-Reload | MEDIUM | M | Phase 2 |
| 19 | UI Automated Testing | MEDIUM | M | Phase 2 (required for 95% coverage) |
| 20 | HUD Subsystem Separation | LOW-MEDIUM | S | Phase 3 |

---

## Architectural Decisions That Must Be Made Now

The following decisions cannot be deferred without incurring significant retrofit cost:

**1. Event struct extensibility**: `UiEvent` must be defined with a `version` field and `size` field from day one, so adding drag fields, text input fields, and focus change data can be done without breaking existing callbacks that read the struct by offset:

```c
typedef struct {
    uint32_t struct_size;   /* sizeof(UiEvent) — caller checks this */
    uint32_t version;       /* increments on each additive change */
    const char* element_id;
    const char* element_class;
    int event_type;
    float value;
    const char* text_value;
    /* v2 additions below — only read if struct_size >= offsetof(UiEvent, drag_x) */
    float drag_x, drag_y;
    float drag_start_x, drag_start_y;
    const char* drag_source_id;
} UiEvent;
```

**2. Document identity vs pointer**: The current API identifies documents by `UiDocument*` pointer. Hot-reload swaps the underlying pointer. Any long-lived references (Lua closures capturing the pointer) will point to a freed document. The API should transition to a stable document handle (integer ID or `StringId` of the resource path) so hot-reload is transparent to Lua code.

**3. Property namespace**: CSS custom properties are a flat namespace. With 20+ UI documents and 100+ properties, collision becomes a real concern. The spec should establish conventions (e.g., properties prefixed with document name: `hud-health`, `hud-ammo`, `menu-selected-item`) or provide a scoping mechanism.

**4. Locale integration approach**: Must be decided before the HTML authoring format is finalized. `{{key}}` template syntax in HTML is incompatible with standard HTML parsers. Alternatively, the `content: attr(data-locale-key)` CSS pattern avoids parser changes. This decision affects every UI document authored.

---

## Internal Spec Inconsistencies Found

1. **`<input type="text">` in console spec but missing from HTML element table**: `docs/specs/console-commands.md` references `<input id="input" type="text">` but `docs/specs/ui-system.md` table lists only `<input type="range">`. Gap 14 addresses this.

2. **Sort key range conflict**: `docs/specs/ui-system.md` says sort_key 0–100 = "World UI" and 100–500 = "HUDs", but `docs/modules/ui/README.md` says 0–99 = "Behind 3D geometry" and 100–499 = "World-space UI" and 500–699 = "HUD layer". These ranges do not match. The module README is more detailed and likely more recent — the spec should be updated to match.

3. **Media queries contradiction**: `docs/specs/ui-system.md` says "No media queries (viewport is fixed in-game)" under CSS support. But Gap 4 (Multi-Resolution) reveals this assumption is incorrect for a cross-resolution game. The spec should change this to "No `@media` queries (use CSS variables for adaptive layout instead)" with documentation of the reference resolution scaling system.

4. **`ui_free_document` vs `ui_unload_document`**: The system prompt C ABI shows `ui_free_document(doc)` but `docs/specs/ui-system.md` and `docs/modules/ui/README.md` show `ui_unload_document(doc)`. Both cannot be correct. The stable C ABI must have exactly one name for this function.

5. **`ui_deliver_input` vs `ui_inject_mouse`/`ui_inject_key`**: The system prompt shows `bool ui_deliver_input(UiDocument*, const UiInputEvent*)` returning a bool (consumed). The specs show `void ui_inject_mouse(...)` and `void ui_inject_key(...)` with no return value. These are incompatible APIs for the same function. The consolidated `ui_deliver_input` approach (returning bool for consumed) is architecturally superior and should be the canonical form.
