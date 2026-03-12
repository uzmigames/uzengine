# UI System — Debug Console

> **Status**: Planned (Phase 3)
> **Source**: `engine/ui/debug_console.h/.cpp`, `engine/console/console.h`
> **Depends on**: Input routing (§18.5), logging system (§7)

## Purpose

A Quake-style console overlay for gameplay debugging, live variable inspection, and command execution. Bound to backtick (`) key. Accessible during gameplay without pausing. Integrates with the logging system and the CVar (console variable) system.

## Console Overlay

### Activation

```cpp
class DebugConsole : public UIRoot {
private:
    bool visible = false;
    float slide_animation = 0.0f;  // 0.0 (hidden) to 1.0 (visible)

public:
    void Toggle() {
        visible = !visible;
        // Animate in/out over 0.2 seconds
    }

    bool IsVisible() const { return visible; }

    void Update(float delta_time) {
        if (visible) {
            slide_animation = std::min(slide_animation + delta_time / 0.2f, 1.0f);
        } else {
            slide_animation = std::max(slide_animation - delta_time / 0.2f, 0.0f);
        }

        // Position console in viewport
        float height = screen_height * 0.5f;  // covers half screen
        float y = -height * (1.0f - slide_animation);
        SetPosition({0, y});
    }
};

// Global input handler
bool HandleBacktickKey() {
    DebugConsole::GetInstance().Toggle();
    return true;  // consumed
}
```

### Console UI Structure

```html
<div id="debug-console">
    <!-- Output (scrollable) -->
    <div id="console-output" class="console-log">
        <div class="log-entry error">Error: Failed to load texture</div>
        <div class="log-entry warning">Warning: Physics step took 5.2ms</div>
        <div class="log-entry info">Info: Scene loaded</div>
        <div class="log-entry debug">Debug: Player position: (100, 50, -200)</div>
    </div>

    <!-- Separator -->
    <div class="console-divider"></div>

    <!-- Input -->
    <div id="console-input-line">
        <span class="prompt">></span>
        <input id="console-input" type="text" placeholder="Command...">
        <div id="console-autocomplete" class="autocomplete-dropdown" style="display: none;">
            <div class="autocomplete-item">gm_god</div>
            <div class="autocomplete-item">gm_noclip</div>
            <div class="autocomplete-item">get_player_health</div>
        </div>
    </div>

    <!-- History (up/down arrow) -->
    <div class="console-hint">Use ↑↓ for history, Tab for autocomplete, Escape to close</div>
</div>
```

### Styling

```css
#debug-console {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    height: 50vh;
    background-color: rgba(0, 0, 0, 0.9);
    border-bottom: 2px solid #00ff00;
    display: flex;
    flex-direction: column;
    font-family: "Courier New", monospace;
    font-size: 12px;
    color: #00ff00;  /* classic terminal green */
    z-index: 10000;  /* on top of everything */
}

#console-output {
    flex: 1;
    overflow-y: auto;
    padding: 8px;
    border-bottom: 1px solid #333;
}

.log-entry {
    margin: 2px 0;
    white-space: pre-wrap;
    word-break: break-word;
}

.log-entry.error {
    color: #ff4444;
}

.log-entry.warning {
    color: #ffff00;
}

.log-entry.info {
    color: #00ff00;
}

.log-entry.debug {
    color: #00ffff;
}

#console-input {
    flex: 1;
    background-color: transparent;
    border: none;
    color: inherit;
    font-family: inherit;
    outline: none;
}
```

## Command Registry

Commands are registered at startup:

```cpp
class ConsoleCommandRegistry {
private:
    std::map<std::string, std::function<void(const std::vector<std::string>&)>> commands;

public:
    void Register(StringView name, std::function<void(const std::vector<std::string>&)> handler) {
        commands[std::string(name)] = handler;
    }

    bool Execute(StringView command_line) {
        std::vector<std::string> args = Tokenize(command_line);
        if (args.empty()) return false;

        std::string cmd_name = args[0];
        if (!commands.count(cmd_name)) {
            UZ_LOG(Console, Error, "Unknown command: {}", cmd_name);
            return false;
        }

        try {
            commands[cmd_name](args);
            return true;
        } catch (const std::exception& e) {
            UZ_LOG(Console, Error, "Command error: {}", e.what());
            return false;
        }
    }

    std::vector<std::string> GetAllCommands() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : commands) {
            names.push_back(name);
        }
        return names;
    }
};
```

## Builtin Commands

### Navigation & Help

```cpp
// Register builtins
registry.Register("help", [&](const auto& args) {
    if (args.size() == 1) {
        // List all commands
        for (const auto& cmd : registry.GetAllCommands()) {
            console.Log(LogLevel::Info, cmd);
        }
    } else {
        // Help for specific command
        console.Log(LogLevel::Info, fmt::format("Help for {}: (not yet implemented)", args[1]));
    }
});

