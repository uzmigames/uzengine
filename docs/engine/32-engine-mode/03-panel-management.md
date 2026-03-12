# Panel Management — Lifecycle, State Preservation, and Memory Control

> **Status**: Planned (Phase 2–3)
> **Source**: `editor/panels/panel_*.ts`, `editor/panels/panel_state_manager.ts`
> **References**: VSCode WebviewPanel API, React component lifecycle, state persistence patterns

## Purpose

The Panel Management system orchestrates the creation, visibility, state preservation, and disposal of editor panels (hierarchy, inspector, asset browser, output). It ensures panels efficiently manage memory, preserve user state (scroll position, selection, expanded nodes), and integrate smoothly with the layout switching system.

## Panel Types

Engine Mode consists of five core panels:

| Panel | Purpose | Webview? | Lazy Create? |
|---|---|---|---|
| **Hierarchy** | Scene tree view, entity list, drag-drop reparenting | Yes | Phase 3 |
| **Inspector** | Property grid, component editor, asset picker | Yes | Phase 3 |
| **Asset Browser** | Asset search, thumbnails, drag-drop to viewport | Yes | Yes (Phase 3) |
| **Output** | Engine logs, compilation errors, warnings | Yes | No (always visible) |
| **Viewport** | 3D HWND scene view, camera control, gizmos | HWND (not webview) | No (always visible) |

## Panel Lifecycle

Each panel follows a lifecycle:

```
┌─────────────┬─────────────┬──────────────┬─────────────┐
│   Create    │   Load      │    Interact  │   Dispose   │
├─────────────┼─────────────┼──────────────┼─────────────┤
│ 1. Register │ 2. Load UI  │ 3. User      │ 4. Save     │
│    in panel │    HTML &   │    edits or  │    state &  │
│    registry │    scripts  │    navigates │    dispose  │
│             │             │              │ webview     │
└─────────────┴─────────────┴──────────────┴─────────────┘
```

### Panel State

State persists across mode switches and IDE restarts:

```typescript
interface PanelState {
    /// Which items are currently selected (e.g., entity IDs in hierarchy).
    selectedItems: string[];

    /// Scroll position in the panel (vertical offset in pixels).
    scrollOffset: number;

    /// Which tree nodes are expanded (e.g., entity hierarchy expansions).
    expandedNodes: Set<string>;

    /// Panel width/height if floating (Phase 4).
    size?: { width: number; height: number };

    /// User's focus was in this panel.
    hadFocus: boolean;

    /// Last interaction time (for cleanup heuristics).
    lastUsedTime: number;
}
```

## Hierarchy Panel

Tree view of scene entities with drag-drop reparenting.

```typescript
class HierarchyPanel {
    private panel: vscode.WebviewPanel;
    private state: PanelState;
    private rootEntity: string;  // Usually the scene root

    async create(viewColumn: vscode.ViewColumn): Promise<void> {
        this.panel = vscode.window.createWebviewPanel(
            "uz-hierarchy",
            "Hierarchy",
            viewColumn,
            {
                enableScripts: true,
                enableFindWidget: false,  // Disable built-in search; we have custom
                retainContextWhenHidden: true,  // Keep state when hidden
            }
        );

        // Load UI from dist/hierarchy.html
        this.panel.webview.html = this.getWebviewContent();

        // Restore previous state (selection, expansion, scroll)
        this.panel.webview.postMessage({
            type: "restore_state",
            state: this.state,
        });

        // Listen for entity selection from hierarchy
        this.panel.webview.onDidReceiveMessage((msg) => {
            if (msg.type === "select_entity") {
                // Notify selection context, update inspector
                vscode.commands.executeCommand("uz-engine.selectEntity", msg.entityId);
            } else if (msg.type === "reparent_entity") {
                // Drag-drop reparenting
                vscode.commands.executeCommand("uz-engine.reparentEntity",
                    msg.entityId, msg.newParentId);
            }
        });

        // Save state before disposal
        this.panel.onDidDispose(() => {
            this.saveState();
        });
    }

    async saveState(): Promise<void> {
        // Ask webview for current scroll, selection, expansion
        const state = await this.panel.webview.postMessage({
            type: "get_state",
        });
        // Save to workspace state
    }

    private getWebviewContent(): string {
        const scriptUri = this.panel.webview.asWebviewUri(
            vscode.Uri.file(path.join(this.context.extensionPath, "dist/hierarchy.js"))
        );
        return `
            <!DOCTYPE html>
            <html>
            <head>
                <meta charset="UTF-8">
                <meta name="viewport" content="width=device-width, initial-scale=1.0">
                <link rel="stylesheet" href="${scriptUri.replace('.js', '.css')}">
            </head>
            <body>
                <div id="root"></div>
                <script src="${scriptUri}"></script>
            </body>
            </html>
        `;
    }
}
```

