# Editor Preferences — Configuration & Keybindings

> **Status**: Planned (Phase 2)
> **Source**: `runtime/editor/preferences.h`, `extension/editor/preferences.ts`
> **References**: VSCode Settings Sync, Godot Editor Settings, UE5 Editor Preferences

## Purpose

Editor preferences control viewport behavior, keybinding customization, layout persistence, rendering quality, and per-project or global settings. Users should be able to customize their development environment to match their workflow without modifying code.

## Architecture

```
Preference Categories:
    ├─ Viewport Settings (FOV, camera speed, grid size)
    ├─ Keybindings (gizmo hotkeys, shortcut overrides)
    ├─ Layout Persistence (panel positions, sizes, visibility)
    ├─ Performance Quality (editor viewport rendering)
    ├─ Theme & UI (follows VSCode theme, accent colors)
    ├─ Auto-Save & Recovery
    ├─ Developer Tools (profiling, debug draw, console)
    └─ Plugin Configuration

Storage Hierarchy:
    Global: ~/.uzengine/preferences.json
    Project: .uzproject/editor/preferences.json (overrides global)
    Session: EditorPreferences (in-memory, not persisted between sessions)
```

## Storage Location

### Global Preferences

`~/.uzengine/preferences.json` — shared across all projects

```json
{
  "editor": {
    "language": "en",
    "theme": "dark",
    "camera": {
      "fov": 60.0,
      "move_speed": 0.05,
      "rotate_speed": 1.0,
      "near_clip": 0.1,
      "far_clip": 10000.0
    },
    "grid": {
      "size": 1.0,
      "visible": true,
      "snap_enabled": false,
      "snap_size": 0.25
    },
    "gizmo": {
      "size": 1.0,
      "local_space": true
    },
    "viewport": {
      "vsync": true,
      "quality": "high"
    },
    "auto_save": {
      "enabled": true,
      "interval_seconds": 60
    },
    "theme": {
      "accent_color": "#00CCFF",
      "text_color": "#FFFFFF",
      "background_color": "#1E1E1E"
    }
  },
  "keybindings": {
    "viewport": {
      "translate": "w",
      "rotate": "e",
      "scale": "r",
      "focus": "f",
      "frame_all": "shift+f",
      "camera_reset": "home"
    },
    "editor": {
      "undo": "ctrl+z",
      "redo": "ctrl+y",
      "duplicate": "ctrl+d",
      "delete": "delete",
      "save": "ctrl+s",
      "open_project": "ctrl+shift+o"
    },
    "scene": {
      "play": "ctrl+shift+p",
      "pause": "ctrl+shift+space",
      "frame_rate": "ctrl+shift+r"
    }
  },
  "recent_projects": [
    "/path/to/project1/.uzproject",
    "/path/to/project2/.uzproject"
  ]
}
```

### Project Preferences

`.uzproject/editor/preferences.json` — project-specific overrides

```json
{
  "editor": {
    "camera": {
      "move_speed": 0.10,
      "fov": 75.0
    },
    "grid": {
      "size": 0.5,
      "snap_enabled": true,
      "snap_size": 0.25
    },
    "auto_save": {
      "interval_seconds": 30
    }
  },
  "keybindings": {
    "viewport": {
      "translate": "q"
    }
  },
  "layout": {
    "panels": {
      "inspector": { "x": 0.7, "y": 0.0, "width": 0.3, "height": 0.5 },
      "hierarchy": { "x": 0.0, "y": 0.0, "width": 0.3, "height": 0.5 },
      "viewport": { "x": 0.3, "y": 0.0, "width": 0.4, "height": 1.0 },
      "asset_browser": { "x": 0.0, "y": 0.5, "width": 0.7, "height": 0.5 }
    },
    "console": { "visible": true, "height": 0.25 }
  }
}
```

## C++ API

