# UI Development Guide

> **For**: Game developers authoring UI in HTML/CSS + Lua
> **Topics**: Creating HUDs, menus, animations, data binding, world-space UI
> **Requires**: Knowledge of HTML/CSS basics, Lua scripting

---

## Quick Start: Your First HUD

Let's create a simple health bar HUD that updates in real-time.

### Step 1: Create the HTML

Create a file `res://ui/hud.html`:

```html
<!DOCTYPE html>
<html>
<head>
    <style>
        .hud {
            position: fixed;
            top: 20px;
            left: 20px;
            width: 300px;
            font-family: "UzDefault";
            color: #ffffff;
        }

        .health-section {
            margin-bottom: 20px;
        }

        .label {
            font-size: 14px;
            margin-bottom: 4px;
            text-transform: uppercase;
            letter-spacing: 1px;
            color: #cccccc;
        }

        .health-bar {
            width: 100%;
            height: 30px;
            background-color: #1a1a1a;
            border: 2px solid #555555;
            border-radius: 2px;
            overflow: hidden;
        }

        .health-bar .fill {
            height: 100%;
            width: calc(var(--health) * 100%);
            background-color: #00ff00;
            transition: width 0.2s ease-out;
        }

        .ammo-bar {
            width: 100%;
            height: 20px;
            background-color: #1a1a1a;
            border: 1px solid #555555;
            border-radius: 2px;
            overflow: hidden;
            margin-top: 8px;
        }

        .ammo-bar .fill {
            height: 100%;
            width: calc(var(--ammo) * 100%);
            background-color: #ffaa00;
            transition: width 0.1s linear;
        }

        .stats {
            display: flex;
            gap: 16px;
            margin-top: 16px;
            font-size: 12px;
        }

        .stat {
            flex: 1;
            text-align: center;
        }

        .stat-value {
            font-size: 16px;
            font-weight: bold;
            color: #00ff00;
        }

        .stat-label {
            font-size: 10px;
            color: #aaaaaa;
            text-transform: uppercase;
            margin-top: 2px;
        }
    </style>
</head>
<body>
    <div class="hud">
        <!-- Health Section -->
        <div class="health-section">
            <div class="label">Health</div>
            <div class="health-bar">
                <div class="fill"></div>
            </div>
            <div style="font-size: 11px; color: #aaaaaa; margin-top: 4px;">
                <span id="health-text">100</span> / <span id="health-max">100</span>
            </div>
        </div>

        <!-- Ammo Section -->
        <div class="health-section">
            <div class="label">Ammo</div>
            <div class="ammo-bar">
                <div class="fill"></div>
            </div>
            <div style="font-size: 11px; color: #aaaaaa; margin-top: 4px;">
                <span id="ammo-text">30</span> / 120
            </div>
        </div>

        <!-- Stats Grid -->
        <div class="stats">
            <div class="stat">
                <div class="stat-value" id="level-text">1</div>
                <div class="stat-label">Level</div>
            </div>
            <div class="stat">
                <div class="stat-value" id="xp-text">0%</div>
                <div class="stat-label">XP</div>
            </div>
            <div class="stat">
                <div class="stat-value" id="fps-text">60</div>
                <div class="stat-label">FPS</div>
            </div>
        </div>
    </div>
</body>
</html>
```

### Step 2: Create the Lua Controller

Create `scripts/ui_hud.lua`:

