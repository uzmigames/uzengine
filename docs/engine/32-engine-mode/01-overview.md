# Engine Mode — VSCode IDE Transformation for Game Development

> **Status**: Planned (Phase 2–3)
> **Source**: `editor/layout/engine_mode.ts`, `editor/extension.ts` (VSCode extension code)
> **References**: UE5 Editor layout and viewport management, Godot Editor docking system, Unreal's customizable layout persistence

## Purpose

Engine Mode transforms VSCode into a game engine IDE with a single click, creating an immersive development environment while preserving the AI assistant chat panel (Claude Code, Cursor, Windsurf, Copilot Chat, etc.). Users toggle between **Code Mode** (standard VSCode) and **Engine Mode** (game-development optimized layout) seamlessly. The system detects and preserves AI extension sidebars across different IDEs, ensuring uninterrupted access to AI tools during development.

## Two Modes

### Code Mode
Standard VSCode layout with:
- File explorer on the left
- Code editor in the center
- AI chat panel visible (usually right sidebar or secondary column)
- Optimized for scripting and general coding

### Engine Mode
Game-development optimized layout with:
- Scene hierarchy panel (left)
- 3D viewport (center, maximized)
- Inspector/properties panel (right)
- Output log, console, asset browser (bottom tabs)
- AI chat panel preserved in secondary sidebar (critical)
- Keyboard shortcuts optimized for game dev (`G` for gizmo, `W`/`E`/`R` for translate/rotate/scale)

## Architecture

```
VSCode Extension (TypeScript)
    │
    ├── editor/layout/                          — Layout management
    │   ├── engine_mode.ts                      — Toggle between modes, save/restore layout
    │   ├── layout_manager.ts                   — Layout definition, panel grouping
    │   ├── ai_sidebar_detector.ts              — Detect AI extensions (Claude Code, Cursor, etc.)
    │   └── viewcolumn_manager.ts               — Map VSCode ViewColumns to panel slots
    │
    ├── editor/panels/                          — Panel lifecycle & state
    │   ├── hierarchy_panel.ts                  — Scene tree webview
    │   ├── inspector_panel.ts                  — Properties webview
    │   ├── asset_browser_panel.ts              — Asset search webview
    │   ├── output_panel.ts                     — Output/log webview
    │   └── panel_state_manager.ts              — Save/restore scroll, selection, expansion
    │
    └── editor/preferences/
        ├── layout_profiles.ts                  — Per-project or global layout configs
        └── keybindings_engine_mode.ts          — Game-dev keybindings (override VSCode defaults)

Runtime (C++ .node addon)
    │
    └── editor/viewport/
        ├── viewport_client.h/cpp               — HWND window creation, input polling
        └── viewport_embedding.h/cpp            — Integration with webview container
```

## Layout Overview

Engine Mode arranges panels in a game-engine aesthetic:

```
┌─────────────────────────────────────────────────────────────┐
│  VSCode Window (with Engine Mode Layout)                    │
├─────────────────────────────────────────────────────────────┤
│ │           │                        │                 │   │
│ │ Hierarchy │                        │  Inspector       │ A │
│ │ Tree      │                        │  Properties      │ I │
│ │           │    3D VIEWPORT         │                 │ S │
│ │ ─────────┤    (HWND Embedded)     │  ────────────   │ I │
│ │ • Spawn  │                        │ • Position      │ S │
│ │ • Camera │                        │ • Rotation      │ T │
│ │ • Light  │                        │ • Scale         │ A │
│ │           │                        │ • Components    │ N │
├─────────────────────────────────────────────────────────────┤
│   Output    │  Console   │  Asset Browser   │  Source    │
├─────────────────────────────────────────────────────────────┤
```

Key layout characteristics:
- **Left panel**: Scene hierarchy (webview)
- **Center**: 3D viewport (HWND, full height, expands horizontally)
- **Right panel**: Inspector (webview)
- **Bottom tabs**: Output, console, asset browser (webviews)
- **Secondary sidebar** (right-most, optional): AI assistant (preserved if present)

## IDE Compatibility

Engine Mode is designed to work across VSCode and its derivatives. The extension detects AI assistant extensions and preserves their visibility.

### Supported IDEs

| IDE | AI Assistant | Detection Method | Note |
|---|---|---|---|
| **VSCode** | Copilot Chat, Claude Code (extension) | Extension ID lookup | Standard VSCode API |
| **Cursor** | Built-in Cursor AI | Secondary sidebar detection | No extension ID; uses sidebar API |
| **Windsurf** | Built-in Cascade | Secondary sidebar detection | No extension ID; uses sidebar API |
| **VSCode Fork** | Varies | Generic detection | Uses vscode.* API only |

### API Surface