```cpp
/// Editor preferences system.
class EditorPreferences {
public:
    /// Load preferences from disk (global then project).
    /// Project preferences override global if both exist.
    static EditorPreferences Load(StringView project_path);

    /// Save all modified preferences to disk.
    /// Saves only to project preferences if in project context,
    /// otherwise saves to global preferences.
    void Save();

    // ─────────────────────────────────────────────────────────────────────────
    // Viewport Settings
    // ─────────────────────────────────────────────────────────────────────────

    /// Camera field of view (degrees).
    float GetCameraFOV() const;
    void SetCameraFOV(float fov);

    /// Camera movement speed (units per frame at default speed).
    float GetCameraMoveSpeed() const;
    void SetCameraMoveSpeed(float speed);

    /// Camera rotation speed (degrees per unit mouse movement).
    float GetCameraRotateSpeed() const;
    void SetCameraRotateSpeed(float speed);

    float GetCameraNearClip() const;
    void SetCameraNearClip(float near_clip);

    float GetCameraFarClip() const;
    void SetCameraFarClip(float far_clip);

    // ─────────────────────────────────────────────────────────────────────────
    // Grid Settings
    // ─────────────────────────────────────────────────────────────────────────

    float GetGridSize() const;
    void SetGridSize(float size);

    bool GetGridVisible() const;
    void SetGridVisible(bool visible);

    bool GetGridSnapEnabled() const;
    void SetGridSnapEnabled(bool enabled);

    float GetGridSnapSize() const;
    void SetGridSnapSize(float size);

    // ─────────────────────────────────────────────────────────────────────────
    // Gizmo Settings
    // ─────────────────────────────────────────────────────────────────────────

    float GetGizmoSize() const;
    void SetGizmoSize(float size);

    /// True = move/rotate in local space, False = world space.
    bool GetGizmoLocalSpace() const;
    void SetGizmoLocalSpace(bool local);

    // ─────────────────────────────────────────────────────────────────────────
    // Viewport Rendering
    // ─────────────────────────────────────────────────────────────────────────

    bool GetVSyncEnabled() const;
    void SetVSyncEnabled(bool enabled);

    enum class Quality { Low, Medium, High, Ultra };
    Quality GetViewportQuality() const;
    void SetViewportQuality(Quality quality);

    // ─────────────────────────────────────────────────────────────────────────
    // Auto-Save
    // ─────────────────────────────────────────────────────────────────────────

    bool GetAutoSaveEnabled() const;
    void SetAutoSaveEnabled(bool enabled);

    uint32_t GetAutoSaveIntervalSeconds() const;
    void SetAutoSaveIntervalSeconds(uint32_t seconds);

    // ─────────────────────────────────────────────────────────────────────────
    // Theme
    // ─────────────────────────────────────────────────────────────────────────

    Color GetAccentColor() const;
    void SetAccentColor(Color color);

    Color GetTextColor() const;
    void SetTextColor(Color color);

    Color GetBackgroundColor() const;
    void SetBackgroundColor(Color color);

    // ─────────────────────────────────────────────────────────────────────────
    // Keybindings
    // ─────────────────────────────────────────────────────────────────────────

    /// Get keybinding for action (e.g., "translate", "rotate").
    /// Returns key combo string (e.g., "w", "ctrl+z", "shift+f").
    std::string GetKeybinding(StringView action) const;

    /// Set custom keybinding for action.
    void SetKeybinding(StringView action, StringView key_combo);

    /// Reset all keybindings to defaults.
    void ResetKeybindingsToDefaults();

    /// Validate keybinding (check for conflicts, syntax).
    bool IsValidKeybinding(StringView key_combo, std::string* out_error = nullptr);

    // ─────────────────────────────────────────────────────────────────────────
    // Recent Projects
    // ─────────────────────────────────────────────────────────────────────────

    void GetRecentProjects(std::vector<fs::path>& out_paths) const;

    void AddRecentProject(fs::path project_path);

    void ClearRecentProjects();

    // ─────────────────────────────────────────────────────────────────────────
    // Layout Persistence
    // ─────────────────────────────────────────────────────────────────────────

    struct PanelLayout {
        float x, y;           // Position (normalized 0-1)
        float width, height;  // Size (normalized 0-1)
    };

    /// Get saved position/size for panel (or default if not found).
    PanelLayout GetPanelLayout(StringView panel_name) const;

    /// Save panel position/size (called before exit).
    void SetPanelLayout(StringView panel_name, const PanelLayout& layout);

    bool IsPanelVisible(StringView panel_name) const;
    void SetPanelVisible(StringView panel_name, bool visible);

    // ─────────────────────────────────────────────────────────────────────────
    // Events
    // ─────────────────────────────────────────────────────────────────────────

    /// Fired when any preference changes (for real-time UI update).
    MulticastDelegate<void(StringView preference_path, const Variant& new_value)>
        OnPreferenceChanged;

private:
    nlohmann::json global_prefs_;
    nlohmann::json project_prefs_;
    fs::path global_prefs_path_;
    fs::path project_prefs_path_;

    void LoadGlobalPreferences();
    void LoadProjectPreferences();
    nlohmann::json GetEffectivePreferences() const;  // Merge global + project
};
```

