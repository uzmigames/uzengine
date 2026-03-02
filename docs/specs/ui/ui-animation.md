# UI Animation System

> **Status**: Planned
> **Phase**: 2
> **Priority**: High — essential for polished menus and HUD transitions

---

## Overview

The **UI Animation System** provides a comprehensive toolkit for animating UI elements:

- **Tweens**: Smooth property changes over time (opacity, position, scale, color)
- **Easing curves**: 10+ timing functions (linear, ease-in, ease-out, spring, bounce)
- **Sequences**: Chain multiple tweens in order with delays
- **Page transitions**: Coordinated in/out animations for menu navigation
- **CSS integration**: Respect CSS `transition` declarations for automatic animation
- **Event-driven**: Callbacks on animation complete, cancel, repeat

All animation is **declarative** and **GPU-accelerated** (using `transform` and `opacity` properties which don't trigger layout recalculation).

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│ Game Code (Lua)                                 │
│                                                 │
│  ctx.ui:tween(doc, selector, props, options)   │
│  ctx.ui:sequence(doc, array_of_steps)          │
│  ctx.ui:animate_class(doc, selector, class)    │
└────────────────┬────────────────────────────────┘
                 │ Lua → C ABI
                 ▼
┌─────────────────────────────────────────────────┐
│ uz_ui_html.dll (AnimationManager)               │
│                                                 │
│  - Track active tweens and sequences            │
│  - Compute interpolated values each frame       │
│  - Dispatch completion callbacks                │
│  - Evaluate easing curves                       │
└────────────────┬────────────────────────────────┘
                 │ ui_tick(delta_t)
                 ▼
┌─────────────────────────────────────────────────┐
│ Property Application                            │
│                                                 │
│  - Set CSS properties on elements               │
│  - Trigger layout recalc (only for non-GPU)    │
│  - Render in next frame                         │
└─────────────────────────────────────────────────┘
```

---

## Basic Tween API

### Simple Fade In

```lua
-- Fade element from opacity 0 to 1 over 0.3 seconds
ctx.ui:tween(hud, "#main-menu", {opacity = 1.0}, {
    duration = 0.3,
    easing = "ease-out",
    delay = 0,
    on_complete = function()
        print("Fade-in complete!")
    end
})
```

### Signature

```lua
ctx.ui:tween(doc, selector, target_props, options)
```

**Parameters**:

| Name | Type | Description |
|------|------|-------------|
| `doc` | UiDocument* | Document handle from `ctx.ui:load()` |
| `selector` | string | CSS selector (`.class`, `#id`, `element-name`) |
| `target_props` | table | Property names and target values |
| `options` | table | Animation configuration (optional) |

**target_props example**:

```lua
{
    opacity = 0.5,
    transform = "translateX(100px) scale(0.8)",
    color = "#ff0000",
    backgroundColor = "rgba(0, 255, 0, 0.5)"
}
```

**options fields**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `duration` | number | 1.0 | Animation time in seconds |
| `easing` | string | "linear" | Timing function name |
| `delay` | number | 0 | Wait time before starting (seconds) |
| `repeat_count` | number | 1 | Number of repeats (0 = infinite) |
| `reverse_on_repeat` | bool | false | Reverse animation on each repeat |
| `on_complete` | function | nil | Callback when animation finishes |
| `on_cancel` | function | nil | Callback if animation cancelled |

### Return Value

```lua
local tween_id = ctx.ui:tween(...)
-- Can later cancel: ctx.ui:cancel_animation(hud, tween_id)
```

---

## Easing Functions

### Available Easing Curves

```lua
-- Linear: constant speed
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "linear"})

-- Quadratic
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-in"})      -- slow start
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-out"})     -- slow end
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-in-out"})  -- slow both

-- Cubic (smoother than quad)
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-in-cubic"})
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-out-cubic"})

-- Quart (even smoother)
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-in-quart"})
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-out-quart"})

-- Quintic (maximum smoothness)
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-in-quint"})
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "ease-out-quint"})

-- Spring oscillation
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "spring", spring_stiffness = 100})

-- Bounce
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "bounce-in"})
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "bounce-out"})

-- Elastic (overshoot then settle)
ctx.ui:tween(doc, "#box", {x = 100}, {easing = "elastic-out"})
```

### Easing Curve Graphs

```
Linear:         ease-in:        ease-out:       ease-in-out:
1 |----         1 |---          1 ---           1  .--.
  |  /            |  /             /  \           | /    \
0 |_/           0 |_/           0 _/  ---       0 |/      \
  0      1         0      1       0      1         0      1

Spring:         Bounce-out:     Elastic-out:
1 |~~.           1 |---.         1  .-~.
  | / \          0 |_.-_.-       0 _/   ~.
0 |_/  \         (repeats)        (oscillates)
```

### Spring Physics

For `easing = "spring"`, customize:

```lua
ctx.ui:tween(doc, "#btn", {scale = 1.2}, {
    duration = 0.8,
    easing = "spring",
    spring_stiffness = 200,   -- 0–500, higher = stiffer
    spring_damping = 10       -- 0–100, higher = less bouncy
})
```

---

## Animatable Properties

### Transform (GPU-accelerated)

```lua
ctx.ui:tween(doc, "#panel", {
    transform = "translateX(100px) translateY(50px) scale(1.1) rotate(5deg)"
}, {duration = 0.3, easing = "ease-out"})

-- Individual transform functions
ctx.ui:tween(doc, "#panel", {translateX = 100, translateY = 50}, {duration = 0.3})
ctx.ui:tween(doc, "#panel", {scaleX = 1.1, scaleY = 1.1}, {duration = 0.3})
ctx.ui:tween(doc, "#panel", {rotate = 5}, {duration = 0.3})
```

**Units**: `px`, `%`, `deg`, `rad`

### Opacity (GPU-accelerated)

```lua
ctx.ui:tween(doc, "#panel", {opacity = 0.5}, {duration = 0.3})
-- Value range: 0.0 (transparent) to 1.0 (opaque)
```

### Colors

```lua
ctx.ui:tween(doc, "#text", {
    color = "#00ff00",
    backgroundColor = "rgba(255, 0, 0, 0.8)"
}, {duration = 0.5})

-- Automatic RGB interpolation
ctx.ui:tween(doc, "#btn", {backgroundColor = "#0066ff"}, {duration = 0.3})
```

### Layout Properties (Triggers layout recalc)

```lua
-- Avoid animating layout properties; use transform instead
ctx.ui:tween(doc, "#box", {width = 200}, {duration = 0.3})  -- NOT recommended (causes jank)

-- Recommended: use transform
ctx.ui:tween(doc, "#box", {scaleX = 2.0}, {duration = 0.3})  -- Better (GPU-accelerated)
```

### Text Properties

```lua
ctx.ui:tween(doc, "#score", {
    fontSize = 32,
    letterSpacing = 2
}, {duration = 0.5})
```

### All Animatable Properties

| Property | GPU? | Notes |
|----------|------|-------|
| `opacity` | ✅ | 0.0–1.0 |
| `transform` | ✅ | translate, scale, rotate, skew |
| `translateX`, `translateY`, `translateZ` | ✅ | Individual transform axes |
| `scaleX`, `scaleY`, `scaleZ` | ✅ | Individual scale axes |
| `rotate` | ✅ | Degrees or radians |
| `color` | ❌ | Text color |
| `backgroundColor` | ❌ | Background color |
| `borderColor` | ❌ | Border color |
| `fontSize` | ❌ | Font size (triggers layout) |
| `width`, `height` | ❌ | Avoid (layout thrashing) |
| `left`, `top`, `right`, `bottom` | ❌ | Use transform instead |
| `padding`, `margin` | ❌ | Use transform instead |

---

## Tween Examples

### Health Bar Fill Animation

```lua
-- Smooth health bar decrease
ctx.ui:tween(hud, ".health-bar .fill", {
    width = tostring(new_health_percent * 100) .. "%"
}, {
    duration = 0.5,
    easing = "ease-out",
    on_complete = function()
        if new_health_percent <= 0 then
            ctx.ui:show_toast(hud, "death-toast", {
                type = "error",
                message = "You died!"
            })
        end
    end
})
```

### Damage Flash

```lua
local damage_amount = 25
ctx.ui:tween(hud, "#damage-indicator", {
    opacity = 1.0,
    color = "#ff0000"
}, {
    duration = 0.1,
    easing = "ease-out",
    on_complete = function()
        ctx.ui:tween(hud, "#damage-indicator", {
            opacity = 0.0
        }, {duration = 0.4, easing = "ease-in"})
    end
})
```

### Button Bounce on Click

```lua
ctx.ui:on_event(hud, "btn-play", function(event_data)
    if event_data.event_type == "click" then
        ctx.ui:tween(hud, "#btn-play", {
            transform = "scale(0.95)"
        }, {
            duration = 0.1,
            easing = "ease-in",
            on_complete = function()
                ctx.ui:tween(hud, "#btn-play", {
                    transform = "scale(1.0)"
                }, {duration = 0.2, easing = "ease-out"})
            end
        })
        start_game()
    end
end)
```

### Scale-up with Spin

```lua
ctx.ui:tween(doc, "#logo", {
    opacity = 1.0,
    scaleX = 1.0,
    scaleY = 1.0,
    rotate = 360
}, {
    duration = 1.0,
    easing = "ease-out-cubic",
    delay = 0.5
})
```

---

## Sequences

Chain multiple tweens in sequence with delays between them.

### Signature

```lua
ctx.ui:sequence(doc, steps)
```

**steps**: Array of tween or wait operations:

```lua
{
    {selector="#logo", props={opacity=1}, duration=0.5, easing="ease-out"},
    {wait=0.2},
    {selector="#menu", props={opacity=1, translateY=0}, duration=0.3, easing="ease-out"},
    {selector="#buttons", props={opacity=1}, duration=0.4, easing="ease-out", stagger=0.1}
}
```

### Step Types

**Tween step**:

```lua
{
    selector = "#element",    -- required
    props = {opacity = 1},    -- required
    duration = 0.3,           -- optional, default 0.3
    easing = "ease-out",      -- optional, default "linear"
    delay = 0,                -- optional, only for first step
    stagger = 0.05            -- optional, stagger multiple elements
}
```

If `selector` matches multiple elements and `stagger` is set, each element's animation starts `stagger` seconds after the previous one.

**Wait step**:

```lua
{
    wait = 0.5                -- Wait 0.5 seconds before next tween
}
```

### Return Value

```lua
local sequence_id = ctx.ui:sequence(doc, steps)
-- Cancel: ctx.ui:cancel_animation(hud, sequence_id)
```

---

## Sequence Examples

### Menu Intro Animation

```lua
ctx.ui:sequence(hud, {
    -- Title fades in
    {selector="#title", props={opacity=1}, duration=0.5, easing="ease-out"},

    -- Brief pause
    {wait=0.3},

    -- Menu items slide in staggered
    {
        selector=".menu-item",
        props={opacity=1, translateX=0},
        duration=0.4,
        easing="ease-out",
        stagger=0.1
    },

    -- Buttons fade in
    {selector=".button", props={opacity=1}, duration=0.3, easing="ease-out"}
})
```

### Combo Counter Animation

```lua
-- Sequence for displaying combo hits
for hit = 1, combo_count do
    table.insert(sequence_steps, {
        selector = "#combo-text",
        props = {opacity=1, scale=1.2},
        duration = 0.2,
        easing = "spring"
    })

    table.insert(sequence_steps, {wait = 0.1})
end

ctx.ui:sequence(hud, sequence_steps)
```

### Battle Transition

```lua
ctx.ui:sequence(battle_ui, {
    -- Fade out current UI
    {selector="*", props={opacity=0}, duration=0.4, easing="ease-in"},

    -- Wait for battle setup
    {wait=0.5},

    -- Fade in battle UI
    {selector="*", props={opacity=1}, duration=0.3, easing="ease-out"},

    -- Animate health bars
    {
        selector=".health-bar",
        props={width="100%"},
        duration=0.6,
        easing="ease-out",
        stagger=0.1
    }
})
```

---

## CSS Transition Integration

The animation system respects CSS `transition` declarations automatically.

### CSS-driven Animation

```html
<style>
  .panel {
    opacity: 0;
    transition: opacity 0.3s ease-out, transform 0.3s ease-out;
  }

  .panel.visible {
    opacity: 1;
    transform: translateY(0);
  }
</style>

<div class="panel" id="settings-panel"></div>
```

### Lua Code

```lua
-- Add CSS class, transition happens automatically
ctx.ui:animate_class(hud, "#settings-panel", "visible", {
    on_complete = function()
        print("Panel fully visible")
    end
})
```

### Hybrid: CSS + Lua Tween

```lua
-- CSS defines transition, Lua triggers it
ctx.ui:set_property(hud, "show-menu", "true")

-- Simultaneously, animate other properties with tween
ctx.ui:tween(hud, ".menu-icon", {rotate=90}, {duration=0.3})
```

---

## Animate Class

Add/remove CSS class and wait for CSS transition to complete.

### Signature

```lua
ctx.ui:animate_class(doc, selector, class_name, options)
```

**Parameters**:

| Name | Type | Description |
|------|------|-------------|
| `doc` | UiDocument* | Document handle |
| `selector` | string | CSS selector |
| `class_name` | string | CSS class to toggle |
| `options` | table | `on_complete`, `remove` (bool) |

### Example

```lua
-- Add class and wait for transition
ctx.ui:animate_class(hud, ".menu", "visible", {
    on_complete = function()
        print("Menu fully visible")
    end
})

-- Later, remove class
ctx.ui:animate_class(hud, ".menu", "visible", {
    remove = true,
    on_complete = function()
        ctx.ui:hide(hud)
    end
})
```

---

## Page Transitions

Coordinate page in/out transitions when navigating between menu screens.

### Navigation Stack

```lua
-- Create/get navigation stack
local nav = ctx.ui:navigation_stack(hud)

-- Navigate with automatic transitions
nav:push("main-menu")    -- fade in main menu
nav:push("settings")     -- fade out main menu, fade in settings
nav:pop()                -- fade out settings, fade in main menu
```

### Page Transition Types

```lua
-- Fade transition (default)
nav:push("new-page", {transition="fade", duration=0.3})

-- Slide transition
nav:push("new-page", {transition="slide", direction="left", duration=0.4})

-- Scale transition
nav:push("new-page", {transition="scale", duration=0.3})

-- No transition
nav:push("new-page", {transition="none"})
```

### Transition Options

| Option | Type | Values | Description |
|--------|------|--------|-------------|
| `transition` | string | `"fade"`, `"slide"`, `"scale"`, `"none"` | Animation type |
| `direction` | string | `"left"`, `"right"`, `"up"`, `"down"` | For slide transitions |
| `duration` | number | 0.1–2.0 | Animation time |
| `easing` | string | Any easing function | Timing curve |

### Example — Main Menu Flow

```lua
local nav = ctx.ui:navigation_stack(pause_menu)

ctx.ui:on_event(pause_menu, "btn-settings", function(event_data)
    if event_data.event_type == "click" then
        nav:push("settings-menu", {transition="slide", direction="right", duration=0.3})
    end
end)

ctx.ui:on_event(pause_menu, "btn-audio", function(event_data)
    if event_data.event_type == "click" then
        nav:push("audio-settings", {transition="slide", direction="right", duration=0.3})
    end
end)

ctx.ui:on_event(pause_menu, "btn-back", function(event_data)
    if event_data.event_type == "click" then
        nav:pop({transition="slide", direction="left", duration=0.3})
    end
end)
```

### Per-Page Setup

```lua
-- Define pages and their animations
local menu_pages = {
    ["main-menu"] = {
        template = "res://ui/main-menu.html",
        transition_in = "fade",
        transition_out = "fade"
    },
    ["settings"] = {
        template = "res://ui/settings.html",
        transition_in = "slide",
        transition_direction = "right"
    }
}
```

---

## Animation State Management

### Get Animation State

```lua
-- Check if animation is running
if ctx.ui:is_animating(hud, "#logo") then
    print("Logo is currently animating")
end

-- Get animation progress (0.0–1.0)
local progress = ctx.ui:get_animation_progress(hud, anim_id)
```

### Cancel Animation

```lua
local tween_id = ctx.ui:tween(hud, "#panel", {opacity=0}, {duration=1.0})

-- Later, cancel the tween
ctx.ui:cancel_animation(hud, tween_id)
```

### Pause/Resume

```lua
ctx.ui:pause_animation(hud, anim_id)
ctx.ui:resume_animation(hud, anim_id)
```

---

## Performance Considerations

### GPU-Accelerated Properties

Always prefer these (no layout recalculation):

```lua
-- GOOD: Uses GPU transform
ctx.ui:tween(hud, "#panel", {transform="translateX(100px)"}, {duration=0.3})

-- GOOD: Uses GPU opacity
ctx.ui:tween(hud, "#panel", {opacity=0.5}, {duration=0.3})

-- BAD: Triggers layout recalc
ctx.ui:tween(hud, "#panel", {width=200, height=100}, {duration=0.3})

-- BAD: Triggers layout recalc
ctx.ui:tween(hud, "#panel", {left=100, top=50}, {duration=0.3})
```

### Stagger Performance

When staggering many elements, limit concurrency:

```lua
-- Animate 100 elements staggered = manageable
ctx.ui:sequence(hud, {
    {
        selector=".item",
        props={opacity=1},
        duration=0.3,
        stagger=0.02  -- 100 * 0.02 = 2 seconds total
    }
})

-- Animate 1000 elements staggered = still efficient
-- (only visible elements are animated)
```

### Batch Updates

Combine multiple tweens into a sequence for better performance:

```lua
-- GOOD: Single sequence object
ctx.ui:sequence(hud, {
    {selector="#a", props={opacity=1}, duration=0.3},
    {selector="#b", props={opacity=1}, duration=0.3},
    {selector="#c", props={opacity=1}, duration=0.3}
})

-- OKAY: Multiple tween calls
ctx.ui:tween(hud, "#a", {opacity=1}, {duration=0.3})
ctx.ui:tween(hud, "#b", {opacity=1}, {duration=0.3})
ctx.ui:tween(hud, "#c", {opacity=1}, {duration=0.3})
```

---

## Advanced Examples

### Achievement Unlock Animation

```lua
function show_achievement(achievement_name, icon_path)
    -- Load achievement toast
    ctx.ui:load("res://ui/achievement.html")

    -- Sequence: slide in, hold, slide out
    ctx.ui:sequence(achievement_ui, {
        {
            selector="#achievement",
            props={opacity=1, translateX=0},
            duration=0.4,
            easing="ease-out"
        },
        {wait=3.0},  -- Hold for 3 seconds
        {
            selector="#achievement",
            props={opacity=0, translateX=-300},
            duration=0.4,
            easing="ease-in"
        }
    })
end
```

### Animated Loading Bar

```lua
function animate_progress_bar(target_percent)
    ctx.ui:tween(hud, ".progress-fill", {
        width = tostring(target_percent) .. "%"
    }, {
        duration = 2.0,
        easing = "ease-in-out",
        on_complete = function()
            print("Loading complete!")
        end
    })
end

-- Usage: increment from 0% to 100%
animate_progress_bar(25)   -- 0–25%
animate_progress_bar(50)   -- 25–50%
animate_progress_bar(75)   -- 50–75%
animate_progress_bar(100)  -- 75–100%
```

### Game Over Screen Reveal

```lua
function show_game_over_screen(reason)
    ctx.ui:sequence(gameover_ui, {
        -- Fade to black
        {selector="#black-overlay", props={opacity=1}, duration=0.5, easing="ease-in"},
        {wait=0.5},

        -- Slide in title
        {
            selector="#game-over-title",
            props={opacity=1, translateY=0},
            duration=0.6,
            easing="ease-out"
        },

        -- Stagger in stats
        {
            selector=".stat-row",
            props={opacity=1, translateX=0},
            duration=0.4,
            easing="ease-out",
            stagger=0.1
        },

        -- Buttons slide up
        {
            selector=".button",
            props={opacity=1, translateY=0},
            duration=0.4,
            easing="ease-out",
            stagger=0.08
        }
    })
end
```

---

## Timing Precision

### Frame-rate Independent

All animations use **delta time** for frame-rate independence:

```
progress = (elapsed_time - delay) / duration

If duration = 1.0 and elapsed = 0.5:
  progress = 0.5

If frame skips (60 FPS → 30 FPS):
  delta_t doubles, elapsed_time still correct
  progress stays accurate
```

### Animation Update Frequency

Animations update during `ui_tick()` which is called:

```
Main Loop:
  Game Update (60 FPS)
  → Physics Simulation
  → Audio Updates
  → UI Tick (includes animation updates)
  → Render
```

---

## C ABI Extension

```c
// New C ABI functions for animation

typedef uint32_t AnimationId;

// Start a tween animation
AnimationId ui_tween(UiDocument* doc, const char* selector,
                     const UiTweenProperty* props, int prop_count,
                     const UiTweenOptions* options);

// Start a sequence
AnimationId ui_sequence(UiDocument* doc,
                        const UiSequenceStep* steps, int step_count);

// Cancel animation
void ui_cancel_animation(UiDocument* doc, AnimationId anim_id);

// Get state
bool ui_is_animating(UiDocument* doc, const char* selector);
float ui_get_animation_progress(UiDocument* doc, AnimationId anim_id);
```

---

## Related Documentation

- [ui-system.md](ui-system.md) — Core UI system
- [ui-widget-library.md](ui-widget-library.md) — Widget library reference
- [../guides/ui-guide.md](../guides/ui-guide.md) — Practical UI guide
- [../engine/ui-architecture.md](../engine/ui-architecture.md) — Implementation details

