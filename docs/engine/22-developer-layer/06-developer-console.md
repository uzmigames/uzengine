# Developer Console — Commands and CVars

> **Status**: Planned (Phase 2)
> **Source**: `developer/console/`
> **References**: Quake console, Source engine console (clean-room)

## Purpose

Interactive command-line console for runtime tweaking and debugging:
- **Commands** — `teleport 10 5 20`, `spawn_enemy goblin`
- **CVars** (Console Variables) — runtime-tweakable settings that persist
- **Autocomplete** — tab-completion for commands and cvar names
- **History** — up/down arrow to recall previous commands
- **Execution** — immediate feedback on command results

All console code compiles out in shipping builds.

## Console API

```cpp
namespace uz::dev {

/// Command handler: args[0] is command name, args[1..N] are parameters
/// Return true if succeeded, false if failed (prints usage)
using ConsoleCommand = std::function<bool(const std::vector<std::string>& args)>;

/// Register a command
void RegisterCommand(
    StringView name,
    StringView usage,  // "teleport <x> <y> <z>  — Move player to position"
    ConsoleCommand fn
);

/// Programmatically execute a console command
bool Execute(StringView command_line);

/// Get command history (last N lines)
std::vector<std::string> GetHistory(int max_lines = 100);

/// Find matching commands for autocomplete
std::vector<std::string> GetCommandMatches(StringView prefix);

/// Clear console output
void Clear();

}  // namespace uz::dev
```

## Command Registration

```cpp
// Register a command
uz::dev::RegisterCommand(
    "teleport",
    "teleport <x> <y> <z>  — Teleport player to position",
    [](const auto& args) {
        if (args.size() != 4) {  // cmd + 3 args
            return false;  // Print usage
        }

        float x = std::stof(args[1]);
        float y = std::stof(args[2]);
        float z = std::stof(args[3]);

        auto player = g_world->FindEntity("Player");
        if (!player) {
            UZ_LOG(Console, Error, "Player not found");
            return false;
        }

        player->SetPosition({x, y, z});
        UZ_LOG(Console, Info, "Teleported to ({}, {}, {})", x, y, z);
        return true;
    }
);

// In console: `teleport 10 5 20`
```

## CVar System

Console variables that persist and can be modified at runtime:

```cpp
#if UZ_DEV_BUILD
    #define UZ_CVAR(Type, Name, DefaultValue, Description) \
        static uz::dev::CVar<Type> Name(#Name, DefaultValue, Description)
#else
    #define UZ_CVAR(Type, Name, DefaultValue, Description) \
        static constexpr Type Name = DefaultValue
#endif

// Usage:
UZ_CVAR(float, r_shadowDistance, 100.0f, "Max shadow draw distance (cm)");
UZ_CVAR(int, p_substeps, 4, "Physics sub-steps per frame");
UZ_CVAR(bool, a_enable_reverb, true, "Enable audio reverb");

void RenderShadows() {
    float distance = r_shadowDistance.Get();  // Read current value
    // ... render shadows up to distance
}
```

### CVar Class

```cpp
namespace uz::dev {

template<typename T>
class CVar {
public:
    /// Create CVar (called by UZ_CVAR macro)
    CVar(const char* name, const T& default_value, const char* description) {
        Register(name, default_value, description);
    }

    /// Get current value
    T Get() const { return value_; }

    /// Set value (can be done from console)
    void Set(const T& new_value) {
        T old_value = value_;
        value_ = new_value;
        OnChanged.Broadcast(old_value, new_value);
    }

    /// Callback when value changes
    MulticastDelegate<void(const T& old_val, const T& new_val)> OnChanged;

private:
    T value_;
    std::string name_;
    std::string description_;
};

}  // namespace uz::dev
```

## CVar Categories

CVars are prefixed by category for organization:

| Prefix | Category |
|--------|----------|
| `r_` | Renderer (shadows, LOD, effects) |
| `p_` | Physics (gravity, substeps, friction) |
| `a_` | Audio (volume, reverb, effects) |
| `l_` | Lua (debug, error logging) |
| `g_` | Game (difficulty, ai behavior) |
| `ed_` | Editor (viewport, selection) |

```cpp
// Renderer CVars
UZ_CVAR(float, r_shadowDistance, 100.0f, "");
UZ_CVAR(int, r_maxDrawCalls, 10000, "");

// Physics CVars
UZ_CVAR(Vec3, p_gravity, Vec3(0, -9.81f, 0), "");
UZ_CVAR(int, p_substeps, 4, "");

// Audio CVars
UZ_CVAR(float, a_masterVolume, 1.0f, "");
UZ_CVAR(float, a_musicVolume, 0.7f, "");

// In console:
//   r_shadowDistance 200
//   p_gravity 0 -20 0
//   a_masterVolume 0.5
```

## Persistence

CVars can be saved/loaded from config file:

```cpp
namespace uz::dev {

/// Load CVars from config file
void LoadCVarsFromFile(StringView config_path);

/// Save current CVar values to config file
void SaveCVarsToFile(StringView config_path);

}  // namespace uz::dev

// Usage:
// Load previous settings
uz::dev::LoadCVarsFromFile(".config/engine.cfg");

// Game runs...

// Save new settings on shutdown
uz::dev::SaveCVarsToFile(".config/engine.cfg");
```

Config file format:

```
# Engine Configuration
r_shadowDistance 150.0
p_gravity 0 -9.81 0
a_masterVolume 0.8
enable_vsync true
```

## Common Commands

```cpp
// Help
uz::dev::RegisterCommand(
    "help",
    "help [command]  — Show command list or help for specific command",
    [](const auto& args) {
        if (args.size() == 1) {
            // List all commands
            auto commands = uz::dev::GetAllCommands();
            for (auto& cmd : commands) {
                std::cout << cmd.name << "  — " << cmd.usage << "\n";
            }
        } else {
            // Show help for specific command
            auto cmd = uz::dev::FindCommand(args[1]);
            if (cmd) {
                std::cout << cmd->usage << "\n";
            }
        }
        return true;
    }
);

// List CVars
uz::dev::RegisterCommand(
    "cvars",
    "cvars [pattern]  — List all CVars (optionally filtered)",
    [](const auto& args) {
        std::string pattern = (args.size() > 1) ? args[1] : "";
        auto cvars = uz::dev::GetCVars(pattern);
        for (auto& cvar : cvars) {
            std::cout << cvar.name << " = " << cvar.value << "\n";
        }
        return true;
    }
);

// Spawn entity
uz::dev::RegisterCommand(
    "spawn",
    "spawn <type> [x] [y] [z]  — Spawn entity",
    [](const auto& args) {
        if (args.size() < 2) return false;

        std::string entity_type = args[1];
        Vec3 position = (args.size() >= 5) ?
            Vec3(std::stof(args[2]), std::stof(args[3]), std::stof(args[4])) :
            Vec3::Zero();

        auto entity = g_world->Create();
        g_world->SetPosition(entity, position);
        // ... attach components based on entity_type

        UZ_LOG(Console, Info, "Spawned {} at ({}, {}, {})",
            entity_type, position.x, position.y, position.z);
        return true;
    }
);

// Pause/resume
uz::dev::RegisterCommand(
    "pause",
    "pause  — Pause the game",
    [](const auto& args) {
        g_engine->Pause();
        return true;
    }
);

// Set time scale
uz::dev::RegisterCommand(
    "timescale",
    "timescale <scale>  — Set game speed (1.0 = normal)",
    [](const auto& args) {
        if (args.size() != 2) return false;
        float scale = std::stof(args[1]);
        g_engine->SetTimeScale(scale);
        return true;
    }
);
```

## Lua Integration

Execute Lua from console:

```cpp
uz::dev::RegisterCommand(
    "lua",
    "lua <code>  — Execute Lua code",
    [](const auto& args) {
        if (args.size() < 2) return false;

        std::string code = args[1];
        // Rejoin remaining args (for code with spaces)
        for (size_t i = 2; i < args.size(); ++i) {
            code += " " + args[i];
        }

        auto result = lua_system->Eval(code);
        if (result.error.empty()) {
            std::cout << result.output << "\n";
            return true;
        } else {
            std::cout << "Error: " << result.error << "\n";
            return false;
        }
    }
);

// In console: `lua player:teleport(10, 5, 20)`
```

## Autocomplete

Tab completion for commands and arguments:

```cpp
namespace uz::dev {

class ConsoleAutocomplete {
public:
    /// Get suggestions for partial input
    std::vector<std::string> GetSuggestions(StringView partial_input) {
        if (partial_input.empty()) {
            return GetAllCommands();
        }

        std::vector<std::string> suggestions;

        // Match command names
        auto commands = uz::dev::GetCommandMatches(partial_input);
        suggestions.insert(suggestions.end(), commands.begin(), commands.end());

        // Match CVar names
        auto cvars = uz::dev::GetCVarMatches(partial_input);
        for (auto& cvar : cvars) {
            suggestions.push_back(cvar.name);
        }

        // Match entity names (for spawn command, etc.)
        if (partial_input.find("spawn") == 0) {
            auto entity_types = GetEntityTypes();
            suggestions.insert(suggestions.end(), entity_types.begin(), entity_types.end());
        }

        return suggestions;
    }
};

}  // namespace uz::dev
```

## Lua Binding

```lua
-- Execute console command from Lua
uz.console.execute("teleport 10 5 20")

-- Read CVar value
local shadow_dist = uz.console.get_cvar("r_shadowDistance")
print("Shadow distance: " .. shadow_dist)

-- Set CVar value
uz.console.set_cvar("r_shadowDistance", 200.0)

-- Get command history
local history = uz.console.get_history(10)
for _, cmd in ipairs(history) do
    print(cmd)
end
```

## ImGui Console Widget

Render console in viewport:

```cpp
namespace uz::dev {

class ConsoleUI {
public:
    void ShowConsole(bool* p_open = nullptr) {
        if (!ImGui::Begin("Console", p_open)) {
            ImGui::End();
            return;
        }

        // Output log
        ImGui::TextWrapped("%s", GetConsoleOutput().c_str());
        ImGui::Separator();

        // Input field
        static char input_buffer[256] = "";
        if (ImGui::InputText("##console_input", input_buffer, sizeof(input_buffer),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion)) {

            // Execute command
            uz::dev::Execute(input_buffer);
            input_buffer[0] = '\0';
        }

        ImGui::End();
    }
};

}  // namespace uz::dev
```

## Configuration

`.uzproj` console settings:

```toml
[developer]
enable_console = true
console_log_file = "logs/console.log"
cvar_config_file = ".config/engine.cfg"
max_history = 200
```

## Comparison

| Feature | Quake console | Source console | Unreal console | **UzEngine** |
|---------|---|---|---|---|
| Commands | Yes | Yes | Yes (Exec) | Yes |
| CVars | Yes | Yes | Yes | Yes |
| Autocomplete | Limited | Yes | Yes | Yes |
| History | Yes | Yes | Yes | Yes |
| Persistence | Manual | `.cfg` files | `.ini` files | `.cfg` files |
| Lua integration | No | No | No | Yes |
| ImGui rendering | N/A | No | No | Yes |

## Implementation Phase

- **Phase 2**: Basic commands, CVar declaration, command execution, history
- **Phase 3**: CVar persistence, autocomplete, ImGui console widget, Lua binding
- **Phase 4**: Advanced commands (entity spawning, scene manipulation), remote console (network)
- **Phase 5**: Visual command tree (VSCode sidebar), command logging to file, command macros
