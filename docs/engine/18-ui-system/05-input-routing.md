# UI System — Input Routing & Focus Management

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/ui/ui_input_context.h/.cpp`, `engine/input/input_manager.h`
> **Depends on**: Input system (§13), HTML/CSS engine (§18.2)

## Purpose

UI needs to intercept and consume input events before the game sees them. Additionally, focused elements (text fields, buttons) need keyboard input routing. This requires:

- **Modal vs Passthrough modes** — UI can be modal (blocks all input) or passthrough (lets uncaptured events through)
- **Focus management** — which element gets keyboard input
- **Event capture/bubbling** — how events propagate through DOM
- **Keyboard navigation** — Tab to move focus, Enter to click

## Input Context Stack

The input manager maintains a priority stack of input contexts. UI is one of these contexts:

```cpp
// Input priority stack (highest priority first)
enum class InputContextPriority : int32_t {
    Debug = 1000,        // imgui (highest)
    UI = 500,            // game UI
    Game = 0,            // gameplay input (lowest)
};

class InputManager {
private:
    std::vector<InputContext*> context_stack;  // sorted by priority

public:
    void RegisterContext(InputContext* ctx, InputContextPriority priority);
    void UnregisterContext(InputContext* ctx);

    void OnKeyDown(KeyCode key) {
        // Poll contexts in priority order
        for (auto& ctx : context_stack) {
            bool consumed = ctx->OnKeyDown(key);
            if (consumed) return;  // event consumed
        }
    }
};

// UI input context
class UIInputContext : public InputContext {
public:
    bool OnKeyDown(KeyCode key) override;
    bool OnMouseMove(Vec2 pos) override;
    bool OnMouseButtonDown(MouseButton button) override;
};
```

## Modal vs Passthrough

### Modal Mode

UI captures **all** input. Game receives nothing.

```cpp
class UIInputContext {
public:
    enum class InputMode { Modal, Passthrough };

    void SetInputMode(InputMode mode) { input_mode = mode; }

    bool OnKeyDown(KeyCode key) override {
        // Modal: always consume
        if (input_mode == InputMode::Modal) {
            DispatchKeyToFocusedElement(key);
            return true;  // consumed
        }

        // Passthrough: let unhandled events through
        // ...
        return false;
    }

private:
    InputMode input_mode = InputMode::Passthrough;
};
```

**Use cases:**
- **Modal**: Main menu, pause menu, inventory screen, dialog box — player is focused on UI
- **Passthrough**: HUD (health bar, minimap, chat), in-game UI overlays — player is primarily controlling the game

### Passthrough Mode

Unhandled events pass to the game:

```cpp
bool UIInputContext::OnMouseMove(Vec2 pos) override {
    // Always update UI element hover state
    HtmlElement* hovered = document->GetElementAtPoint(pos);
    if (hovered != last_hovered) {
        last_hovered->OnMouseLeave();
        hovered->OnMouseEnter();
        last_hovered = hovered;
    }

    // In passthrough mode, always return false (let game handle too)
    if (input_mode == InputMode::Passthrough) {
        return false;  // not consumed
    }

    return true;  // consumed (modal mode)
}

bool UIInputContext::OnMouseButtonDown(MouseButton button) override {
    HtmlElement* target = last_hovered;
    if (target) {
        target->OnMouseDown(button);
        return true;  // consumed if hovering UI
    }

    // Not hovering UI
    return false;  // let game handle
}
```

## Focus Management

### Focused Element

Only one element receives keyboard input at a time:

```cpp
class UIInputContext {
private:
    HtmlElement* focused_element = nullptr;

public:
    void SetFocus(HtmlElement* elem) {
        if (focused_element == elem) return;

        if (focused_element) {
            focused_element->OnBlur();  // lost focus
        }

        focused_element = elem;
        if (focused_element) {
            focused_element->OnFocus();  // gained focus
        }
    }

    HtmlElement* GetFocusedElement() const { return focused_element; }

