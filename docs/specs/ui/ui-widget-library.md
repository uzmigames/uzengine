# UI Widget Library

> **Status**: Planned
> **Phase**: 2
> **Priority**: High — provides standard, reusable UI controls for all game menus and HUDs

---

## Overview

The **UI Widget Library** provides a collection of custom HTML elements (`<uz-*>`) that extend the base HTML subset with common game UI controls. Each widget is:

- **Declarative**: Define structure and style purely in HTML/CSS
- **Accessible**: Full keyboard navigation and ARIA support
- **Themeable**: CSS custom properties (`--uz-*`) for complete visual customization
- **Lua-bindable**: All events dispatch back to game code via `ctx.ui:on_event()`
- **Performant**: Lightweight, no JavaScript, rendered by uz_ui_html.dll

Widgets are **not** custom JavaScript components — they are rendering primitives built directly into the UI library's C++ core.

---

## uz-button

Clickable button with hover, active, and focus states.

### HTML Usage

```html
<uz-button id="btn-play" class="primary">
  Play
</uz-button>

<uz-button id="btn-settings" class="secondary" disabled>
  Settings
</uz-button>

<uz-button data-focusable="true" data-focus-right="btn-settings">
  Play
</uz-button>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Unique identifier for event binding |
| `class` | string | CSS class for styling (e.g., `primary`, `secondary`, `danger`) |
| `disabled` | bool | Disables click and focus |
| `data-focusable` | bool | Included in keyboard navigation focus graph |
| `data-focus-up` / `data-focus-down` / `data-focus-left` / `data-focus-right` | string | ID of element to focus on arrow key |
| `data-focus-default` | bool | Set initial focus to this button on document load |

### CSS Custom Properties

```css
--uz-button-bg-normal:      #2a2a2a
--uz-button-bg-hover:       #3a3a3a
--uz-button-bg-active:      #1a1a1a
--uz-button-bg-disabled:    #1a1a1a
--uz-button-text-color:     #ffffff
--uz-button-text-disabled:  #888888
--uz-button-border-radius:  4px
--uz-button-border-width:   1px
--uz-button-border-color:   #555555
--uz-button-padding:        8px 16px
--uz-button-font-size:      14px
--uz-button-font-weight:    500
--uz-button-transition:     background-color 0.2s, border-color 0.2s
```

### Pseudo-Classes

- `:hover` — Mouse over the button
- `:active` — Mouse button pressed down
- `:focus` — Button has keyboard focus (visible outline)
- `:disabled` — Button is disabled (opacity reduced, cursor not-allowed)

### Events

| Event | Trigger | Lua Callback |
|-------|---------|--------------|
| `click` | Button clicked (mouse or Enter key) | `event_data.event_type == "click"` |
| `focus` | Button gains keyboard focus | `event_data.event_type == "focus"` |
| `blur` | Button loses keyboard focus | `event_data.event_type == "blur"` |

### Lua Binding

```lua
-- Simple click handler
ctx.ui:on_event(hud, "btn-play", function(event_data)
    if event_data.event_type == "click" then
        ctx.scene:set_paused(false)
    end
end)

-- Handler for all buttons in a class
ctx.ui:on_event_class(hud, "menu-button", function(event_data)
    if event_data.event_type == "click" then
        print("Menu button clicked:", event_data.element_id)
    end
end)
```

### Example — Themed Buttons

```html
<style>
  uz-button.primary {
    --uz-button-bg-normal: #0066ff;
    --uz-button-bg-hover: #0052cc;
    --uz-button-bg-active: #003d99;
  }

  uz-button.secondary {
    --uz-button-bg-normal: #666666;
    --uz-button-bg-hover: #777777;
    --uz-button-bg-active: #555555;
  }

  uz-button.danger {
    --uz-button-bg-normal: #cc0000;
    --uz-button-bg-hover: #ff0000;
    --uz-button-bg-active: #990000;
  }

  uz-button:focus {
    outline: 2px solid #ffff00;
    outline-offset: 2px;
  }
</style>