```lua
-- Global HUD state
local hud_doc = nil
local player_ref = nil

function setup_hud()
    -- Load the HTML document
    hud_doc = ctx.ui:load("res://ui/hud.html")
    if not hud_doc then
        ctx.log:error("Failed to load HUD")
        return false
    end

    -- Show it immediately
    ctx.ui:show(hud_doc)

    -- Find player node
    local players = ctx.scene:find_by_tag("player")
    if #players > 0 then
        player_ref = players[1]
    end

    return true
end

function update_hud()
    if not hud_doc or not player_ref then
        return
    end

    -- Calculate normalized values (0.0–1.0)
    local health_norm = math.max(0, math.min(1, player_ref.health / player_ref.max_health))
    local ammo_norm = math.max(0, math.min(1, player_ref.ammo / player_ref.max_ammo))
    local xp_norm = player_ref.xp / player_ref.xp_for_level_up

    -- Update CSS custom properties
    ctx.ui:set_properties(hud_doc, {
        health = tostring(health_norm),
        ammo = tostring(ammo_norm)
    })

    -- Update text values (CSS can't interpolate numbers)
    ctx.ui:set_property(hud_doc, "health-text", tostring(math.floor(player_ref.health)))
    ctx.ui:set_property(hud_doc, "health-max", tostring(player_ref.max_health))
    ctx.ui:set_property(hud_doc, "ammo-text", tostring(player_ref.ammo))
    ctx.ui:set_property(hud_doc, "level-text", tostring(player_ref.level))
    ctx.ui:set_property(hud_doc, "xp-text", string.format("%d%%", math.floor(xp_norm * 100)))

    -- Health bar color change (critical health = red)
    if health_norm < 0.25 then
        ctx.ui:set_property(hud_doc, "health-bar-color", "#ff0000")
    else
        ctx.ui:set_property(hud_doc, "health-bar-color", "#00ff00")
    end
end

function cleanup_hud()
    if hud_doc then
        ctx.ui:unload(hud_doc)
        hud_doc = nil
    end
end
```

### Step 3: Wire It Up

In your scene script (`scripts/scene.lua`):

```lua
function on_start()
    setup_hud()
end

function on_update(dt)
    update_hud()
end

function on_stop()
    cleanup_hud()
end
```

**Result**: A health bar that updates smoothly every frame with animations!

---

## HTML/CSS Fundamentals

### Document Structure

All UI documents follow this pattern:

```html
<!DOCTYPE html>
<html>
<head>
    <style>
        /* CSS goes here */
    </style>
</head>
<body>
    <!-- HTML elements -->
</body>
</html>
```

### Supported Elements

| Element | Use | Example |
|---------|-----|---------|
| `<div>`, `<span>` | Layout containers | `<div class="panel">...</div>` |
| `<p>`, `<h1>–<h6>` | Text blocks | `<h2>Health</h2>` |
| `<img>` | Images | `<img src="res://ui/icon.png">` |
| `<button>` | Clickable button | `<button id="play">Play</button>` |
| `<uz-*>` | Custom widgets | `<uz-slider>`, `<uz-checkbox>`, etc. |

### Flexbox Layout

Use Flexbox for responsive layouts:

```html
<style>
    .toolbar {
        display: flex;
        flex-direction: row;          /* Left-to-right (default) */
        gap: 8px;                     /* Space between items */
        align-items: center;          /* Vertically center */
        justify-content: space-between; /* Space items evenly */
        padding: 16px;
    }

    .toolbar-item {
        flex: 1;                      /* Equal width */
    }
</style>

<div class="toolbar">
    <button>Back</button>
    <h2>Settings</h2>
    <button>Save</button>
</div>
```

### CSS Custom Properties (Variables)

Game code sets properties, CSS reads them:

```css
.health-bar .fill {
    width: calc(var(--health) * 100%);
    background-color: var(--health-color, #00ff00);  /* Default if not set */
}
```

Lua code:

```lua
ctx.ui:set_property(hud, "health", "0.75")
ctx.ui:set_property(hud, "health-color", "#ff0000")
```

### Transitions (Smooth Animations)

CSS transitions automatically animate property changes:

```css
.health-bar .fill {
    transition: width 0.2s ease-out, background-color 0.3s;
}
```

When the width changes, it animates smoothly over 0.2 seconds!

---

## Data Binding

### One-Way Binding (CSS Variables)

Game code → CSS variable → Display:

```html
<div class="progress-bar">
    <div class="fill" style="width: calc(var(--progress) * 100%)"></div>
    <span class="text">var(--progress-text)</span>
</div>
```

```lua
-- Every frame
ctx.ui:set_property(hud, "progress", tostring(current_progress))
ctx.ui:set_property(hud, "progress-text", string.format("%d%%", current_progress * 100))
```

**Advantages**:
- Simple and declarative
- No JavaScript needed
- Automatic CSS transitions

**When to use**:
- Numeric values (health, ammo, scores)
- Status display (on/off, active/inactive)
- Color themes