    bool OnKeyDown(KeyCode key) override {
        if (!focused_element) return false;

        // Send key to focused element
        focused_element->OnKeyDown(key);
        return true;  // consumed by focused element
    }
};
```

### Focusable Elements

Only certain elements can receive focus:

```cpp
// In HTML
<input type="text" id="search">      <!-- focusable -->
<button id="play">Play</button>       <!-- focusable -->
<div id="display">Score: 100</div>   <!-- not focusable -->
```

```cpp
bool CanFocus(HtmlElement* elem) {
    return elem->tag_name == "INPUT" ||
           elem->tag_name == "TEXTAREA" ||
           elem->tag_name == "SELECT" ||
           elem->tag_name == "BUTTON" ||
           elem->HasAttribute("tabindex");
}
```

## Keyboard Navigation

### Tab Navigation

Tab key moves focus to next focusable element:

```cpp
bool UIInputContext::OnKeyDown(KeyCode key) override {
    if (key == KeyCode::Tab) {
        // Collect all focusable elements in document order
        std::vector<HtmlElement*> focusable;
        CollectFocusable(document->GetRoot(), focusable);

        // Find current focus position
        int current_idx = -1;
        for (int i = 0; i < focusable.size(); ++i) {
            if (focusable[i] == focused_element) {
                current_idx = i;
                break;
            }
        }

        // Move focus
        bool shift_held = GetKeyState(KeyCode::LShift);
        int next_idx = (current_idx + (shift_held ? -1 : 1) + focusable.size()) % focusable.size();

        SetFocus(focusable[next_idx]);
        return true;  // consumed
    }

    // Delegate other keys to focused element
    if (focused_element) {
        focused_element->OnKeyDown(key);
        return true;
    }

    return false;
}
```

### Enter & Space for Buttons

```cpp
bool UIInputContext::OnKeyDown(KeyCode key) override {
    if (focused_element && focused_element->tag_name == "BUTTON") {
        if (key == KeyCode::Return || key == KeyCode::Space) {
            // Simulate click
            focused_element->OnClick();
            return true;
        }
    }

    return false;
}
```

## Event Capture & Bubbling

### Event Propagation

Events start at the target and bubble up:

```cpp
bool HtmlElement::DispatchEvent(const HtmlEvent& event) {
    // Capture phase (top-down)
    HtmlElement* elem = this;
    std::vector<HtmlElement*> ancestors;
    while (elem) {
        ancestors.push_back(elem);
        elem = elem->GetParent();
    }
    std::reverse(ancestors.begin(), ancestors.end());

    // Dispatch capture listeners
    for (auto& ancestor : ancestors) {
        if (ancestor->capture_listeners[event.type]) {
            ancestor->capture_listeners[event.type](event);
        }
    }

    // Bubble phase (bottom-up)
    elem = this;
    while (elem) {
        if (elem->bubble_listeners[event.type]) {
            elem->bubble_listeners[event.type](event);
        }

        if (event.propagation_stopped) break;  // event.stopPropagation() called

        elem = elem->GetParent();
    }

    return event.default_prevented;  // event.preventDefault() called
}
```

### Event Handler Registration

```cpp
// C++
element->AddEventListener("click", [](const HtmlEvent& e) {
    std::cout << "Element clicked\n";
});

element->AddEventListener("input", [](const HtmlEvent& e) {
    std::string value = e.GetAttribute("value");
    std::cout << "Input changed to: " << value << "\n";
});

// HTML inline
<button onclick="HandleClick()">Click Me</button>

// But since we don't support JavaScript, inline handlers are parsed but need C++ registration
document->RegisterInlineHandler("HandleClick", [](const HtmlEvent&) {
    // ...
});
```

## Text Input

### Text Input Element

```html
<input type="text" id="search" placeholder="Search...">
```

```cpp
class TextInputElement : public HtmlElement {
private:
    std::string text_content;
    size_t cursor_position = 0;
    size_t selection_start = 0, selection_end = 0;

public:
    void OnKeyDown(KeyCode key) override {
        if (key >= KeyCode::A && key <= KeyCode::Z) {
            // Printable character
            char c = GetCharFromKeyCode(key);
            text_content.insert(cursor_position, 1, c);
            cursor_position++;
            NotifyChanged();  // triggers "input" event
        } else if (key == KeyCode::Backspace) {
            if (cursor_position > 0) {
                text_content.erase(cursor_position - 1, 1);
                cursor_position--;
                NotifyChanged();
            }
        } else if (key == KeyCode::Delete) {
            if (cursor_position < text_content.length()) {
                text_content.erase(cursor_position, 1);
                NotifyChanged();
            }
        } else if (key == KeyCode::Left) {
            if (cursor_position > 0) cursor_position--;
        } else if (key == KeyCode::Right) {
            if (cursor_position < text_content.length()) cursor_position++;
        }
    }

