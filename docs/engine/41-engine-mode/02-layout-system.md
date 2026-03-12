# Layout System — Configuration, Profiles, and Panel Positioning

> **Status**: Planned (Phase 2–3)
> **Source**: `editor/layout/layout_manager.ts`, `editor/layout/layout_profiles.ts`
> **References**: VSCode Extension API (ViewColumn, WebviewPanel), Unreal Editor layout system, Godot docking system

## Purpose

The Layout System defines how panels are positioned in Engine Mode and provides preset configurations for different workflows. It maps VSCode's ViewColumn API to game-engine-style slots (left, center, right, bottom) and manages persistence so users' preferred layouts survive IDE restarts.

## Layout Definitions

A layout specifies which panels exist, where they appear, and their relative sizes.

```typescript
interface PanelPosition {
    /// ViewColumn: One (left), Two (center), Three (right), etc.
    viewColumn: vscode.ViewColumn;

    /// Weight for sizing: 1 = small, 2 = medium, 3 = large, etc.
    weight: number;

    /// Whether panel is visible by default in this layout.
    visible: boolean;

    /// Order within the column (0 = top).
    order: number;
}

interface LayoutDefinition {
    /// User-friendly name: "Default Engine", "Minimal", "Wide Viewport", etc.
    name: string;

    /// Short description.
    description: string;

    /// Panels and their positions in this layout.
    panels: Record<string, PanelPosition>;

    /// Whether to preserve AI sidebar in secondary position.
    preserveAISidebar: boolean;

    /// Optional: recommended monitor count (1 or 2).
    monitorCount?: number;
}
```

## Preset Layouts

### Default Engine Layout (Phase 2)
Balanced game-development layout: hierarchy, viewport, inspector, and bottom tabs.

```typescript
const defaultEngineLayout: LayoutDefinition = {
    name: "Default Engine",
    description: "Hierarchy (left), viewport (center), inspector (right), output (bottom)",
    panels: {
        "hierarchy": { viewColumn: vscode.ViewColumn.One, weight: 1, visible: true, order: 0 },
        "inspector": { viewColumn: vscode.ViewColumn.Three, weight: 1, visible: true, order: 0 },
        "viewport": { viewColumn: vscode.ViewColumn.Two, weight: 3, visible: true, order: 0 },
        "output": { viewColumn: vscode.ViewColumn.Two, weight: 1, visible: true, order: 1 },
        "console": { viewColumn: vscode.ViewColumn.Two, weight: 0.5, visible: false, order: 2 },
        "assetBrowser": { viewColumn: vscode.ViewColumn.Two, weight: 0.5, visible: false, order: 3 },
    },
    preserveAISidebar: true,
};
```

**Panel arrangement**:
- **Left (ViewColumn.One)**: Hierarchy panel only
- **Center (ViewColumn.Two)**: Viewport (large), Output below (small tabs: console, asset browser)
- **Right (ViewColumn.Three)**: Inspector panel only
- **Secondary sidebar**: AI assistant (if present)

### Minimal Layout (Phase 3)
Maximizes viewport for scene viewing. Inspector hidden, hierarchy minimized.

```typescript
const minimalLayout: LayoutDefinition = {
    name: "Minimal",
    description: "Viewport dominant, hierarchy collapsed, inspector hidden",
    panels: {
        "hierarchy": { viewColumn: vscode.ViewColumn.One, weight: 0.3, visible: true, order: 0 },
        "viewport": { viewColumn: vscode.ViewColumn.Two, weight: 4, visible: true, order: 0 },
        "inspector": { viewColumn: vscode.ViewColumn.Three, weight: 0, visible: false, order: 0 },
        "output": { viewColumn: vscode.ViewColumn.Two, weight: 0.5, visible: true, order: 1 },
    },
    preserveAISidebar: true,
};
```

### Wide Viewport Layout (Phase 3)
Viewport spans full height, inspector stacked below in same column.

```typescript
const wideViewportLayout: LayoutDefinition = {
    name: "Wide Viewport",
    description: "Viewport + inspector stacked, hierarchy on left",
    panels: {
        "hierarchy": { viewColumn: vscode.ViewColumn.One, weight: 1, visible: true, order: 0 },
        "viewport": { viewColumn: vscode.ViewColumn.Two, weight: 3, visible: true, order: 0 },
        "inspector": { viewColumn: vscode.ViewColumn.Two, weight: 1, visible: true, order: 1 },
        "output": { viewColumn: vscode.ViewColumn.Two, weight: 0.5, visible: true, order: 2 },
    },
    preserveAISidebar: true,
};
```

### Dual Monitor Layout (Phase 3)
For two monitors: game on primary, inspector/output on secondary.

```typescript
const dualMonitorLayout: LayoutDefinition = {
    name: "Dual Monitor",
    description: "Viewport on primary monitor, hierarchy + inspector on secondary",
    panels: {
        "hierarchy": { viewColumn: vscode.ViewColumn.Three, weight: 1, visible: true, order: 0 },
        "viewport": { viewColumn: vscode.ViewColumn.One, weight: 5, visible: true, order: 0 },
        "inspector": { viewColumn: vscode.ViewColumn.Three, weight: 1, visible: true, order: 1 },
        "output": { viewColumn: vscode.ViewColumn.Three, weight: 0.5, visible: true, order: 2 },
    },
    preserveAISidebar: true,
    monitorCount: 2,
};
```

## Layout Manager

Orchestrates layout switching and persistence.