### Text Content with data-l10n-key

For localized strings:

```html
<button data-l10n-key="ui.menu.play">Play</button>
```

When the language is changed, the button text automatically updates from the localization file.

---

## Widget Library Overview

### Buttons

```html
<uz-button id="btn-play" class="primary" data-focusable="true">
    Play Game
</uz-button>
```

```lua
ctx.ui:on_event(menu, "btn-play", function(event_data)
    if event_data.event_type == "click" then
        start_game()
    end
end)
```

### Sliders

```html
<uz-slider id="volume" min="0" max="1" value="0.8" step="0.01"></uz-slider>
```

```lua
ctx.ui:on_event(menu, "volume", function(event_data)
    if event_data.event_type == "input" then
        -- Real-time feedback
        ctx.audio:set_bus_volume("master", event_data.value)
    elseif event_data.event_type == "change" then
        -- Final value, save to settings
        save_setting("master_volume", event_data.value)
    end
end)
```

### Checkboxes

```html
<uz-checkbox id="fullscreen" checked>
    Fullscreen Mode
</uz-checkbox>
```

```lua
ctx.ui:on_event(menu, "fullscreen", function(event_data)
    if event_data.event_type == "change" then
        ctx.scene:set_fullscreen(event_data.value)
    end
end)
```

### Dropdowns

```html
<uz-dropdown id="language" selected="en">
    <option value="en">English</option>
    <option value="pt-br">Português</option>
    <option value="es">Español</option>
</uz-dropdown>
```

### List Views

```html
<uz-list-view id="inventory" template="item-template">
</uz-list-view>

<template id="item-template">
    <div class="item-row">
        <img src="res://ui/${data.icon}" width="40">
        <span>${data.name}</span>
        <span class="qty">x${data.quantity}</span>
    </div>
</template>
```

```lua
local items = {
    {id=1, name="Health Potion", quantity=5, icon="potions/health.png"},
    {id=2, name="Mana Potion", quantity=3, icon="potions/mana.png"},
}

ctx.ui:set_list_data(inventory_ui, "inventory", items)
```

### Modals

```html
<uz-modal id="confirm-delete" title="Delete Item?" auto-dismiss="true">
    <p>Are you sure?</p>
    <div class="actions">
        <uz-button id="confirm-yes" class="danger">Delete</uz-button>
        <uz-button id="confirm-no" class="secondary">Cancel</uz-button>
    </div>
</uz-modal>
```

```lua
ctx.ui:show_modal(inventory_ui, "confirm-delete")
```

---

## Animations & Transitions

### Simple Fade-In

```lua
ctx.ui:tween(menu, "#logo", {opacity = 1.0}, {
    duration = 0.5,
    easing = "ease-out"
})
```

### Slide Menu

```lua
ctx.ui:tween(menu, "#panel", {
    translateX = 0,      -- Slide from left
    opacity = 1.0
}, {
    duration = 0.3,
    easing = "ease-out"
})
```

### Sequence (Staggered Animation)

```lua
ctx.ui:sequence(menu, {
    {selector="#title", props={opacity=1}, duration=0.3, easing="ease-out"},
    {wait=0.2},
    {
        selector=".menu-item",
        props={opacity=1, translateX=0},
        duration=0.3,
        easing="ease-out",
        stagger=0.1  -- Each item starts 0.1s after previous
    }
})
```

### Page Navigation

```lua
local nav = ctx.ui:navigation_stack(root_menu)

-- Navigate to settings menu with slide transition
nav:push("settings-menu", {
    transition = "slide",
    direction = "right",
    duration = 0.3
})

-- Go back
nav:pop({transition = "slide", direction = "left"})
```

### CSS Transitions (Automatic)

```css
.panel {
    opacity: 0;
    transition: opacity 0.3s ease-out;
}

.panel.visible {
    opacity: 1;
}
```

```lua
-- Add class, transition happens automatically
ctx.ui:animate_class(menu, ".panel", "visible", {
    on_complete = function()
        print("Panel is now visible")
    end
})
```

---

## Keyboard Navigation & Gamepad Support

### Make Elements Focusable