<uz-button class="primary" id="btn-yes">Yes</uz-button>
<uz-button class="secondary" id="btn-no">No</uz-button>
<uz-button class="danger" id="btn-delete">Delete</uz-button>
```

### Accessibility

- **Keyboard**: Tab to focus, Enter to activate
- **ARIA**: `role="button"`, `aria-pressed` for toggle buttons
- **Focus ring**: Visible outline when focused (configurable via `--uz-button-outline-color`)

---

## uz-slider

Range slider with min/max values, step size, and draggable thumb.

### HTML Usage

```html
<uz-slider id="vol-slider" min="0" max="100" value="50" step="1">
</uz-slider>

<uz-slider id="sens-slider" min="0.1" max="3.0" value="1.5" step="0.1">
</uz-slider>

<label for="brightness">Brightness</label>
<uz-slider id="brightness" min="0" max="1" value="0.8" step="0.01">
</uz-slider>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Unique identifier |
| `min` | number | Minimum value (default: 0) |
| `max` | number | Maximum value (default: 100) |
| `value` | number | Current value |
| `step` | number | Increment per unit (default: 1) |
| `disabled` | bool | Disables dragging |
| `data-focusable` | bool | Include in focus navigation |

### CSS Custom Properties

```css
--uz-slider-track-height:      4px
--uz-slider-track-bg:          #333333
--uz-slider-track-active-bg:   #0066ff
--uz-slider-thumb-size:        20px
--uz-slider-thumb-bg:          #ffffff
--uz-slider-thumb-shadow:      0 2px 8px rgba(0,0,0,0.3)
--uz-slider-border-radius:     2px
--uz-slider-transition:        background-color 0.2s
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `change` | User drags thumb or uses arrow keys | `event_data.value` (0.0–1.0 normalized) |
| `input` | Each frame while dragging | `event_data.value` |
| `focus` | Slider gains focus | — |
| `blur` | Slider loses focus | — |

**Note**: `change` fires on mouse release or key press. `input` fires continuously while dragging for real-time feedback.

### Lua Binding

```lua
-- Volume slider: update audio system in real-time
ctx.ui:on_event(hud, "vol-slider", function(event_data)
    if event_data.event_type == "input" then
        local norm_value = event_data.value  -- 0.0–1.0
        ctx.audio:set_bus_volume("master", norm_value)
    elseif event_data.event_type == "change" then
        -- Save final value to settings
        save_setting("master_volume", event_data.value)
    end
end)
```

### Example — Stacked Sliders with Labels

```html
<style>
  .setting-row {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 8px 0;
  }

  .setting-label {
    width: 120px;
    font-size: 14px;
    color: #ffffff;
  }

  .setting-slider {
    flex: 1;
    min-width: 200px;
  }

  .setting-value {
    width: 60px;
    text-align: right;
    font-size: 12px;
    color: #aaaaaa;
  }
</style>

<div class="setting-row">
  <span class="setting-label">Master Volume</span>
  <uz-slider id="master-vol" class="setting-slider" min="0" max="1" value="0.8" step="0.01"></uz-slider>
  <span class="setting-value" id="master-vol-text">80%</span>
</div>

<div class="setting-row">
  <span class="setting-label">Brightness</span>
  <uz-slider id="brightness" class="setting-slider" min="0" max="1" value="1.0" step="0.01"></uz-slider>
  <span class="setting-value" id="brightness-text">100%</span>
</div>
```

### Keyboard Navigation

- **Left/Right arrows**: Decrease/increase by `step`
- **Home/End**: Jump to min/max
- **Page Down/Page Up**: Jump by 10× `step`

---

## uz-checkbox

Toggle checkbox with optional label.

### HTML Usage

```html
<uz-checkbox id="fullscreen">
  Fullscreen
</uz-checkbox>

<uz-checkbox id="vsync" checked>
  V-Sync
</uz-checkbox>

<label for="mute-audio">
  <uz-checkbox id="mute-audio"></uz-checkbox>
  Mute Audio
</label>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Unique identifier |
| `checked` | bool | Initial checked state |
| `disabled` | bool | Disables toggling |
| `data-focusable` | bool | Include in focus graph |

### CSS Custom Properties