registry.Register("clear", [&](const auto& args) {
    console.Clear();
});

registry.Register("history", [&](const auto& args) {
    for (const auto& hist : console.GetHistory()) {
        console.Log(LogLevel::Info, hist);
    }
});
```

### Game Commands

```cpp
// Example: god mode toggle
registry.Register("gm_god", [game](const auto& args) {
    game->GetPlayer()->SetInvulnerable(!game->GetPlayer()->IsInvulnerable());
    const char* status = game->GetPlayer()->IsInvulnerable() ? "ON" : "OFF";
    console.Log(LogLevel::Info, fmt::format("God mode: {}", status));
});

// Example: teleport player
registry.Register("gm_teleport", [game](const auto& args) {
    if (args.size() < 4) {
        throw std::runtime_error("Usage: gm_teleport <x> <y> <z>");
    }

    Vec3 pos = {
        std::stof(args[1]),
        std::stof(args[2]),
        std::stof(args[3]),
    };

    game->GetPlayer()->SetPosition(pos);
    console.Log(LogLevel::Info, fmt::format("Teleported to ({:.1f}, {:.1f}, {:.1f})", pos.x, pos.y, pos.z));
});

// Example: query variable
registry.Register("get", [game](const auto& args) {
    if (args.size() < 2) {
        throw std::runtime_error("Usage: get <variable>");
    }

    std::string var_name = args[1];
    std::string value = game->QueryDebugVariable(var_name);
    console.Log(LogLevel::Info, fmt::format("{} = {}", var_name, value));
});
```

## Autocomplete

### Prefix Matching

As the user types, show matching commands:

```cpp
class AutocompleteEngine {
private:
    std::vector<std::string> all_commands;

public:
    std::vector<std::string> GetMatches(StringView prefix) const {
        std::vector<std::string> matches;

        for (const auto& cmd : all_commands) {
            if (cmd.find(prefix) == 0) {  // starts with prefix
                matches.push_back(cmd);
            }
        }

        // Sort by length (shorter = more likely)
        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.length() < b.length(); });

        return matches;
    }
};
```

### Tab Completion

Pressing Tab auto-fills the best match:

```cpp
bool DebugConsole::OnKeyDown(KeyCode key) {
    if (key == KeyCode::Tab) {
        std::string current_input = GetInputText();

        // Find command prefix (before first space)
        size_t space_pos = current_input.find(' ');
        std::string prefix = (space_pos == std::string::npos) ? current_input : current_input.substr(0, space_pos);

        auto matches = autocomplete.GetMatches(prefix);
        if (matches.size() == 1) {
            // Unique match, auto-fill
            SetInputText(matches[0] + " ");
            input_field->SetCursorEnd();
        } else if (matches.size() > 1) {
            // Multiple matches, show dropdown
            ShowAutocompleteDropdown(matches);
        }

        return true;
    }

    return false;
}
```

## CVars (Console Variables)

Live variable inspection and modification:

```cpp
class ConsoleCVar {
public:
    std::string name;
    std::string description;

    virtual ~ConsoleCVar() = default;

    virtual std::string GetAsString() const = 0;
    virtual void SetFromString(StringView value) = 0;
};

template<typename T>
class CVar : public ConsoleCVar {
private:
    T* value_ptr;  // pointer to game variable

public:
    CVar(StringView name, T* ptr, StringView desc = "")
        : value_ptr(ptr) {
        this->name = name;
        this->description = desc;
    }

    std::string GetAsString() const override {
        return std::to_string(*value_ptr);  // specialize for string, bool, etc.
    }

    void SetFromString(StringView str) override {
        *value_ptr = Parse<T>(str);
    }
};

class CVarRegistry {
private:
    std::map<std::string, std::unique_ptr<ConsoleCVar>> cvars;

public:
    void Register(std::unique_ptr<ConsoleCVar> cvar) {
        cvars[cvar->name] = std::move(cvar);
    }

    ConsoleCVar* Get(StringView name) {
        if (cvars.count(std::string(name))) {
            return cvars[std::string(name)].get();
        }
        return nullptr;
    }

    std::vector<std::string> GetAllCVars() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : cvars) {
            names.push_back(name);
        }
        return names;
    }
};
```

### CVar Commands

```cpp
// Register CVar accessors
registry.Register("set", [cvar_registry](const auto& args) {
    if (args.size() < 3) {
        throw std::runtime_error("Usage: set <cvar> <value>");
    }

    auto cvar = cvar_registry.Get(args[1]);
    if (!cvar) {
        throw std::runtime_error(fmt::format("Unknown CVar: {}", args[1]));
    }

    cvar->SetFromString(args[2]);
    console.Log(LogLevel::Info, fmt::format("{} = {}", args[1], cvar->GetAsString()));
});