## Default Keybindings

| Action | Default | Category |
|--------|---------|----------|
| **Translate** | W | Viewport Gizmo |
| **Rotate** | E | Viewport Gizmo |
| **Scale** | R | Viewport Gizmo |
| **Focus Selected** | F | Viewport Camera |
| **Frame All** | Shift+F | Viewport Camera |
| **Camera Reset** | Home | Viewport Camera |
| **Pan Camera** | Middle Mouse Drag | Viewport Camera |
| **Orbit Camera** | Right Mouse Drag | Viewport Camera |
| **Undo** | Ctrl+Z | Editor |
| **Redo** | Ctrl+Y | Editor |
| **Duplicate** | Ctrl+D | Editor |
| **Delete** | Delete | Editor |
| **Save Scene** | Ctrl+S | Editor |
| **Open Project** | Ctrl+Shift+O | Editor |
| **Play** | Ctrl+Shift+P | Scene |
| **Pause** | Ctrl+Shift+Space | Scene |
| **Frame Rate Toggle** | Ctrl+Shift+R | Scene |

## Keybinding Format

VSCode-compatible key combination syntax:

```
<modifier>+<modifier>+<key>

Modifiers: ctrl, shift, alt, cmd (macOS)
Keys: a-z, 0-9, f1-f12, enter, space, tab, backspace, delete, home, end, pageup, pagedown,
      up, down, left, right, plus, minus, bracketleft, bracketright
```

Examples:
- `w` — single key
- `ctrl+z` — control modifier
- `shift+ctrl+s` — multiple modifiers
- `f1` — function key
- `ctrl+shift+equal` — combination with special key

## TypeScript UI