```css
--uz-checkbox-size:          20px
--uz-checkbox-bg-unchecked:  #333333
--uz-checkbox-bg-checked:    #0066ff
--uz-checkbox-border-color:  #555555
--uz-checkbox-border-width:  2px
--uz-checkbox-border-radius: 2px
--uz-checkbox-checkmark:     "✓"
--uz-checkbox-checkmark-size: 14px
--uz-checkbox-transition:    background-color 0.2s, border-color 0.2s
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `change` | User toggles checkbox | `event_data.value` (true/false) |
| `focus` | Checkbox gains focus | — |
| `blur` | Checkbox loses focus | — |

### Lua Binding

```lua
ctx.ui:on_event(hud, "fullscreen", function(event_data)
    if event_data.event_type == "change" then
        local enabled = event_data.value
        ctx.scene:set_fullscreen(enabled)
        save_setting("fullscreen", enabled)
    end
end)
```

### Example — Settings Toggle Group

```html
<fieldset class="settings-section">
  <legend>Graphics</legend>

  <div class="checkbox-group">
    <uz-checkbox id="fullscreen" checked data-focusable="true">
      Fullscreen
    </uz-checkbox>
    <uz-checkbox id="vsync" checked data-focusable="true">
      V-Sync
    </uz-checkbox>
    <uz-checkbox id="bloom">
      Bloom
    </uz-checkbox>
  </div>
</fieldset>

<style>
  .settings-section {
    border: 1px solid #555555;
    padding: 16px;
    border-radius: 4px;
  }

  legend {
    font-weight: bold;
    padding: 0 8px;
  }

  .checkbox-group {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }
</style>
```

---

## uz-radio-group

Exclusive selection group where only one option can be selected at a time.

### HTML Usage

```html
<uz-radio-group id="difficulty" selected="normal">
  <uz-radio value="easy">Easy</uz-radio>
  <uz-radio value="normal">Normal</uz-radio>
  <uz-radio value="hard">Hard</uz-radio>
  <uz-radio value="impossible">Impossible</uz-radio>
</uz-radio-group>
```

### Attributes

**uz-radio-group**:

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Group identifier |
| `selected` | string | Value of initially selected radio |
| `disabled` | bool | Disable all radios in group |
| `data-focusable` | bool | Tab into group, arrow keys navigate |

**uz-radio**:

| Attribute | Type | Description |
|-----------|------|-------------|
| `value` | string | Value when selected |
| `disabled` | bool | Disable this specific radio |

### CSS Custom Properties

```css
--uz-radio-size:             20px
--uz-radio-bg-unchecked:     #333333
--uz-radio-bg-checked:       #0066ff
--uz-radio-bg-disabled:      #1a1a1a
--uz-radio-border-color:     #555555
--uz-radio-border-width:     2px
--uz-radio-border-radius:    50%
--uz-radio-dot-size:         10px
--uz-radio-dot-bg:           #ffffff
--uz-radio-transition:       background-color 0.2s
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `change` | Selection changes | `event_data.value` (selected value) |
| `focus` | Group gains focus | — |
| `blur` | Group loses focus | — |

### Lua Binding

```lua
ctx.ui:on_event(hud, "difficulty", function(event_data)
    if event_data.event_type == "change" then
        local difficulty = event_data.value
        ctx.scene:set_difficulty(difficulty)
        save_setting("difficulty", difficulty)
    end
end)
```

### Example — Difficulty Selection Menu

```html
<div class="menu-section">
  <h2>Difficulty</h2>

  <uz-radio-group id="difficulty" selected="normal" data-focusable="true">
    <div class="radio-item">
      <uz-radio value="easy"></uz-radio>
      <label for="easy">Easy - Tutorial Mode</label>
    </div>

    <div class="radio-item">
      <uz-radio value="normal"></uz-radio>
      <label for="normal">Normal - Recommended</label>
    </div>

    <div class="radio-item">
      <uz-radio value="hard"></uz-radio>
      <label for="hard">Hard - Challenging</label>
    </div>

    <div class="radio-item">
      <uz-radio value="impossible"></uz-radio>
      <label for="impossible">Impossible - Masochist</label>
    </div>
  </uz-radio-group>
</div>

<style>
  .radio-item {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 8px 0;
  }
</style>
```

### Keyboard Navigation