registry.Register("get", [cvar_registry](const auto& args) {
    if (args.size() < 2) {
        throw std::runtime_error("Usage: get <cvar>");
    }

    auto cvar = cvar_registry.Get(args[1]);
    if (!cvar) {
        throw std::runtime_error(fmt::format("Unknown CVar: {}", args[1]));
    }

    console.Log(LogLevel::Info, fmt::format("{} = {}", args[1], cvar->GetAsString()));
});

registry.Register("cvars", [cvar_registry](const auto& args) {
    console.Log(LogLevel::Info, "Registered CVars:");
    for (const auto& name : cvar_registry.GetAllCVars()) {
        auto cvar = cvar_registry.Get(name);
        console.Log(LogLevel::Info, fmt::format("  {} = {} ({})", name, cvar->GetAsString(), cvar->description));
    }
});
```

### Registering Game CVars

At startup, register variables for live tweaking:

```cpp
// In game initialization
cvar_registry.Register(std::make_unique<CVar<float>>(
    "gravity", &physics_gravity, "Gravitational acceleration (m/s^2)"
));

cvar_registry.Register(std::make_unique<CVar<float>>(
    "player_speed", &player_max_speed, "Player movement speed"
));

cvar_registry.Register(std::make_unique<CVar<int>>(
    "debug_draw", &debug_draw_flags, "Debug visualization flags"
));

// Now in console:
// > set gravity 15.0
// > get player_speed
// > cvars
```

## Command History

```cpp
class DebugConsole {
private:
    std::vector<std::string> command_history;
    int history_index = -1;  // -1 = new input

public:
    void SubmitCommand(StringView command) {
        command_history.push_back(std::string(command));
        history_index = -1;  // reset to new input
        registry.Execute(command);
    }

    void NavigateHistory(int direction) {
        // direction: 1 = up (older), -1 = down (newer)
        if (history_index == -1 && direction == 1) {
            history_index = command_history.size() - 1;
        } else {
            history_index += direction;
            history_index = std::clamp(history_index, -1, (int)command_history.size() - 1);
        }

        if (history_index >= 0) {
            input_field->SetText(command_history[history_index]);
        } else {
            input_field->SetText("");
        }
    }
};

bool DebugConsole::OnKeyDown(KeyCode key) {
    if (key == KeyCode::Up) {
        NavigateHistory(1);
        return true;
    } else if (key == KeyCode::Down) {
        NavigateHistory(-1);
        return true;
    }

    return false;
}
```

## Log Filtering

```cpp
class LogFilter {
private:
    LogLevel min_level = LogLevel::Debug;
    std::set<std::string> enabled_categories;  // empty = all

public:
    void SetMinLevel(LogLevel level) { min_level = level; }

    void EnableCategory(StringView cat) {
        enabled_categories.insert(std::string(cat));
    }

    void DisableCategory(StringView cat) {
        enabled_categories.erase(std::string(cat));
    }

    bool ShouldShow(LogLevel level, StringView category) const {
        if (level < min_level) return false;

        if (!enabled_categories.empty()) {
            return enabled_categories.count(std::string(category)) > 0;
        }

        return true;  // all categories enabled
    }
};

// Commands
registry.Register("log_level", [console](const auto& args) {
    if (args.size() < 2) {
        throw std::runtime_error("Usage: log_level <debug|info|warning|error>");
    }

    LogLevel level = ParseLogLevel(args[1]);
    console.SetMinLevel(level);
});

registry.Register("log_enable", [console](const auto& args) {
    if (args.size() < 2) {
        throw std::runtime_error("Usage: log_enable <category>");
    }

    console.EnableCategory(args[1]);
});
```

## Performance Considerations

### Limit Output Buffer

Console keeps only last N log entries to avoid memory bloat:

```cpp
class ConsoleOutput {
private:
    static constexpr size_t MAX_LINES = 1000;
    std::deque<LogEntry> log_buffer;  // circular buffer

public:
    void AddLog(const LogEntry& entry) {
        log_buffer.push_back(entry);

        // Maintain max size
        if (log_buffer.size() > MAX_LINES) {
            log_buffer.pop_front();
        }
    }
};
```

### Lazy Rendering

Only update console UI when visible:

```cpp
void DebugConsole::Update(float delta_time) {
    if (!visible && slide_animation == 0.0f) {
        return;  // skip update when fully hidden
    }

    // Update animation, input processing, etc.
}
```

## Rules

1. **ALWAYS** bind console to backtick (`) — standard Quake convention
2. **NEVER** require pausing to open console — should work during gameplay
3. **ALWAYS** persist command history — across game sessions
4. **NEVER** allow commands to corrupt game state permanently — all god-mode changes are temporary
5. **ALWAYS** validate command arguments — throw on parse errors, not silent failures