```html
<uz-button id="play" data-focusable="true" data-focus-right="settings">
    Play
</uz-button>

<uz-button id="settings" data-focusable="true" data-focus-left="play">
    Settings
</uz-button>
```

### Set Initial Focus

```html
<uz-button id="play" data-focusable="true" data-focus-default="true">
    Play
</uz-button>
```

### Style Focused Elements

```css
button:focus {
    outline: 2px solid #ffff00;
    outline-offset: 2px;
}
```

### Tab Navigation

- **Tab**: Move focus forward
- **Shift+Tab**: Move focus backward
- **Arrow Keys**: Use data-focus-* hints to navigate
- **Enter/Space**: Activate button
- **Escape**: Close modals, go back

---

## World-Space UI

Attach UI to 3D objects (health bars above enemies, nameplates, etc.):

```ini
[node id=15 type=UIRoot name="EnemyHealthBar" parent=1]
ui_document = "res://ui/health-bar.html"
ui_layer = 50
ui_auto_show = true
modal = false
```

The UIRoot transforms with its parent node in 3D space.

**Lua Control**:

```lua
local enemy = ctx.scene:find_by_id(1)
local health_ui = ctx.scene:find_by_id(15)

function on_update(dt)
    -- Sync UI health with enemy health
    ctx.ui:set_property(health_ui.ui_document, "health", tostring(enemy.health / enemy.max_health))

    -- Rotate to face camera
    local camera_pos = ctx.scene:get_camera_position()
    local look_dir = camera_pos - enemy.position
    health_ui.rotation = look_at(look_dir)
end
```

---

## Patterns & Best Practices

### Pattern: Settings Menu with Tabs

```lua
function setup_settings_menu()
    local settings_ui = ctx.ui:load("res://ui/settings.html")

    -- Tab navigation
    ctx.ui:on_event(settings_ui, "settings-tabs", function(event_data)
        if event_data.event_type == "change" then
            local tab = event_data.value
            show_settings_tab(tab)
        end
    })

    -- Individual settings
    ctx.ui:on_event(settings_ui, "brightness-slider", function(event_data)
        if event_data.event_type == "change" then
            ctx.scene:set_brightness(event_data.value)
            save_setting("brightness", event_data.value)
        end
    end)

    return settings_ui
end
```

### Pattern: Achievement Notification

```lua
function show_achievement(title, description, icon)
    local toast_ui = ctx.ui:load("res://ui/achievement-toast.html")

    -- Set content
    ctx.ui:set_properties(toast_ui, {
        title = title,
        description = description,
        icon = icon
    })

    -- Animate in, hold, animate out
    ctx.ui:sequence(toast_ui, {
        {selector="#toast", props={opacity=1, translateY=0}, duration=0.4, easing="ease-out"},
        {wait=3.0},
        {selector="#toast", props={opacity=0, translateY=-100}, duration=0.4, easing="ease-in"}
    })
end
```

### Pattern: Dynamic Inventory with Search

```lua
function setup_inventory(inventory_ui)
    local inventory_items = {}

    -- Populate initial items
    function refresh_inventory()
        ctx.ui:set_list_data(inventory_ui, "items", inventory_items)
    end

    -- Search filtering
    ctx.ui:on_event(inventory_ui, "search-input", function(event_data)
        if event_data.event_type == "change" then
            local query = event_data.text_value:lower()
            local filtered = {}

            for _, item in ipairs(inventory_items) do
                if item.name:lower():find(query, 1, true) then
                    table.insert(filtered, item)
                end
            end

            ctx.ui:set_list_data(inventory_ui, "items", filtered)
        end
    end)

    -- Item selection
    ctx.ui:on_event(inventory_ui, "items", function(event_data)
        if event_data.event_type == "select" then
            local index = event_data.value + 1
            show_item_details(inventory_items[index])
        end
    end)

    refresh_inventory()
end
```

### Pattern: Damage Indicator