- **Tab**: Enter group (focuses first selected radio)
- **Arrow Up/Down**: Cycle through options
- **Space/Enter**: Select focused option

---

## uz-dropdown

Select dropdown with keyboard navigation and search support.

### HTML Usage

```html
<uz-dropdown id="resolution">
  <option value="1920x1080">1920×1080</option>
  <option value="2560x1440">2560×1440</option>
  <option value="3840x2160">3840×2160</option>
</uz-dropdown>

<label for="language">Language:</label>
<uz-dropdown id="language" selected="en">
  <option value="en">English</option>
  <option value="pt-br">Português (Brasil)</option>
  <option value="es">Español</option>
  <option value="fr">Français</option>
</uz-dropdown>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Unique identifier |
| `selected` | string | Initially selected option value |
| `disabled` | bool | Disables dropdown |
| `searchable` | bool | Show search box in dropdown menu |
| `data-focusable` | bool | Include in focus graph |

### CSS Custom Properties

```css
--uz-dropdown-bg-normal:     #333333
--uz-dropdown-bg-hover:      #444444
--uz-dropdown-bg-selected:   #0066ff
--uz-dropdown-text-color:    #ffffff
--uz-dropdown-border-color:  #555555
--uz-dropdown-border-width:  1px
--uz-dropdown-border-radius: 4px
--uz-dropdown-padding:       8px 12px
--uz-dropdown-menu-bg:       #2a2a2a
--uz-dropdown-menu-shadow:   0 4px 12px rgba(0,0,0,0.3)
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `change` | Selection changes | `event_data.value` (selected value) |
| `open` | Menu opens | — |
| `close` | Menu closes | — |
| `focus` | Dropdown gains focus | — |
| `blur` | Dropdown loses focus | — |

### Lua Binding

```lua
ctx.ui:on_event(hud, "language", function(event_data)
    if event_data.event_type == "change" then
        local lang = event_data.value
        ctx.locale:set(lang)
        save_setting("language", lang)
    end
end)
```

### Example — Resolution & Language Selectors

```html
<div class="settings-panel">
  <div class="setting">
    <label for="resolution">Resolution:</label>
    <uz-dropdown id="resolution" selected="1920x1080">
      <option value="1280x720">1280×720</option>
      <option value="1920x1080">1920×1080</option>
      <option value="2560x1440">2560×1440</option>
      <option value="3840x2160">3840×2160</option>
    </uz-dropdown>
  </div>

  <div class="setting">
    <label for="language">Language:</label>
    <uz-dropdown id="language" selected="en" searchable>
      <option value="en">English</option>
      <option value="pt-br">Português (Brasil)</option>
      <option value="es">Español</option>
      <option value="fr">Français</option>
      <option value="de">Deutsch</option>
      <option value="ja">日本語</option>
    </uz-dropdown>
  </div>
</div>

<style>
  .setting {
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 8px 0;
  }

  .setting label {
    width: 120px;
  }

  uz-dropdown {
    flex: 1;
  }
</style>
```

### Keyboard Navigation

- **Tab**: Focus dropdown
- **Enter/Space**: Open menu
- **Arrow Up/Down**: Navigate options in menu
- **Enter**: Select focused option
- **Escape**: Close menu without selection
- **Type letter**: Jump to option starting with letter (if searchable)

---

## uz-scroll-view

Virtualized scrollable container for efficient rendering of large lists.

### HTML Usage

```html
<uz-scroll-view id="inventory" width="400" height="600">
  <div class="item">Item 1</div>
  <div class="item">Item 2</div>
  <div class="item">Item 3</div>
  <!-- ... hundreds of items, only visible ones are rendered -->
</uz-scroll-view>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Unique identifier |
| `width` | number | Container width in pixels |
| `height` | number | Container height in pixels |
| `item-height` | number | Height of each item (for virtualization) |
| `disabled` | bool | Disables scrolling |

### CSS Custom Properties

```css
--uz-scroll-bg:              #1a1a1a
--uz-scroll-track-bg:        #2a2a2a
--uz-scroll-thumb-bg:        #555555
--uz-scroll-thumb-bg-hover:  #666666
--uz-scroll-border-radius:   4px
--uz-scroll-border-color:    #333333
--uz-scroll-border-width:    1px
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `scroll` | Scrolled to new position | `event_data.value` (0.0–1.0 normalized) |
| `top` | Scrolled to top | — |
| `bottom` | Scrolled to bottom | — |

