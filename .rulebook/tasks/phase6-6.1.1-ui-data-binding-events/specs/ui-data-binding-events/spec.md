# UI Data Binding & Events — Specification

## ADDED Requirements

### Requirement: CSS Custom Property Data Binding
The UI system SHALL support one-way data binding from game code to CSS via custom properties.

#### Scenario: Health Bar Binding
Given CSS: `.fill { width: calc(var(--health) * 1%); background: green; }`
When game code calls ui_set_property("--health", "75")
Then the .fill element width SHALL update to 75%
And re-layout SHALL occur only for affected elements (dirty flag)

#### Scenario: Color Binding
Given CSS: `.warning { color: var(--alert-color, white); }`
When ui_set_property("--alert-color", "red") is called
Then the text color SHALL change to red
When the property is unset
Then the fallback "white" SHALL be used

### Requirement: UI Events
The UI system SHALL dispatch events to game code when users interact with UI elements.

#### Scenario: Button Click
Given an HTML button with id="start-btn"
When the user clicks the button
Then a UiEvent SHALL be dispatched: { element_id: "start-btn", event_type: "click" }

#### Scenario: Slider Change
Given an `<input type="range" id="volume" min="0" max="100">`
When the user drags the slider to 60
Then a UiEvent SHALL be dispatched: { element_id: "volume", event_type: "change", value: "60" }

### Requirement: Input Routing
The UI system SHALL manage input routing between game and UI layers.

#### Scenario: Modal Dialog
Given a modal UI layer (is_modal=true) is active
When the user presses movement keys
Then input SHALL be consumed by the UI layer
And the game input system SHALL NOT receive those keys

#### Scenario: Passthrough HUD
Given a non-modal UI layer (is_modal=false)
When the user presses movement keys
Then input SHALL pass through to the game input system
And only mouse clicks on UI elements SHALL be consumed by UI

### Requirement: Lua UI API
The scripting system SHALL expose full UI control to Lua.

#### Scenario: Lua Load and Bind
Given a Lua script:
```lua
uz.ui.load("hud.html", "hud.css")
uz.ui.set_property("--health", tostring(player.health))
uz.ui.on_event("start-btn", "click", function() start_game() end)
```
Then the UI SHALL load, bind health data, and respond to button clicks