```typescript
// extension/editor/preferences.ts

interface EditorPreferencesUI {
    camera: {
        fov: number;
        moveSpeed: number;
        rotateSpeed: number;
        nearClip: number;
        farClip: number;
    };
    grid: {
        size: number;
        visible: boolean;
        snapEnabled: boolean;
        snapSize: number;
    };
    gizmo: {
        size: number;
        localSpace: boolean;
    };
    viewport: {
        vsync: boolean;
        quality: 'low' | 'medium' | 'high' | 'ultra';
    };
    autoSave: {
        enabled: boolean;
        intervalSeconds: number;
    };
    theme: {
        accentColor: string;  // Hex color
        textColor: string;
        backgroundColor: string;
    };
}

class EditorPreferencesUI {
    constructor(private runtimeBridge: RuntimeBridge) {}

    async loadPreferences() {
        const prefs = await this.runtimeBridge.call('editor.get_preferences');
        this.renderPreferencesPanel(prefs);
    }

    async savePreferences(prefs: EditorPreferencesUI) {
        await this.runtimeBridge.call('editor.set_preferences', prefs);
    }

    renderPreferencesPanel(prefs: EditorPreferencesUI) {
        const container = document.getElementById('preferences-panel');

        // Viewport settings
        const viewportSection = this.createSection('Viewport', [
            { label: 'FOV', type: 'slider', min: 20, max: 120, value: prefs.camera.fov },
            { label: 'Move Speed', type: 'slider', min: 0.01, max: 1.0, value: prefs.camera.moveSpeed },
            { label: 'Rotate Speed', type: 'slider', min: 0.1, max: 5.0, value: prefs.camera.rotateSpeed },
            { label: 'Near Clip', type: 'input', type_hint: 'number', value: prefs.camera.nearClip },
            { label: 'Far Clip', type: 'input', type_hint: 'number', value: prefs.camera.farClip },
        ]);
        container.appendChild(viewportSection);

        // Grid settings
        const gridSection = this.createSection('Grid', [
            { label: 'Grid Size', type: 'slider', min: 0.1, max: 10.0, value: prefs.grid.size },
            { label: 'Visible', type: 'toggle', value: prefs.grid.visible },
            { label: 'Snap to Grid', type: 'toggle', value: prefs.grid.snapEnabled },
            { label: 'Snap Size', type: 'slider', min: 0.1, max: 5.0, value: prefs.grid.snapSize },
        ]);
        container.appendChild(gridSection);

        // Gizmo settings
        const gizmoSection = this.createSection('Gizmo', [
            { label: 'Size', type: 'slider', min: 0.5, max: 3.0, value: prefs.gizmo.size },
            { label: 'Local Space', type: 'toggle', value: prefs.gizmo.localSpace },
        ]);
        container.appendChild(gizmoSection);

        // Viewport rendering
        const renderSection = this.createSection('Rendering', [
            { label: 'VSync', type: 'toggle', value: prefs.viewport.vsync },
            { label: 'Quality', type: 'dropdown', options: ['low', 'medium', 'high', 'ultra'], value: prefs.viewport.quality },
        ]);
        container.appendChild(renderSection);

        // Auto-save
        const autoSaveSection = this.createSection('Auto-Save', [
            { label: 'Enabled', type: 'toggle', value: prefs.autoSave.enabled },
            { label: 'Interval (seconds)', type: 'input', type_hint: 'number', value: prefs.autoSave.intervalSeconds },
        ]);
        container.appendChild(autoSaveSection);

        // Theme
        const themeSection = this.createSection('Theme', [
            { label: 'Accent Color', type: 'color', value: prefs.theme.accentColor },
            { label: 'Text Color', type: 'color', value: prefs.theme.textColor },
            { label: 'Background Color', type: 'color', value: prefs.theme.backgroundColor },
        ]);
        container.appendChild(themeSection);

        // Add save button
        const saveButton = document.createElement('button');
        saveButton.textContent = 'Save Preferences';
        saveButton.addEventListener('click', () => {
            const updated = this.readFormValues();
            this.savePreferences(updated);
        });
        container.appendChild(saveButton);
    }

    private createSection(title: string, fields: any[]): HTMLElement {
        const section = document.createElement('fieldset');
        section.classList.add('preferences-section');

        const legend = document.createElement('legend');
        legend.textContent = title;
        section.appendChild(legend);

        for (const field of fields) {
            const control = this.createControl(field);
            section.appendChild(control);
        }

        return section;
    }

    private createControl(field: any): HTMLElement {
        const div = document.createElement('div');
        div.classList.add('preference-control');

        const label = document.createElement('label');
        label.textContent = field.label;
        div.appendChild(label);

        let input: HTMLElement;
        switch (field.type) {
            case 'toggle':
                input = document.createElement('input');
                (input as HTMLInputElement).type = 'checkbox';
                (input as HTMLInputElement).checked = field.value;
                break;

            case 'slider':
                input = document.createElement('input');
                (input as HTMLInputElement).type = 'range';
                (input as HTMLInputElement).min = String(field.min);
                (input as HTMLInputElement).max = String(field.max);
                (input as HTMLInputElement).value = String(field.value);
                break;

            case 'input':
                input = document.createElement('input');
                (input as HTMLInputElement).type = field.type_hint || 'text';
                (input as HTMLInputElement).value = String(field.value);
                break;

            case 'color':
                input = document.createElement('input');
                (input as HTMLInputElement).type = 'color';
                (input as HTMLInputElement).value = field.value;
                break;

            case 'dropdown':
                input = document.createElement('select');
                for (const option of field.options) {
                    const opt = document.createElement('option');
                    opt.value = option;
                    opt.textContent = option;
                    (input as HTMLSelectElement).appendChild(opt);
                }
                (input as HTMLSelectElement).value = field.value;
                break;

            default:
                input = document.createElement('span');
                input.textContent = 'Unknown control type';
        }

        input.setAttribute('data-pref-key', field.label);
        div.appendChild(input);
        return div;
    }
}
```

## Keybindings Editor