### Lua Binding

```lua
-- Detect when user scrolls near bottom (lazy load more items)
ctx.ui:on_event(hud, "inventory", function(event_data)
    if event_data.event_type == "bottom" then
        load_more_inventory_items()
    end
end)
```

### Example — Virtualized Inventory List

```html
<uz-scroll-view id="inventory" width="400" height="500" item-height="60">
  <!-- Dynamically populated by Lua script -->
  <!-- Only ~8 items visible at a time, rest virtualized -->
</uz-scroll-view>

<style>
  uz-scroll-view {
    --uz-scroll-thumb-bg: #0066ff;
  }

  uz-scroll-view .item {
    display: flex;
    align-items: center;
    padding: 8px;
    border-bottom: 1px solid #333333;
  }

  uz-scroll-view .item-icon {
    width: 40px;
    height: 40px;
    background-color: #2a2a2a;
    margin-right: 8px;
    border-radius: 2px;
  }

  uz-scroll-view .item-name {
    flex: 1;
  }

  uz-scroll-view .item-count {
    font-size: 12px;
    color: #aaaaaa;
  }
</style>
```

### Performance

- **Virtualization**: Only visible items + 1 buffer row above/below are rendered
- **Target**: Handle 10,000+ items with <2ms scroll update time
- **Memory**: O(visible_items) instead of O(total_items)

---

## uz-list-view

Data-bound list with item template and row selection.

### HTML Usage

```html
<uz-list-view id="characters" template="character-row" selected-row="0">
  <!-- Item template below, repeated for each data element -->
</uz-list-view>

<template id="character-row">
  <div class="character-row" data-focusable="true">
    <img class="avatar" src="res://ui/avatar-${data.id}.png"/>
    <div class="info">
      <h3>${data.name}</h3>
      <p>Level ${data.level}</p>
    </div>
  </div>
</template>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | List identifier |
| `template` | string | ID of `<template>` element to repeat |
| `selected-row` | number | Initially selected row index |
| `multi-select` | bool | Allow selecting multiple rows |

### CSS Custom Properties

```css
--uz-list-row-bg-normal:     #1a1a1a
--uz-list-row-bg-hover:      #2a2a2a
--uz-list-row-bg-selected:   #0066ff
--uz-list-row-text-selected: #ffffff
--uz-list-row-padding:       8px
--uz-list-row-border-bottom: 1px solid #333333
--uz-list-transition:        background-color 0.15s
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `select` | Row selected | `event_data.value` (row index or indices if multi-select) |
| `double-click` | Row double-clicked | `event_data.value` (row index) |
| `focus` | List gains focus | — |
| `blur` | List loses focus | — |

### Lua Binding — Populate from Data

```lua
local characters = {
    {id=1, name="Warrior", level=15},
    {id=2, name="Mage", level=12},
    {id=3, name="Rogue", level=18},
}

ctx.ui:set_list_data(hud, "characters", characters)

ctx.ui:on_event(hud, "characters", function(event_data)
    if event_data.event_type == "select" then
        local selected_index = event_data.value
        local character = characters[selected_index + 1]
        show_character_details(character)
    end
end)
```

### Example — Character Selection Screen

```html
<div class="character-select-panel">
  <h2>Select Character</h2>

  <uz-list-view id="characters" template="character-row" selected-row="0">
  </uz-list-view>

  <div class="actions">
    <uz-button id="btn-play" class="primary">Play</uz-button>
    <uz-button id="btn-delete" class="danger">Delete</uz-button>
  </div>
</div>

<template id="character-row">
  <div class="character-row">
    <img class="avatar" src="res://ui/avatar-${data.id}.png" width="60" height="60"/>
    <div class="info">
      <h3>${data.name}</h3>
      <p>Level ${data.level} • ${data.class}</p>
    </div>
  </div>
</template>

<style>
  .character-select-panel {
    padding: 32px;
    max-width: 600px;
  }

  uz-list-view {
    height: 400px;
    border: 1px solid #555555;
    margin-bottom: 16px;
    border-radius: 4px;
  }

  .character-row {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .avatar {
    border-radius: 2px;
  }

  .info h3 {
    margin: 0;
    font-size: 16px;
  }

  .info p {
    margin: 4px 0 0 0;
    font-size: 12px;
    color: #aaaaaa;
  }

  .actions {
    display: flex;
    gap: 8px;
  }
</style>
```

