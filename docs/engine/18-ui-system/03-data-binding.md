# UI System — Reactive Data Binding

> **Status**: Planned (Phase 3–4)
> **Source**: `engine/ui/reactive_binding.h/.cpp`, `engine/ui/ui_subsystem.h`
> **Depends on**: HTML/CSS runtime (§18.2), scripting system (§15)

## Purpose

Data binding automatically syncs C++ or Lua state with HTML/CSS. When a value changes, the UI updates **reactively** without manual imperative code. This is inspired by Vue.js and other modern frameworks.

```cpp
// C++: expose game state to UI
player_health = 50;
ui->Bind("player.health", &player_health);

// Now whenever player_health changes, the UI is updated automatically
// (via CSS custom property --player-health: 50)
```

## Binding System

### Architecture

```
Game State (C++ variable)
    │
    └──────────────────────────┐
                               ▼
                    Binding Proxy
                     (observes change)
                               │
                               ▼
                    CSS Custom Property
                    (--player-health: 50)
                               │
                               ▼
                    HTML Element
                    <div class="health-bar" style="width: var(--player-health)">
                               │
                               ▼
                            Layout Re-render (if property affects layout)
                               │
                               ▼
                            Display to screen
```

### Binding Types

**Value Bindings** — scalar values (int, float, string, bool):

```cpp
// C++
int player_health = 100;
float player_mana = 50.0f;
std::string player_name = "Hero";

ui->Bind("player.health", &player_health);
ui->Bind("player.mana", &player_mana);
ui->Bind("player.name", &player_name);

// CSS Custom Properties (auto-updated)
:root {
    --player-health: 100;
    --player-mana: 50;
    --player-name: "Hero";
}

// CSS Usage
.health-bar { width: calc(var(--player-health) * 1%); }
.mana-bar { width: calc(var(--player-mana) * 1%); }
h1::before { content: var(--player-name); }
```

**List Bindings** — arrays/vectors (for dynamic lists):

```cpp
// C++
std::vector<InventoryItem> inventory;
ui->Bind("inventory", &inventory);

// HTML (template repeats for each item)
<ul id="inventory">
    <li data-repeat="item in inventory">
        <img src="res://icons/{{ item.icon }}">
        <span>{{ item.name }} x{{ item.count }}</span>
    </li>
</ul>

// When inventory changes, list items are added/removed/updated automatically
```

**Event Bindings** — UI events trigger game code:

```cpp
ui->On("buy_item", [](const UIEvent& e) {
    int item_id = e.Get<int>("item_id");
    float cost = e.Get<float>("cost");
    game->BuyItem(item_id, cost);
});

// In HTML:
<button data-on-click="buy_item" data-item-id="42" data-cost="100.0">
    Buy Potion
</button>
```

## CSS Custom Properties

### Property Formatting

Values are converted to CSS-compatible strings:

```cpp
int health = 50;
ui->Bind("health", &health);
// CSS: --health: 50

float opacity = 0.75f;
ui->Bind("opacity", &opacity);
// CSS: --opacity: 0.75

bool is_damaged = true;
ui->Bind("is-damaged", &is_damaged);
// CSS: --is-damaged: true

Color team_color = {1.0f, 0.0f, 0.0f, 1.0f};  // RGBA
ui->Bind("team-color", &team_color);
// CSS: --team-color: rgba(255, 0, 0, 255)

Vec3 position = {100, 200, 50};
ui->Bind("position", &position);
// CSS: --position: "100,200,50"  (serialized as string)
```

### CSS Usage Examples

```css
/* Numeric CSS units */
.health-bar {
    width: calc(var(--health) * 1%);       /* 50% width */
    opacity: var(--damage-intensity);       /* 0.0-1.0 */
    z-index: var(--priority);               /* stacking order */
}

/* Colors */
.indicator {
    background-color: var(--team-color);   /* rgba() */
}

/* Conditional display (via data attributes) */
.warning:not([data-warning="false"]) {
    display: block;
}

.warning[data-warning="true"] {
    border: 2px solid red;
}

/* Content from variables */
.score::after {
    content: var(--score);
}
```

## Two-Way Binding (Form Inputs)

### Input Elements

HTML inputs can also update game state:

```html
<input type="text" id="player-name" data-bind="player.name" placeholder="Enter name">
<input type="range" id="volume-slider" data-bind="settings.volume" min="0" max="100">
<input type="checkbox" id="fullscreen" data-bind="settings.fullscreen">
```