```lua
function show_damage(damage_amount, critical)
    local hud = ctx.scene:find_by_tag("hud")[1]
    local hud_doc = hud.ui_document

    -- Flash red for critical hit
    local color = critical and "#ff0000" or "#ffaa00"

    ctx.ui:sequence(hud_doc, {
        {
            selector="#damage-text",
            props={opacity=1, color=color, scaleY=1.2},
            duration=0.1,
            easing="ease-out"
        },
        {wait=0.3},
        {
            selector="#damage-text",
            props={opacity=0, translateY=-50},
            duration=0.4,
            easing="ease-in"
        }
    })

    -- Update damage number
    ctx.ui:set_property(hud_doc, "damage-text", tostring(damage_amount))
end
```

---

## Performance Tips

### Avoid Layout Thrashing

**BAD**: Animate layout properties

```lua
-- Causes layout recalc every frame
ctx.ui:tween(menu, "#panel", {width=500}, {duration=1.0})
```

**GOOD**: Use transform

```lua
-- GPU-accelerated, no layout recalc
ctx.ui:tween(menu, "#panel", {scaleX=2.0}, {duration=1.0})
```

### Virtualize Large Lists

Use `<uz-scroll-view>` and `<uz-list-view>` for large lists:

```lua
-- Efficient: only visible items rendered
ctx.ui:set_list_data(inventory_ui, "items", 10000 items)

-- NOT efficient: renders all items
for i=1, 10000 do
    create_item_element_in_lua()
end
```

### Batch Updates

```lua
-- GOOD: Single set_properties call
ctx.ui:set_properties(hud, {
    health = h,
    ammo = a,
    level = l,
    xp = x
})

-- OKAY: Multiple calls (but less efficient)
ctx.ui:set_property(hud, "health", h)
ctx.ui:set_property(hud, "ammo", a)
ctx.ui:set_property(hud, "level", l)
ctx.ui:set_property(hud, "xp", x)
```

### Limit Concurrent Animations

```lua
-- GOOD: Sequential animations
ctx.ui:sequence(menu, {
    {selector="#a", props={opacity=1}, duration=0.3},
    {selector="#b", props={opacity=1}, duration=0.3},
    {selector="#c", props={opacity=1}, duration=0.3}
})

-- OKAY: Parallel tweens (higher CPU)
ctx.ui:tween(menu, "#a", {opacity=1}, {duration=0.3})
ctx.ui:tween(menu, "#b", {opacity=1}, {duration=0.3})
ctx.ui:tween(menu, "#c", {opacity=1}, {duration=0.3})
```

---

## Debugging UI

### Check Loaded Documents

```lua
-- In console
print("Loaded documents: ", ctx.ui:get_document_count())
```

### Inspect Element State

```lua
-- Get property value
local health_val = ctx.ui:get_property(hud, "health")
print("Health CSS var: " .. health_val)
```

### Debug Animations

```lua
-- Check if element is animating
if ctx.ui:is_animating(hud, "#panel") then
    print("Panel is animating")
end

-- Get animation progress
local progress = ctx.ui:get_animation_progress(hud, anim_id)
print("Progress: " .. progress)
```

### Visual Debug in Editor

In VSCode extension:
- Enable "UI Debug Overlay" to show element bounds
- Hover elements to inspect styles
- Edit CSS live and see changes

---

## Localization Integration

### Markup Translation

```html
<button data-l10n-key="ui.menu.play">Play</button>
<h2 data-l10n-key="ui.level.title">Level 1</h2>
```

### Runtime Language Switching

```lua
-- Load Portuguese strings
ctx.locale:set("pt-BR")

-- All data-l10n-key elements auto-update
```

### Formatting with Variables

Localization file (`res://localization/strings.csv`):

```
key,en,pt-BR
ui.hud.level,"Level {0}","Nível {0}"
ui.hud.health,"{0}/{1} HP","{0}/{1} HP"
```

Lua:

```lua
-- Use format function (if available)
local text = ctx.locale:t("ui.hud.level", {5})  -- "Level 5"
local text = ctx.locale:t("ui.hud.health", {50, 100})  -- "50/100 HP"
```

---

## Troubleshooting

### UI Not Appearing

Check:
1. Document loaded: `ctx.ui:load()` returned non-nil
2. Shown: `ctx.ui:show()` called
3. CSS display: `display: flex` or `display: block` (not `none`)
4. Z-order: `ui_layer` property correct

### Animations Janky