### Keyboard Navigation

- **Tab**: Focus list
- **Arrow Up/Down**: Navigate rows
- **Enter**: Select/confirm
- **Escape**: Deselect

---

## uz-modal

Modal dialog overlay with backdrop and focus trapping.

### HTML Usage

```html
<uz-modal id="confirm-dialog" title="Confirm Action" auto-dismiss="true">
  <p>Are you sure you want to delete this item?</p>

  <div class="modal-actions">
    <uz-button id="btn-confirm" class="danger">Delete</uz-button>
    <uz-button id="btn-cancel" class="secondary">Cancel</uz-button>
  </div>
</uz-modal>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Modal identifier |
| `title` | string | Modal title text |
| `visible` | bool | Initially visible (default: false, show via Lua) |
| `auto-dismiss` | bool | Close on backdrop click |
| `modal` | bool | Capture all input (default: true) |

### CSS Custom Properties

```css
--uz-modal-bg:               rgba(0,0,0,0.7)
--uz-modal-content-bg:       #1a1a1a
--uz-modal-border-color:     #555555
--uz-modal-border-width:     1px
--uz-modal-border-radius:    4px
--uz-modal-shadow:           0 8px 32px rgba(0,0,0,0.5)
--uz-modal-padding:          24px
--uz-modal-title-font-size:  18px
--uz-modal-title-font-weight: bold
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `open` | Modal shows | — |
| `close` | Modal hides | — |
| `backdrop-click` | User clicks backdrop | — |

### Lua Binding

```lua
ctx.ui:on_event(hud, "confirm-dialog", function(event_data)
    if event_data.event_type == "close" then
        print("Modal closed")
    end
end)

ctx.ui:on_event(hud, "btn-confirm", function(event_data)
    if event_data.event_type == "click" then
        delete_item()
        ctx.ui:hide_modal(hud, "confirm-dialog")
    end
end)

-- Show modal
ctx.ui:show_modal(hud, "confirm-dialog")
```

### Example — Confirmation Dialog

```html
<uz-modal id="delete-confirm" title="Delete Item?" auto-dismiss="true">
  <div class="modal-content">
    <p>Are you sure you want to delete this item?</p>
    <p class="warning">This action cannot be undone.</p>
  </div>

  <div class="modal-actions">
    <uz-button id="delete-confirm-yes" class="danger">Delete</uz-button>
    <uz-button id="delete-confirm-no" class="secondary">Cancel</uz-button>
  </div>
</uz-modal>

<style>
  uz-modal {
    --uz-modal-content-bg: #2a2a2a;
  }

  .modal-content {
    margin-bottom: 16px;
  }

  .modal-content p {
    margin: 0 0 8px 0;
  }

  .modal-content p.warning {
    color: #ff6666;
    font-size: 12px;
  }

  .modal-actions {
    display: flex;
    gap: 8px;
    justify-content: flex-end;
  }
</style>
```

### Focus Trapping

- All keyboard focus is trapped within the modal
- Tab cycles through focusable elements inside modal
- Escape key closes modal
- Backdrop click closes modal (if `auto-dismiss=true`)

---

## uz-toast

Transient notification that auto-dismisses.

### HTML Usage

```html
<uz-toast id="save-toast" duration="3000">
  Game saved successfully!
</uz-toast>

<uz-toast id="error-toast" type="error" duration="5000">
  Failed to load save file.
</uz-toast>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Toast identifier |
| `type` | string | `info`, `success`, `warning`, `error` (default: info) |
| `duration` | number | Auto-dismiss time in milliseconds (0 = manual) |
| `position` | string | `top-left`, `top-center`, `top-right`, `bottom-left`, `bottom-center`, `bottom-right` |

### CSS Custom Properties

```css
--uz-toast-bg-info:       #0066ff
--uz-toast-bg-success:    #00aa00
--uz-toast-bg-warning:    #ffaa00
--uz-toast-bg-error:      #ff0000
--uz-toast-text-color:    #ffffff
--uz-toast-border-radius: 4px
--uz-toast-padding:       12px 16px
--uz-toast-shadow:        0 4px 12px rgba(0,0,0,0.3)
--uz-toast-margin:        8px
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `show` | Toast appears | — |
| `dismiss` | Toast auto-dismisses | — |