## Inspector Panel

Property grid for the selected entity/component.

```typescript
class InspectorPanel {
    private panel: vscode.WebviewPanel;
    private state: PanelState;
    private selectedEntity: string | null;

    async create(viewColumn: vscode.ViewColumn): Promise<void> {
        this.panel = vscode.window.createWebviewPanel(
            "uz-inspector",
            "Inspector",
            viewColumn,
            { enableScripts: true, retainContextWhenHidden: true }
        );

        this.panel.webview.html = this.getWebviewContent();

        // Listen for property changes
        this.panel.webview.onDidReceiveMessage((msg) => {
            if (msg.type === "property_changed") {
                vscode.commands.executeCommand("uz-engine.setProperty",
                    this.selectedEntity, msg.component, msg.property, msg.value);
            }
        });
    }

    /// Update inspector to show properties for an entity.
    async showEntity(entityId: string): Promise<void> {
        this.selectedEntity = entityId;

        // Query entity properties via IPC (see §28-ipc-protocol)
        const props = await vscode.commands.executeCommand(
            "uz-engine.getEntityProperties", entityId
        );

        // Send to webview for rendering
        this.panel.webview.postMessage({
            type: "show_entity",
            entityId: entityId,
            properties: props,
        });
    }

    private getWebviewContent(): string {
        // Similar to HierarchyPanel
    }
}
```

## Asset Browser Panel

Searchable asset grid with thumbnails and drag-drop support.

```typescript
class AssetBrowserPanel {
    private panel: vscode.WebviewPanel;
    private state: PanelState;
    private searchQuery: string = "";
    private viewMode: "grid" | "list" = "grid";

    async create(viewColumn: vscode.ViewColumn): Promise<void> {
        this.panel = vscode.window.createWebviewPanel(
            "uz-asset-browser",
            "Asset Browser",
            viewColumn,
            { enableScripts: true, retainContextWhenHidden: true }
        );

        this.panel.webview.html = this.getWebviewContent();

        // Handle search input
        this.panel.webview.onDidReceiveMessage((msg) => {
            if (msg.type === "search") {
                this.searchQuery = msg.query;
                this.refreshAssets();
            } else if (msg.type === "drag_start") {
                // Notify viewport to expect drop
                vscode.commands.executeCommand(
                    "uz-engine.dragAsset", msg.assetPath
                );
            }
        });
    }

    private async refreshAssets(): Promise<void> {
        // Query assets matching search query
        const assets = await vscode.commands.executeCommand(
            "uz-engine.searchAssets", this.searchQuery
        );

        this.panel.webview.postMessage({
            type: "assets",
            assets: assets,
        });
    }

    private getWebviewContent(): string {
        // Grid or list of assets with thumbnails
    }
}
```

## Output Panel