```typescript
class LayoutManager {
    private currentLayout: LayoutDefinition;
    private layouts: Map<string, LayoutDefinition>;
    private panelRegistry: Map<string, vscode.WebviewPanel>;
    private layoutCache: Map<string, any>;

    constructor() {
        this.layouts = new Map();
        this.panelRegistry = new Map();
        this.layoutCache = new Map();

        // Register preset layouts
        this.registerLayout(defaultEngineLayout);
        this.registerLayout(minimalLayout);
        this.registerLayout(wideViewportLayout);
        this.registerLayout(dualMonitorLayout);
    }

    /// Register a new layout definition.
    registerLayout(layout: LayoutDefinition): void {
        this.layouts.set(layout.name, layout);
    }

    /// Switch to a named layout.
    async switchToLayout(layoutName: string): Promise<boolean> {
        const layout = this.layouts.get(layoutName);
        if (!layout) {
            vscode.window.showErrorMessage(`Layout '${layoutName}' not found`);
            return false;
        }

        // Save current panel state (scroll, selection, etc.)
        await this.savePanelState();

        // Hide all panels, reorganize
        for (const [panelId, position] of Object.entries(layout.panels)) {
            const panel = this.panelRegistry.get(panelId);
            if (!panel) {
                // Create panel if it doesn't exist
                await this.createPanel(panelId);
            } else {
                // Move to new ViewColumn
                panel.reveal(position.viewColumn, false);
            }

            // Update visibility
            // (VSCode doesn't have built-in hide; instead we minimize or close)
            if (!position.visible && panel) {
                panel.dispose();
                this.panelRegistry.delete(panelId);
            }
        }

        this.currentLayout = layout;
        await this.saveLayoutPreference(layoutName);
        return true;
    }

    /// Get current layout.
    getLayout(): LayoutDefinition {
        return this.currentLayout;
    }

    /// Get all available layouts.
    listLayouts(): LayoutDefinition[] {
        return Array.from(this.layouts.values());
    }

    private async createPanel(panelId: string): Promise<vscode.WebviewPanel> {
        // Implementation: create hierarchy, inspector, asset browser, etc.
        // (See 03-panel-management.md for details)
        throw new Error("Not implemented");
    }

    private async savePanelState(): Promise<void> {
        // Save scroll positions, selected items, expanded nodes, etc.
        // Saved to workspace state (context.workspaceState)
    }

    private async saveLayoutPreference(layoutName: string): Promise<void> {
        // Save to workspace state so preference survives IDE restart
    }
}
```

## ViewColumn Mapping

VSCode's `ViewColumn` enum maps to spatial positions:

| ViewColumn | Position | Use Case |
|---|---|---|
| **One** | Left | Scene hierarchy |
| **Two** | Center | Viewport (main), output tabs below |
| **Three** | Right | Inspector/properties |
| **Four+** | Secondary windows | Not typically used; AI sidebar may take a slot |

**Key insight**: Viewport goes in **ViewColumn.Two** (center) and spans vertically, with output tabs placed in the same column below using webview grouping.

## Layout Persistence

Layouts are persisted in VSCode's `workspaceState` (per-workspace) or `globalState` (global):

```typescript
async function saveLayoutPreference(context: vscode.ExtensionContext, layoutName: string): Promise<void> {
    // Save as global preference
    await context.globalState.update("uz-engine.lastLayout", layoutName);
}

async function restoreLayoutPreference(context: vscode.ExtensionContext): Promise<string> {
    // Restore last used layout, default to "Default Engine"
    return context.globalState.get("uz-engine.lastLayout", "Default Engine");
}
```

On extension activation:
```typescript
async function activate(context: vscode.ExtensionContext) {
    const lastLayout = await restoreLayoutPreference(context);
    const layoutManager = new LayoutManager();
    await layoutManager.switchToLayout(lastLayout);
}
```

## Custom Layouts (Phase 4)

Users can define custom layouts in `.uzproject`:

```toml
# .uzproject (TOML format)
[editor]
layout = "Default Engine"  # or "Minimal", or "Custom"

# Custom layout definition
[editor.layouts.custom]
name = "My Custom Layout"
description = "Viewport on left, inspector on right, no bottom panels"
preserveAISidebar = true

[editor.layouts.custom.panels.hierarchy]
viewColumn = "One"
weight = 0.8
visible = true
order = 0

[editor.layouts.custom.panels.viewport]
viewColumn = "Two"
weight = 4
visible = true
order = 0

[editor.layouts.custom.panels.inspector]
viewColumn = "Three"
weight = 1.2
visible = true
order = 0
```

Parse `.uzproject` and register custom layouts on startup:

```typescript
async function loadProjectLayouts(projectPath: string): Promise<void> {
    const toml = await fs.readFile(path.join(projectPath, ".uzproject"), "utf8");
    const config = parseToml(toml);

    if (config.editor?.layouts) {
        for (const [name, layoutDef] of Object.entries(config.editor.layouts)) {
            const layout: LayoutDefinition = {
                name: layoutDef.name || name,
                description: layoutDef.description || "",
                panels: layoutDef.panels,
                preserveAISidebar: layoutDef.preserveAISidebar ?? true,
            };
            layoutManager.registerLayout(layout);
        }
    }
}
```

## Panel Weight System

The `weight` field controls relative sizing when multiple panels share a ViewColumn.

Example: Viewport + Output in ViewColumn.Two:
- Viewport: weight = 3 (occupies 75% of column height)
- Output: weight = 1 (occupies 25% of column height)

Calculation: `panel_height = (panel_weight / total_weight) * column_height`

## Related Docs

- **[Overview (§41.01)](01-overview.md)** — Architecture, toggle mechanism, AI sidebar preservation
- **[Panel Management (§41.03)](03-panel-management.md)** — Panel lifecycle, lazy creation, state preservation
- **[IDE Compatibility (§41.04)](04-compatibility.md)** — IDE-specific layout handling
