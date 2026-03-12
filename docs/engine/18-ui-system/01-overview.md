# UI System — HTML/CSS Reactive

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/ui/`
> **References**: UE5 UMG/Slate (clean-room), Godot Control nodes, Vue.js reactive patterns

## Purpose

Game UI is built with **HTML/CSS** rendered via an embedded browser layer, using a **reactive framework** inspired by Vue.js patterns. HTML/CSS is well-understood by LLMs — easy to generate and modify. Debug UI (engine/editor overlays) uses imgui separately.

## Architecture

```
┌─────────────────────────────────────────┐
│  Game UI Layer (HTML/CSS/JS)            │
│                                         │
│  ┌──────────┐   data binding   ┌──────┐│
│  │ Reactive  │◄───────────────►│ C++  ││
│  │ Templates │   events        │ State││
│  │ (Vue-style)│◄───────────────►│ Lua  ││
│  └──────────┘                  │ State││
│                                └──────┘│
└─────────────────────────────────────────┘
```

## Design Principles

- **Reactive data binding** — C++ and Lua expose observable state to UI; UI updates automatically on change
- **Event system** — UI emits events consumed by C++ or Lua (button clicks, slider changes, input fields)
- **CSS styling** — standard CSS for layout, animations, transitions
- **Component-based** — reusable UI components (health bar, inventory slot, dialog box) as HTML templates
- **LLM-friendly** — HTML/CSS is the most LLM-friendly UI format
- **Debug UI** — imgui for engine/editor debug overlays (separate from game UI)

## Binding API (C++)

```cpp
// Expose C++ state to UI
ui->Bind("player.health", &playerHealth);  // auto-updates UI on change
ui->Bind("inventory", &inventoryList);     // reactive list binding

// Listen for UI events
ui->On("buy_item", [](const UIEvent& e) {
    auto itemId = e.Get<std::string>("id");
    // handle purchase
});
```

## Binding API (Lua)

```lua
-- Expose Lua state to UI
ui:bind("score", score_value)
ui:bind("quest_log", quest_entries)

-- Listen for UI events
ui:on("dialog_choice", function(event)
    local choice = event.choice_id
    -- handle dialog
end)
```

## Input Routing

UI has two input modes:

| Mode | Behavior |
|------|----------|
| **Modal** | UI captures all input. Game receives nothing. Used for: menus, dialogs, inventory. |
| **Passthrough** | UI receives input first, uncaptured events pass to game. Used for: HUD, minimap. |

The input system's context priority stack (§13) handles this — a modal UI adds `IMC_UI` at high priority that consumes all keys.

## UIRoot Scene Node

```cpp
/// Scene node that hosts an HTML UI document.
/// Attach to an entity. Renders as an overlay or world-space quad.
class UIRoot : public SceneComponent {
public:
    std::string ui_document;     // path to HTML file
    bool        is_world_space = false;  // false = screen overlay, true = 3D billboard
    Vec2        size = {1920, 1080};     // render resolution

    /// Send a CSS custom property update to the document.
    /// The UI re-renders reactively when properties change.
    void SetProperty(StringView name, StringView value);
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| UI framework | UMG (Slate C++) | Control nodes | uGUI / UI Toolkit | **HTML/CSS reactive** |
| Data binding | Manual (C++ delegates) | Signals | Data binding (UI Toolkit) | **Reactive (Vue-style)** |
| Styling | USlateBrush (C++) | Theme resources | USS (CSS-like) | **Standard CSS** |
| LLM authoring | Poor (Slate API) | Medium | Medium (UXML) | **Excellent (HTML/CSS)** |
| Debug UI | Slate | Custom | IMGUI packages | **imgui (separate)** |
| World-space UI | Widget component | Control + SubViewport | World Space Canvas | **UIRoot (world_space flag)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | UIRoot node, HTML rendering, CSS custom property binding, basic input routing |
| **Phase 4** | Full reactive framework, Lua binding, world-space UI, font SDF atlas, UI hot-reload |