Engine logs, warnings, and errors (similar to VSCode's Output panel).

```typescript
class OutputPanel {
    private panel: vscode.WebviewPanel;
    private logBuffer: string[] = [];
    private maxBufferSize: number = 10000;  // Lines

    async create(viewColumn: vscode.ViewColumn): Promise<void> {
        this.panel = vscode.window.createWebviewPanel(
            "uz-output",
            "Output",
            viewColumn,
            { enableScripts: true, retainContextWhenHidden: true }
        );

        this.panel.webview.html = this.getWebviewContent();

        // Listen to engine IPC for log messages
        this.setupLogListener();
    }

    /// Append a log line (called by engine via IPC).
    appendLog(message: string, level: "info" | "warn" | "error"): void {
        this.logBuffer.push(`[${level.toUpperCase()}] ${message}`);

        // Trim buffer if too large
        if (this.logBuffer.length > this.maxBufferSize) {
            this.logBuffer.shift();
        }

        // Send to webview to append (avoid re-rendering entire buffer)
        this.panel.webview.postMessage({
            type: "append_log",
            message: message,
            level: level,
        });
    }

    /// Clear output.
    clear(): void {
        this.logBuffer = [];
        this.panel.webview.postMessage({ type: "clear" });
    }

    private setupLogListener(): void {
        // Engine sends logs via IPC
        // This would be handled by the IPC protocol manager
    }

    private getWebviewContent(): string {
        // Scrollable log view with syntax highlighting for error messages
    }
}
```

## Panel State Manager

Centralized state persistence.

```typescript
class PanelStateManager {
    private stateCache: Map<string, PanelState> = new Map();

    /// Save state for a panel to workspace state.
    async saveState(panelId: string, state: PanelState): Promise<void> {
        this.stateCache.set(panelId, state);

        // Persist to VSCode workspace state (survives IDE restart)
        await vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        const stateFile = path.join(projectRoot, ".vscode/uz-engine-panels.json");
        await fs.writeFile(stateFile, JSON.stringify(
            Object.fromEntries(this.stateCache),
            null,
            2
        ));
    }

    /// Load state for a panel.
    async loadState(panelId: string): Promise<PanelState | null> {
        const cached = this.stateCache.get(panelId);
        if (cached) return cached;

        // Load from disk
        const stateFile = path.join(projectRoot, ".vscode/uz-engine-panels.json");
        try {
            const data = JSON.parse(await fs.readFile(stateFile, "utf8"));
            const state = data[panelId];
            this.stateCache.set(panelId, state);
            return state;
        } catch {
            return null;
        }
    }

    /// Clear state for a panel (e.g., user closed it explicitly).
    async clearState(panelId: string): Promise<void> {
        this.stateCache.delete(panelId);
    }
}
```

## Lazy Panel Creation (Phase 3)

Asset Browser and other heavy panels should be created on-demand:

```typescript
class PanelRegistry {
    private panels: Map<string, vscode.WebviewPanel> = new Map();
    private panelClasses: Map<string, any> = new Map();

    constructor() {
        // Register panel classes (not instances)
        this.panelClasses.set("hierarchy", HierarchyPanel);
        this.panelClasses.set("inspector", InspectorPanel);
        this.panelClasses.set("assetBrowser", AssetBrowserPanel);
        this.panelClasses.set("output", OutputPanel);
    }

    /// Get or create a panel.
    async getPanel(panelId: string): Promise<vscode.WebviewPanel | null> {
        if (!this.panels.has(panelId)) {
            // Lazy create
            const PanelClass = this.panelClasses.get(panelId);
            if (PanelClass) {
                const instance = new PanelClass();
                await instance.create(vscode.ViewColumn.Two);  // or appropriate column
                this.panels.set(panelId, instance.panel);
            }
        }

        return this.panels.get(panelId) || null;
    }

    /// Dispose a panel (called when mode switches or IDE closes).
    async disposePanel(panelId: string): Promise<void> {
        const panel = this.panels.get(panelId);
        if (panel) {
            panel.dispose();
            this.panels.delete(panelId);
        }
    }
}
```

## Memory Management

Panels can consume significant memory if left open. Strategy:

1. **Lazy creation**: Don't create heavy panels (Asset Browser) until needed
2. **Retention context**: Use `retainContextWhenHidden: true` to preserve state when panel is hidden
3. **Cleanup on disposal**: Save state and release resources
4. **LRU cleanup**: If many panels are open, evict least-recently-used (Phase 4)

```typescript
class PanelMemoryManager {
    private usageHistory: Map<string, number> = new Map();  // panelId -> timestamp

    /// Record when a panel was last used.
    recordUsage(panelId: string): void {
        this.usageHistory.set(panelId, Date.now());
    }

    /// If memory is tight, dispose least-recently-used panel.
    async evictLRU(registry: PanelRegistry): Promise<void> {
        const memoryMB = process.memoryUsage().heapUsed / 1024 / 1024;
        if (memoryMB > 200) {  // Heuristic: >200MB of heap
            // Find least-recently-used panel
            let lruPanel = "";
            let oldestTime = Infinity;

            for (const [panelId, time] of this.usageHistory) {
                if (time < oldestTime) {
                    oldestTime = time;
                    lruPanel = panelId;
                }
            }

            if (lruPanel) {
                await registry.disposePanel(lruPanel);
                this.usageHistory.delete(lruPanel);
            }
        }
    }
}
```

## Related Docs

- **[Overview (§41.01)](01-overview.md)** — Architecture and toggle mechanism
- **[Layout System (§41.02)](02-layout-system.md)** — How panels are positioned and switched
- **[IDE Compatibility (§41.04)](04-compatibility.md)** — Panel behavior across IDEs