### Binding Direction

```cpp
// One-way: C++ → UI (value changes update UI)
ui->Bind("health", &player_health, BindDirection::OneWayToUI);

// Two-way: C++ ↔ UI (changes sync both directions)
ui->Bind("player_name", &player_name, BindDirection::TwoWay);

// Input-only: UI → C++ (input field changes update C++)
ui->Bind("search_query", &search_query, BindDirection::OneWayToGame);
```

### Implementation

```cpp
class ReactiveBinding {
private:
    void* value_ptr;                // pointer to game state
    std::string property_name;      // CSS custom property name
    BindDirection direction;
    std::string last_ui_value;      // for change detection

public:
    void SyncFromGame() {
        // Game state → CSS property
        std::string new_value = FormatValue(value_ptr);
        if (new_value != last_css_value) {
            document->SetCustomProperty(property_name, new_value);
            last_css_value = new_value;
        }
    }

    void SyncFromUI() {
        // UI input → game state
        std::string ui_value = GetInputValue(property_name);
        if (ui_value != last_ui_value) {
            ParseAndSet(value_ptr, ui_value);
            last_ui_value = ui_value;
        }
    }
};

void UISubsystem::Update(float delta_time) {
    // Every frame, sync bindings
    for (auto& binding : bindings) {
        if (binding.direction == BindDirection::OneWayToUI ||
            binding.direction == BindDirection::TwoWay) {
            binding.SyncFromGame();
        }
        if (binding.direction == BindDirection::OneWayToGame ||
            binding.direction == BindDirection::TwoWay) {
            binding.SyncFromUI();
        }
    }
}
```

## List Binding (Dynamic Repeats)

### Data-Repeat Directive

HTML template:

```html
<div class="inventory">
    <div data-repeat="item in inventory" class="slot">
        <img src="{{ item.texture_path }}" class="icon">
        <span class="name">{{ item.name }}</span>
        <span class="count">x{{ item.count }}</span>
        <button data-on-click="use_item" data-item-id="{{ item.id }}">Use</button>
    </div>
</div>
```

### Binding Code

```cpp
struct InventoryItem {
    int id;
    std::string name;
    std::string texture_path;
    int count;
};

std::vector<InventoryItem> inventory = {
    {1, "Health Potion", "res://icons/potion.png", 5},
    {2, "Mana Potion", "res://icons/mana.png", 3},
};

ui->Bind("inventory", &inventory);
```

### Template Engine

The binding engine processes the template:

```cpp
void ProcessRepeatDirective(HtmlElement* template_elem,
                           const std::vector<InventoryItem>& items) {
    HtmlElement* container = template_elem->parentNode;

    // Clear existing repeated elements
    container->innerHTML = "";

    // For each item, create DOM element from template
    for (size_t i = 0; i < items.size(); ++i) {
        HtmlElement* clone = template_elem->cloneNode(true);

        // Substitute {{ variable }} with item data
        SubstituteVariables(clone, items[i]);

        container->appendChild(clone);
    }
}

void SubstituteVariables(HtmlElement* elem, const InventoryItem& item) {
    // Recursively replace {{ item.name }} with item.name, etc.
    for (auto& node : elem->getAllNodes()) {
        std::string text = node->text;

        // {{ item.name }} → "Health Potion"
        text = std::regex_replace(text, std::regex(R"(\{\{item\.name\}\})"), item.name);

        // {{ item.count }} → "5"
        text = std::regex_replace(text, std::regex(R"(\{\{item\.count\}\})"),
                                  std::to_string(item.count));

        // {{ item.texture_path }} → "res://icons/potion.png"
        node->text = text;
    }
}
```

## Event Binding

### UI Event Handlers

```html
<!-- Click handler -->
<button data-on-click="equip_item" data-item-id="42">Equip</button>

<!-- Input handler -->
<input type="text" id="search" data-on-input="search_inventory">

<!-- Hover handler -->
<div data-on-mouseenter="show_tooltip" data-tooltip-text="Heal all party members">
    Holy Water
</div>

<!-- Change handler (for selects, checkboxes) -->
<select data-on-change="set_difficulty" id="difficulty-select">
    <option value="easy">Easy</option>
    <option value="normal" selected>Normal</option>
    <option value="hard">Hard</option>
</select>
```

### Event Listener Registration