Engine Mode uses **only** the standard VSCode Extension API:
- `vscode.commands.*` — execute commands
- `vscode.window.createWebviewPanel()` — create panels
- `vscode.window.visibleTextEditors` — get open editors
- `vscode.ViewColumn` — position panels (One, Two, Three, etc.)
- `vscode.workspace.onDidChangeConfiguration` — react to settings changes
- `context.subscriptions` — register/cleanup event listeners

**Never uses**:
- ❌ Proprietary Cursor APIs
- ❌ Proprietary Windsurf APIs
- ❌ Electron APIs (browser cannot access)

## Toggle Mechanism

### Status Bar Button
Add a button to VSCode's status bar:

```typescript
// In extension.ts:
const engineModeButton = vscode.window.createStatusBarItem(
    vscode.StatusBarAlignment.Left,
    100
);
engineModeButton.text = "$(game) Code Mode";
engineModeButton.command = "uz-engine.toggleEngineMode";
engineModeButton.tooltip = "Toggle between Code Mode and Engine Mode (Ctrl+Shift+E)";
engineModeButton.show();
```

Status bar text changes:
- Code Mode: `$(game) Code Mode` (icon + label)
- Engine Mode: `$(game) Engine Mode` (icon changes or label updates)

### Keyboard Shortcut
Default: `Ctrl+Shift+E` (configurable in settings)

```json
// In keybindings.json:
{
    "key": "ctrl+shift+e",
    "command": "uz-engine.toggleEngineMode",
    "when": "editorFocus || !editorFocus"
}
```

### Command Palette
Users can invoke: `>UzEngine: Toggle Engine Mode`

## AI Sidebar Preservation

The critical feature: Engine Mode must not hide the user's AI assistant. Different IDEs handle AI differently.

### Detection Strategy

```typescript
interface AISidebar {
    extensionId?: string;          // e.g., "anthropics.claude-code"
    isBuiltin: boolean;            // Cursor/Windsurf have built-in AI
    viewId?: string;               // VSCode extension's view ID
    wasVisible: boolean;           // Was it visible before mode switch?
    position: "primary" | "secondary";  // Which sidebar position
}

async function detectAISidebar(): Promise<AISidebar | null> {
    // 1. Check for extension-based AI (Claude Code, Copilot Chat)
    const extensionIds = [
        "anthropics.claude-code",
        "github.copilot-chat"
    ];

    for (const id of extensionIds) {
        const ext = vscode.extensions.getExtension(id);
        if (ext) {
            return {
                extensionId: id,
                isBuiltin: false,
                wasVisible: /* check visibility */,
                position: "secondary"
            };
        }
    }

    // 2. Check for built-in Cursor/Windsurf AI
    if (isCursorIDE() || isWindsurfIDE()) {
        return {
            isBuiltin: true,
            wasVisible: /* sidebar visible */,
            position: "secondary"
        };
    }

    return null;
}

function isCursorIDE(): boolean {
    const appName = vscode.env.appName;
    return appName.toLowerCase().includes("cursor");
}

function isWindsurfIDE(): boolean {
    const appName = vscode.env.appName;
    return appName.toLowerCase().includes("windsurf");
}
```

### Layout Preservation

When toggling modes, save and restore the AI sidebar state:

```typescript
async function switchToEngineMode() {
    // 1. Detect and remember AI sidebar state
    const aiSidebar = await detectAISidebar();
    const aiWasVisible = aiSidebar ? aiSidebar.wasVisible : false;

    // 2. Reorganize panels into engine layout
    // (see 02-layout-system.md for details)

    // 3. After layout switch, restore AI sidebar if it was visible
    if (aiWasVisible && aiSidebar) {
        await restoreAISidebar(aiSidebar);
    }
}

async function switchToCodeMode() {
    // 1. Save current panel states (selection, scroll, etc.)
    // 2. Restore original Code Mode layout
    // 3. Re-show AI sidebar if it was visible before
}
```

## Phase Roadmap

| Phase | Features |
|-------|----------|
| **Phase 2** | Toggle button, basic layout switching (hierarchy + viewport + inspector), AI sidebar detection, layout persistence (save/restore on reopen) |
| **Phase 3** | Multi-monitor support, preset layouts (Default, Minimal, Wide Viewport), floating panels, panel state preservation (scroll, selection, expansion), keyboard shortcuts optimization |
| **Phase 4** | Custom layout definitions via `.uzproject`, per-monitor configuration, layout templates, undo/redo for layout changes |

## Related Docs

- **[Layout System (§41.02)](02-layout-system.md)** — Predefined layouts, ViewColumn mapping, profile definitions
- **[Panel Management (§41.03)](03-panel-management.md)** — Panel lifecycle, state preservation, memory management
- **[IDE Compatibility (§41.04)](04-compatibility.md)** — Target IDE support matrix, feature detection, testing strategy
- **[Viewport Integration (§41.05)](05-viewport-integration.md)** — HWND embedding, input routing, toolbar, split views