```typescript
// extension/editor/keybindings_ui.ts

class KeybindingsUI {
    constructor(private runtimeBridge: RuntimeBridge) {}

    async loadKeybindings() {
        const keybindings = await this.runtimeBridge.call('editor.get_keybindings');
        this.renderKeybindingsTable(keybindings);
    }

    renderKeybindingsTable(keybindings: Record<string, Record<string, string>>) {
        const container = document.getElementById('keybindings-panel');
        const table = document.createElement('table');

        // Header
        const header = table.createTHead();
        const headerRow = header.insertRow();
        headerRow.insertCell(0).textContent = 'Action';
        headerRow.insertCell(1).textContent = 'Current';
        headerRow.insertCell(2).textContent = 'Set';
        headerRow.insertCell(3).textContent = 'Reset';

        // Body
        const body = table.createTBody();
        for (const [category, actions] of Object.entries(keybindings)) {
            const categoryRow = body.insertRow();
            const categoryCell = categoryRow.insertCell(0);
            categoryCell.colSpan = 4;
            categoryCell.textContent = category;
            categoryCell.style.fontWeight = 'bold';
            categoryCell.style.backgroundColor = '#2d2d2d';

            for (const [action, current_binding] of Object.entries(actions)) {
                const row = body.insertRow();
                row.insertCell(0).textContent = action;
                const currentCell = row.insertCell(1);
                currentCell.textContent = current_binding;
                currentCell.style.fontFamily = 'monospace';

                const setCell = row.insertCell(2);
                const setButton = document.createElement('button');
                setButton.textContent = 'Edit';
                setButton.addEventListener('click', () => {
                    this.openKeybindingDialog(action, current_binding);
                });
                setCell.appendChild(setButton);

                const resetCell = row.insertCell(3);
                const resetButton = document.createElement('button');
                resetButton.textContent = 'Reset';
                resetButton.addEventListener('click', async () => {
                    await this.runtimeBridge.call('editor.reset_keybinding', { action });
                    this.loadKeybindings();
                });
                resetCell.appendChild(resetButton);
            }
        }

        container.appendChild(table);
    }

    private openKeybindingDialog(action: string, current: string) {
        const dialog = document.createElement('div');
        dialog.classList.add('keybinding-dialog');
        dialog.innerHTML = `
            <h3>Set Keybinding for: ${action}</h3>
            <p>Current: <code>${current}</code></p>
            <p>Press the key combination you want to use...</p>
            <div id="key-input" style="border: 1px solid #ccc; padding: 10px;">
                <span id="key-display">-</span>
            </div>
            <div>
                <button id="accept">Accept</button>
                <button id="cancel">Cancel</button>
            </div>
        `;

        const keyDisplay = dialog.querySelector('#key-display') as HTMLElement;
        let capturedKeys: string[] = [];

        dialog.addEventListener('keydown', (e) => {
            e.preventDefault();
            capturedKeys = [];
            if (e.ctrlKey) capturedKeys.push('ctrl');
            if (e.shiftKey) capturedKeys.push('shift');
            if (e.altKey) capturedKeys.push('alt');
            if (e.metaKey) capturedKeys.push('cmd');

            const key = this.getKeyName(e.key);
            if (key) capturedKeys.push(key);

            keyDisplay.textContent = capturedKeys.join('+') || '-';
        });

        (dialog.querySelector('#accept') as HTMLButtonElement).addEventListener('click', async () => {
            const keybinding = capturedKeys.join('+');
            await this.runtimeBridge.call('editor.set_keybinding', {
                action,
                keybinding,
            });
            document.body.removeChild(dialog);
            this.loadKeybindings();
        });

        (dialog.querySelector('#cancel') as HTMLButtonElement).addEventListener('click', () => {
            document.body.removeChild(dialog);
        });

        document.body.appendChild(dialog);
        (dialog.querySelector('#key-input') as HTMLElement).focus();
    }

    private getKeyName(key: string): string | null {
        const keyMap: Record<string, string> = {
            'a': 'a', 'b': 'b', 'c': 'c', 'd': 'd', 'e': 'e', 'f': 'f', 'g': 'g', 'h': 'h',
            'i': 'i', 'j': 'j', 'k': 'k', 'l': 'l', 'm': 'm', 'n': 'n', 'o': 'o', 'p': 'p',
            'q': 'q', 'r': 'r', 's': 's', 't': 't', 'u': 'u', 'v': 'v', 'w': 'w', 'x': 'x',
            'y': 'y', 'z': 'z',
            '0': '0', '1': '1', '2': '2', '3': '3', '4': '4', '5': '5', '6': '6', '7': '7',
            '8': '8', '9': '9',
            'Enter': 'enter',
            ' ': 'space',
            'Tab': 'tab',
            'Backspace': 'backspace',
            'Delete': 'delete',
            'Home': 'home',
            'End': 'end',
            'PageUp': 'pageup',
            'PageDown': 'pagedown',
            'ArrowUp': 'up',
            'ArrowDown': 'down',
            'ArrowLeft': 'left',
            'ArrowRight': 'right',
            '[': 'bracketleft',
            ']': 'bracketright',
            '+': 'plus',
            '-': 'minus',
        };

        // Function keys
        if (key.startsWith('F') && key.length <= 3) {
            const num = parseInt(key.slice(1));
            if (num >= 1 && num <= 12) return key.toLowerCase();
        }

        return keyMap[key] || null;
    }
}
```

## IPC Messages