```cpp
ui->On("equip_item", [game](const UIEvent& e) {
    int item_id = e.Get<int>("item_id");
    game->EquipItem(item_id);
});

ui->On("search_inventory", [ui](const UIEvent& e) {
    std::string query = e.Get<std::string>("value");
    ui->FilterInventoryUI(query);
});

ui->On("set_difficulty", [game](const UIEvent& e) {
    std::string difficulty = e.Get<std::string>("value");
    game->SetDifficulty(difficulty);
});
```

### Event Data Structure

```cpp
struct UIEvent {
    std::string event_type;              // "click", "input", "change"
    HtmlElement* target;                 // element that triggered event
    std::map<std::string, std::any> data; // event payload (item_id, value, etc.)

    template<typename T>
    T Get(StringView key) const {
        if (data.count(key)) {
            return std::any_cast<T>(data[key]);
        }
        throw std::runtime_error(fmt::format("Event key not found: {}", key));
    }

    template<typename T>
    T GetOr(StringView key, const T& default_value) const {
        if (data.count(key)) {
            try {
                return std::any_cast<T>(data[key]);
            } catch (const std::bad_any_cast&) {}
        }
        return default_value;
    }
};
```

## Lua Integration

### Lua Binding API

```lua
-- Expose Lua table to UI
local player = {
    health = 100,
    mana = 50,
    name = "Hero",
    level = 1,
}

ctx.ui:bind("player", player)

-- Update Lua state (automatically syncs to UI)
player.health = 75
player.name = "Champion"

-- Listen for UI events
ctx.ui:on("level_up", function(event)
    player.level = player.level + 1
    print("Leveled up to " .. player.level)
end)

-- Two-way binding for settings
local settings = {
    volume = 0.8,
    fullscreen = true,
}
ctx.ui:bind("settings", settings, "two-way")

-- When player moves volume slider, settings.volume updates
-- When code changes settings.volume, UI slider moves
```

### Change Callbacks

```lua
-- Watch for property changes
ctx.ui:watch("player.health", function(old_value, new_value)
    if new_value < 20 then
        print("Health critical!")
    end
end)

ctx.ui:watch("inventory", function(old_list, new_list)
    print("Inventory changed from " .. #old_list .. " to " .. #new_list .. " items")
end)
```

## Observer Pattern (Change Detection)

### Notification System

When a binding value changes, observers are notified:

```cpp
class ObservableValue {
private:
    std::vector<std::function<void(const std::any&)>> observers;

public:
    void Subscribe(std::function<void(const std::any&)> observer) {
        observers.push_back(observer);
    }

    void Notify(const std::any& new_value) {
        for (auto& observer : observers) {
            observer(new_value);
        }
    }
};

// Usage
ObservableValue player_health(100);
player_health.Subscribe([](const std::any& value) {
    int health = std::any_cast<int>(value);
    std::cout << "Health changed to " << health << std::endl;
});

player_health.Notify(75);  // triggers observer
```

## Performance Optimization

### Batch Updates

For multiple property changes, batch them to avoid repeated layout passes:

```cpp
ui->BeginBatch();
player.health = 50;
player.mana = 30;
player.level = 2;
ui->EndBatch();  // layout recalculated once, not three times
```

### Dirty Flags

Only affected DOM is re-rendered:

```cpp
// This binding change only affects --health property
// Layout is re-computed only if --health is used in a layout rule
player_health = 50;

// This binding change affects multiple properties
// Layout is recomputed more extensively
player_position = {100, 200, 300};
```

## Debugging

### Binding Inspector

In editor console:

```
ui:inspect_bindings()
→ player.health = <ptr:0x12345678> (int) sync=OneWay
  player.mana = <ptr:0x87654321> (float) sync=OneWay
  inventory = <ptr:0x11111111> (vector<InventoryItem>) sync=OneWay
  (events) = 5 handlers registered
```

### Reactive Tracing

Log every binding change:

```cpp
ui->SetDebugMode(true);
// Output:
// [UI] Binding changed: --player-health: 100 → 75
// [UI] CSS property updated, 3 elements affected
// [UI] Layout dirty flag set
// [UI] Render batch rebuilt (12 rects, 5 text runs)
```

## Rules

1. **ALWAYS** expose state through bindings — never directly manipulate DOM from game code
2. **NEVER** modify a binding-controlled property directly in the HTML — use C++ API
3. **ALWAYS** batch property updates — minimize layout passes
4. **NEVER** create circular bindings — A depends on B, B depends on A → infinite loop
5. **ALWAYS** unsubscribe from events when UI is destroyed — prevent dangling listeners