- Use GPU properties: `opacity`, `transform` only
- Avoid animating `width`, `height`, `padding`, `margin`
- Reduce number of simultaneous tweens
- Check for layout recalc in CSS transitions

### Events Not Firing

Check:
1. Element has `id` attribute
2. `ctx.ui:on_event()` callback registered
3. Modal doesn't block input: `ctx.ui:set_modal(doc, false)`
4. Element is focusable: `data-focusable="true"`

### Text Not Rendering

Check:
1. Font registered: `ctx.ui:register_font()`
2. Correct font family in CSS
3. Text color not same as background
4. Font file exists at resource path

---

## Example: Complete Pause Menu

```html
<!-- res://ui/pause-menu.html -->
<!DOCTYPE html>
<html>
<head>
    <style>
        body {
            position: fixed;
            top: 0;
            left: 0;
            width: 100%;
            height: 100%;
            background-color: rgba(0, 0, 0, 0.7);
            display: flex;
            align-items: center;
            justify-content: center;
            font-family: "UzDefault";
        }

        .menu {
            background-color: #2a2a2a;
            border: 2px solid #555555;
            border-radius: 8px;
            padding: 32px;
            width: 400px;
            box-shadow: 0 8px 32px rgba(0, 0, 0, 0.8);
        }

        h1 {
            text-align: center;
            margin: 0 0 32px 0;
            color: #ffffff;
        }

        .button-group {
            display: flex;
            flex-direction: column;
            gap: 12px;
        }

        uz-button {
            width: 100%;
            padding: 12px;
            font-size: 16px;
        }
    </style>
</head>
<body>
    <div class="menu">
        <h1>Paused</h1>
        <div class="button-group">
            <uz-button id="btn-resume" class="primary" data-focusable="true" data-focus-down="btn-settings">
                Resume
            </uz-button>
            <uz-button id="btn-settings" data-focusable="true" data-focus-up="btn-resume" data-focus-down="btn-exit">
                Settings
            </uz-button>
            <uz-button id="btn-exit" class="danger" data-focusable="true" data-focus-up="btn-settings">
                Exit to Menu
            </uz-button>
        </div>
    </div>
</body>
</html>
```

```lua
-- scripts/pause_menu.lua

local pause_menu = nil

function show_pause_menu()
    if pause_menu then return end

    -- Load and show
    pause_menu = ctx.ui:load("res://ui/pause-menu.html")
    ctx.ui:show(pause_menu)

    -- Pause game
    ctx.scene:set_paused(true)

    -- Animate in
    ctx.ui:tween(pause_menu, ".menu", {
        opacity = 1.0,
        scaleY = 1.0
    }, {
        duration = 0.3,
        easing = "ease-out"
    })

    -- Setup button handlers
    ctx.ui:on_event(pause_menu, "btn-resume", function(event_data)
        if event_data.event_type == "click" then
            hide_pause_menu()
        end
    end)

    ctx.ui:on_event(pause_menu, "btn-settings", function(event_data)
        if event_data.event_type == "click" then
            show_settings_menu()
        end
    end)

    ctx.ui:on_event(pause_menu, "btn-exit", function(event_data)
        if event_data.event_type == "click" then
            ctx.scene:load("res://scenes/main_menu.uzscene")
        end
    end)
end

function hide_pause_menu()
    if not pause_menu then return end

    -- Animate out
    ctx.ui:tween(pause_menu, ".menu", {
        opacity = 0.0,
        scaleY = 0.8
    }, {
        duration = 0.2,
        easing = "ease-in",
        on_complete = function()
            ctx.ui:hide(pause_menu)
            ctx.ui:unload(pause_menu)
            pause_menu = nil

            -- Resume game
            ctx.scene:set_paused(false)
        end
    })
end
```

---

## Related Documentation

- [../specs/ui-system.md](../specs/ui-system.md) — UI system architecture
- [../specs/ui-widget-library.md](../specs/ui-widget-library.md) — Widget reference
- [../specs/ui-animation.md](../specs/ui-animation.md) — Animation API
- [../engine/ui-architecture.md](../engine/ui-architecture.md) — Implementation details
- [../guides/lua-scripting-guide.md](../guides/lua-scripting-guide.md) — Lua scripting basics