```cpp
// IPC protocol for preferences
struct PreferencesMessages {
    // Get all editor preferences
    struct GetPreferences {
        static constexpr StringView kMethod = "editor.get_preferences";
        // Response: EditorPreferencesUI as JSON
    };

    // Set editor preferences
    struct SetPreferences {
        static constexpr StringView kMethod = "editor.set_preferences";
        nlohmann::json preferences;
    };

    // Get keybindings
    struct GetKeybindings {
        static constexpr StringView kMethod = "editor.get_keybindings";
        // Response: nested object { viewport: {translate: "w", ...}, editor: {...}, ... }
    };

    // Set keybinding for action
    struct SetKeybinding {
        static constexpr StringView kMethod = "editor.set_keybinding";
        std::string action;  // e.g., "translate"
        std::string keybinding;  // e.g., "ctrl+z"
    };

    // Reset keybinding to default
    struct ResetKeybinding {
        static constexpr StringView kMethod = "editor.reset_keybinding";
        std::string action;
    };

    // Reset all keybindings
    struct ResetAllKeybindings {
        static constexpr StringView kMethod = "editor.reset_all_keybindings";
    };
};
```

## Auto-Save System

```cpp
/// Auto-save manager for scene and editor state.
class AutoSaveManager {
public:
    AutoSaveManager(EditorPreferences& prefs, Scene& scene)
        : preferences_(prefs), scene_(scene), last_save_time_(0) {}

    /// Update auto-save timer. Call every frame.
    void Update(float delta_time) {
        if (!preferences_.GetAutoSaveEnabled()) return;

        last_save_time_ += delta_time;

        uint32_t interval = preferences_.GetAutoSaveIntervalSeconds();
        if (last_save_time_ >= interval) {
            PerformAutoSave();
            last_save_time_ = 0;
        }
    }

private:
    void PerformAutoSave() {
        auto autosave_path = fs::path("~/.uzengine/autosave/");
        autosave_path /= fmt::format("autosave_{}.uzscene", GetTimestamp());

        fs::create_directories(autosave_path.parent_path());

        if (scene_.Save(autosave_path)) {
            UZ_LOG(Editor, Info, "Auto-saved scene to: {}", autosave_path);

            // Clean up old autosaves (keep last 10)
            CleanupOldAutosaves(autosave_path.parent_path());
        } else {
            UZ_LOG(Editor, Warning, "Auto-save failed");
        }
    }

    void CleanupOldAutosaves(fs::path autosave_dir) {
        std::vector<fs::path> saves;
        for (const auto& entry : fs::directory_iterator(autosave_dir)) {
            if (entry.path().extension() == ".uzscene") {
                saves.push_back(entry.path());
            }
        }

        // Sort by modification time (newest first)
        std::sort(saves.begin(), saves.end(),
                 [](const auto& a, const auto& b) {
                     return fs::last_write_time(a) > fs::last_write_time(b);
                 });

        // Delete oldest saves beyond limit
        constexpr size_t kMaxAutosaves = 10;
        if (saves.size() > kMaxAutosaves) {
            for (size_t i = kMaxAutosaves; i < saves.size(); ++i) {
                fs::remove(saves[i]);
            }
        }
    }

    EditorPreferences& preferences_;
    Scene& scene_;
    float last_save_time_;

    std::string GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        return fmt::format("{:%Y%m%d_%H%M%S}", fmt::localtime(time));
    }
};
```

## Comparison

| Feature | UE5 | Godot | VSCode | **UzEngine** |
|---|---|---|---|---|
| **Storage** | .ini files + registry | .tres settings | settings.json + extensions | **JSON + project override** |
| **Keybindings** | Keybind Manager UI | Input Map (visual) | Command Palette | **VSCode-compatible format + UI editor** |
| **Viewport settings** | Rich settings menu | Project settings | N/A | **FOV, speed, near/far clip, grid** |
| **Layout persistence** | Window layout | Window layout | Workbench layout | **Per-panel position/size storage** |
| **Auto-save** | Backup system | Not built-in | Not built-in | **Configurable interval, old cleanup** |
| **Theme integration** | Custom UI theme | Theme system | Theme inheritance | **Accent color + text/bg override** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Global + project preferences; Viewport (FOV, speed, clips); Grid settings; Gizmo settings; Basic keybindings (W/E/R/F); Auto-save (60s default); Recent projects list |
| **Phase 3** | Keybindings editor UI; Theme customization (colors); Layout persistence (panel positions); Viewport quality presets |
| **Phase 4** | Settings sync (VSCode extension), Profile system (save/load preference sets), Plugin configuration, Advanced performance tuning |