### Lua Binding

```lua
-- Show success toast
ctx.ui:show_toast(hud, "save-toast", {
    type = "success",
    message = "Game saved!",
    duration = 2000
})

-- Show error toast with longer duration
ctx.ui:show_toast(hud, "error-toast", {
    type = "error",
    message = "Failed to connect to server.",
    duration = 5000
})
```

### Example — Toast Notifications

```html
<div id="toast-container" style="position: fixed; top: 20px; right: 20px; z-index: 1000;">
  <!-- Toasts dynamically added here -->
</div>

<!-- Toast templates -->
<template id="toast-success">
  <uz-toast type="success" duration="3000">
    ${data.message}
  </uz-toast>
</template>

<template id="toast-error">
  <uz-toast type="error" duration="5000">
    ${data.message}
  </uz-toast>
</template>
```

---

## uz-tab-bar

Tabbed panel switcher with multiple panes.

### HTML Usage

```html
<uz-tab-bar id="settings-tabs" selected="graphics">
  <uz-tab value="graphics">Graphics</uz-tab>
  <uz-tab value="audio">Audio</uz-tab>
  <uz-tab value="gameplay">Gameplay</uz-tab>
</uz-tab-bar>

<div class="tab-panels">
  <div id="graphics-panel" class="tab-panel">
    <!-- Graphics settings -->
  </div>
  <div id="audio-panel" class="tab-panel">
    <!-- Audio settings -->
  </div>
  <div id="gameplay-panel" class="tab-panel">
    <!-- Gameplay settings -->
  </div>
</div>
```

### Attributes

**uz-tab-bar**:

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Tab bar identifier |
| `selected` | string | Initially selected tab value |
| `data-focusable` | bool | Include in focus graph |

**uz-tab**:

| Attribute | Type | Description |
|-----------|------|-------------|
| `value` | string | Unique tab identifier |
| `disabled` | bool | Disable this tab |

### CSS Custom Properties

```css
--uz-tab-bg-normal:     #2a2a2a
--uz-tab-bg-hover:      #333333
--uz-tab-bg-active:     #0066ff
--uz-tab-text-normal:   #aaaaaa
--uz-tab-text-active:   #ffffff
--uz-tab-border-bottom: 2px solid #0066ff
--uz-tab-padding:       8px 16px
--uz-tab-transition:    background-color 0.2s
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `change` | Tab selected | `event_data.value` (tab value) |

### Lua Binding

```lua
ctx.ui:on_event(hud, "settings-tabs", function(event_data)
    if event_data.event_type == "change" then
        local selected_tab = event_data.value
        show_tab_panel(selected_tab)
    end
end)
```

### Example — Settings Tabs

```html
<div class="settings-page">
  <h1>Settings</h1>

  <uz-tab-bar id="settings-tabs" selected="graphics" data-focusable="true">
    <uz-tab value="graphics">Graphics</uz-tab>
    <uz-tab value="audio">Audio</uz-tab>
    <uz-tab value="gameplay">Gameplay</uz-tab>
  </uz-tab-bar>

  <div class="tab-content">
    <div id="graphics-panel" class="tab-panel">
      <h2>Graphics Settings</h2>
      <uz-slider id="brightness" min="0" max="1" value="1.0" step="0.01"></uz-slider>
    </div>

    <div id="audio-panel" class="tab-panel" style="display: none;">
      <h2>Audio Settings</h2>
      <uz-slider id="master-vol" min="0" max="1" value="0.8" step="0.01"></uz-slider>
    </div>

    <div id="gameplay-panel" class="tab-panel" style="display: none;">
      <h2>Gameplay Settings</h2>
      <uz-checkbox id="screen-shake">Screen Shake</uz-checkbox>
    </div>
  </div>