    void NotifyChanged() {
        HtmlEvent event;
        event.type = "input";
        event.target = this;
        event.data["value"] = text_content;
        DispatchEvent(event);
    }

    std::string GetValue() const { return text_content; }
    void SetValue(StringView val) { text_content = val; cursor_position = val.length(); }
};
```

## Controller/Gamepad Support

### Button Mapping

Controller buttons can also trigger UI events:

```cpp
enum class GamepadButton {
    A,       // Confirm / Click
    B,       // Cancel / Go back
    X,       // Cancel
    Y,       // Help / Info
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    LB,      // Focus left
    RB,      // Focus right
};

bool UIInputContext::OnGamepadButtonDown(GamepadButton button) override {
    if (!focused_element) return false;

    if (button == GamepadButton::A) {
        // Confirm (like Enter on keyboard)
        if (focused_element->tag_name == "BUTTON") {
            focused_element->OnClick();
            return true;
        } else if (focused_element->tag_name == "INPUT") {
            focused_element->OnKeyDown(KeyCode::Return);
            return true;
        }
    } else if (button == GamepadButton::DPadDown) {
        // Move focus down (like Tab)
        MoveFocus(1);
        return true;
    } else if (button == GamepadButton::DPadUp) {
        // Move focus up (like Shift-Tab)
        MoveFocus(-1);
        return true;
    }

    return false;
}
```

## Input Method Editor (IME) Support

For non-Latin text input (Chinese, Japanese, Korean):

```cpp
class TextInputElement {
private:
    std::string ime_composition;  // text being composed but not yet committed
    size_t ime_cursor = 0;

public:
    void OnIMEComposition(StringView text, size_t cursor) {
        ime_composition = text;
        ime_cursor = cursor;
        // Display composition text with underline
        InvalidateRender();
    }

    void OnIMECommit(StringView text) {
        // Composition complete, commit final text
        text_content.insert(cursor_position, text);
        cursor_position += text.length();
        ime_composition.clear();
        NotifyChanged();
    }
};
```

## Hit Testing

### Point-in-Element Detection

```cpp
HtmlElement* HtmlDocument::GetElementAtPoint(Vec2 point) const {
    return GetElementAtPointRecursive(root.get(), point);
}

HtmlElement* GetElementAtPointRecursive(HtmlElement* elem, Vec2 point) {
    // Check if point is within element's box
    const Box& box = elem->GetLayoutBox();
    if (!box.Contains(point)) {
        return nullptr;
    }

    // If display=none, skip
    if (elem->GetComputedStyle().display == Display::None) {
        return nullptr;
    }

    // Check children in reverse order (top-to-bottom, rightmost first)
    std::vector<HtmlElement*> children = elem->GetChildren();
    std::reverse(children.begin(), children.end());

    for (auto& child : children) {
        HtmlElement* result = GetElementAtPointRecursive(child, point);
        if (result) return result;  // highest z-order element found
    }

    // No child found, return this element
    return elem;
}
```

## Hover Effects

### Hover State Tracking

```cpp
class UIInputContext {
private:
    HtmlElement* hovered_element = nullptr;

    void UpdateHover(Vec2 mouse_pos) {
        HtmlElement* new_hovered = document->GetElementAtPoint(mouse_pos);

        if (new_hovered != hovered_element) {
            if (hovered_element) {
                hovered_element->RemoveClass("hover");
                hovered_element->OnMouseLeave();
            }

            hovered_element = new_hovered;
            if (hovered_element) {
                hovered_element->AddClass("hover");
                hovered_element->OnMouseEnter();
            }
        }
    }
};
```

### CSS Hover Selector

```css
button:hover {
    background-color: var(--hover-color);
    cursor: pointer;
}

button:active {
    transform: scale(0.95);
}

input:focus {
    outline: 2px solid var(--focus-color);
}
```

## Rules

1. **ALWAYS** check input_mode before consuming input — passthrough mode must let events through
2. **NEVER** dispatch keyboard events to unfocused elements — only focused element gets key input
3. **ALWAYS** update hover state on mouse move — even in passthrough mode
4. **NEVER** prevent Tab key in non-text fields — Tab must navigate focus
5. **ALWAYS** support Escape to close modal UI — standard UX expectation