</div>

<style>
  .tab-panel {
    padding: 16px;
  }

  .tab-panel h2 {
    margin-top: 0;
  }
</style>
```

### Keyboard Navigation

- **Tab**: Focus tab bar
- **Arrow Left/Right**: Switch tabs
- **Home/End**: Jump to first/last tab

---

## uz-tooltip

Hover/focus tooltip with automatic positioning.

### HTML Usage

```html
<uz-button id="btn-save" data-tooltip="Save Game (Ctrl+S)">
  Save
</uz-button>

<img src="res://ui/icon.png"
     data-tooltip="This is a health potion. Restores 50 HP."
     data-tooltip-delay="500"/>
```

### Attributes

| Attribute | Type | Description |
|-----------|------|-------------|
| `id` | string | Tooltip identifier (optional) |
| `data-tooltip` | string | Tooltip text content |
| `data-tooltip-delay` | number | Delay before showing (ms, default: 500) |
| `data-tooltip-position` | string | `top`, `bottom`, `left`, `right` (auto if not set) |

### CSS Custom Properties

```css
--uz-tooltip-bg:           rgba(0,0,0,0.9)
--uz-tooltip-text-color:   #ffffff
--uz-tooltip-padding:      6px 10px
--uz-tooltip-border-radius: 2px
--uz-tooltip-font-size:    12px
--uz-tooltip-arrow-size:   6px
--uz-tooltip-shadow:       0 2px 8px rgba(0,0,0,0.5)
```

### Events

| Event | Trigger | Data |
|-------|---------|------|
| `show` | Tooltip appears | — |
| `hide` | Tooltip disappears | — |

### Example — Rich Tooltip Content

```html
<div class="inventory-item" data-tooltip-id="item-tooltip">
  <img src="res://ui/sword.png" width="40" height="40"/>
</div>

<uz-tooltip id="item-tooltip" data-tooltip-position="right">
  <h4>Iron Sword</h4>
  <p>Damage: 15–20</p>
  <p>Weight: 3.5 kg</p>
</uz-tooltip>
```

---

## Summary Table

| Widget | Purpose | Key Feature |
|--------|---------|------------|
| `uz-button` | Clickable button | Hover, active, focus states |
| `uz-slider` | Range input | Real-time value feedback |
| `uz-checkbox` | Boolean toggle | Simple on/off |
| `uz-radio-group` | Exclusive choice | One option selected |
| `uz-dropdown` | Select from list | Keyboard search |
| `uz-scroll-view` | Large list container | Virtualization (10k+ items) |
| `uz-list-view` | Data-bound list | Template repetition, row selection |
| `uz-modal` | Dialog overlay | Focus trapping, auto-dismiss |
| `uz-toast` | Notification | Auto-dismiss with timer |
| `uz-tab-bar` | Panel tabs | Multi-pane switcher |
| `uz-tooltip` | Hover help | Auto-position, delay |

---

## Performance Targets

- **Widget render**: <1ms per visible widget
- **List virtualization**: Handle 10,000 items with <2ms scroll update
- **Modal focus trap**: <0.1ms per keystroke
- **Tooltip delay**: Configurable 500–2000ms default

---

## Accessibility Checklist

All widgets implement:

- ARIA roles and attributes (`role`, `aria-label`, `aria-pressed`, `aria-selected`)
- Keyboard navigation (Tab, arrows, Enter, Escape)
- Focus indicators (visible outline, customizable color)
- Color contrast (WCAG AA minimum)
- Screen reader support (semantic HTML)

---

## CSS Custom Property Reference

All `--uz-*` variables are inherited, allowing global theme override:

```css
:root {
  --uz-button-bg-normal: #2a2a2a;
  --uz-button-bg-hover: #3a3a3a;
  --uz-slider-thumb-size: 20px;
  /* ... all --uz-* properties cascade to widgets */
}
```

---

## Related Documentation

- [ui-system.md](ui-system.md) — Core UI system architecture
- [ui-animation.md](ui-animation.md) — Animation and tweening system
- [../guides/ui-guide.md](../guides/ui-guide.md) — Practical UI development guide
- [../engine/ui-architecture.md](../engine/ui-architecture.md) — Implementation details

