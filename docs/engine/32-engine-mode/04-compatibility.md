# IDE Compatibility — VSCode, Cursor, Windsurf, and Forks

> **Status**: Planned (Phase 2–3)
> **Source**: `editor/compat/`, `editor/extension.ts`
> **References**: VSCode Extension API stability guarantees, IDE detection heuristics, capability detection patterns

## Purpose

Engine Mode must work across VSCode and its derivatives (Cursor, Windsurf) while preserving AI chat panels and gracefully degrading when APIs are unavailable. The Compatibility layer detects the host IDE and applies appropriate behavior.

## Target IDEs

| IDE | Base Version | Built-in AI | Proprietary Extensions |
|---|---|---|---|
| **VSCode** | Latest (stable + insiders) | Copilot Chat (extension) | None (100% standard API) |
| **Cursor** | VSCode 1.85+ fork | Built-in Cursor AI | None (uses standard API + custom sidebar) |
| **Windsurf** | VSCode 1.85+ fork | Built-in Cascade | None (uses standard API + custom sidebar) |
| **VSCode Forks** | Various | Varies | Varies; detect dynamically |

**Key principle**: **ONLY use `vscode.*` namespace APIs**. Never use proprietary APIs from Cursor, Windsurf, or other IDEs.

## IDE Detection

Detect the host IDE at startup:

```typescript
class IDEDetector {
    /// Detect which IDE is running.
    static detectIDE(): IDEType {
        const appName = vscode.env.appName.toLowerCase();
        const appPath = vscode.env.appRoot.toLowerCase();

        if (appName.includes("cursor")) {
            return IDEType.Cursor;
        } else if (appName.includes("windsurf") || appName.includes("codeium")) {
            return IDEType.Windsurf;
        } else if (appName.includes("vscode")) {
            return IDEType.VSCode;
        } else {
            // Generic VSCode fork
            return IDEType.Generic;
        }
    }

    /// Get human-readable IDE name.
    static getIDEName(ide: IDEType): string {
        const names: Record<IDEType, string> = {
            [IDEType.VSCode]: "Visual Studio Code",
            [IDEType.Cursor]: "Cursor",
            [IDEType.Windsurf]: "Windsurf",
            [IDEType.Generic]: "VSCode-based IDE",
        };
        return names[ide];
    }

    /// Detect IDE version.
    static getIDEVersion(): string {
        return vscode.version;  // e.g., "1.95.0"
    }
}

enum IDEType {
    VSCode,
    Cursor,
    Windsurf,
    Generic,
}
```

## API Compatibility Matrix

| Feature | VSCode | Cursor | Windsurf | Fallback |
|---|---|---|---|---|
| **ViewColumn** | ✓ | ✓ | ✓ | Standard |
| **WebviewPanel** | ✓ | ✓ | ✓ | Standard |
| **Extension API** | ✓ | ✓ | ✓ | Standard |
| **Status bar** | ✓ | ✓ | ✓ | Standard |
| **Keybindings** | ✓ | ✓ (custom) | ✓ (custom) | Standard |
| **Extension Registry** | ✓ | ✓ (filtered) | ✓ (filtered) | Limited |
| **Secondary sidebar** | ✓ | ✓ | ✓ | Standard |

**Notes**:
- Keybindings in Cursor/Windsurf may conflict with IDE defaults
- Extension registry in Cursor/Windsurf may not show all VS Code Marketplace extensions
- Secondary sidebar detection works the same across all IDEs

## AI Assistant Detection

Different IDEs host AI in different ways. Engine Mode must detect and preserve them.

### VSCode: Copilot Chat Extension

Copilot Chat is a standard VSCode extension with a known ID:

```typescript
class AIAssistantDetector {
    static async detectAIAssistant(): Promise<AIAssistant | null> {
        // Check for known extension-based AI
        const extensionIds = [
            "github.copilot-chat",      // GitHub Copilot Chat
            "anthropics.claude-code",   // Claude Code
            "continue.continue",        // Continue (open source)
        ];

        for (const id of extensionIds) {
            const ext = vscode.extensions.getExtension(id);
            if (ext) {
                return {
                    type: "extension",
                    extensionId: id,
                    name: ext.packageJSON.displayName || id,
                    isActive: ext.isActive,
                };
            }
        }

        // Check for built-in AI (Cursor, Windsurf)
        const ide = IDEDetector.detectIDE();
        if (ide === IDEType.Cursor) {
            return {
                type: "builtin",
                name: "Cursor AI",
                isActive: true,
            };
        } else if (ide === IDEType.Windsurf) {
            return {
                type: "builtin",
                name: "Cascade",
                isActive: true,
            };
        }

        return null;
    }
}

interface AIAssistant {
    type: "extension" | "builtin";
    extensionId?: string;
    name: string;
    isActive: boolean;
}
```

### Sidebar Detection

Both extension-based and built-in AI use the secondary sidebar. Detect it:

```typescript
/// Detect if a secondary sidebar (AI panel) is visible.
function isSecondarySidebarVisible(): boolean {
    // Secondary sidebar is typically rightmost
    // VSCode API doesn't directly expose sidebar state, so we use heuristics:

    // 1. Check if any webview has been placed in ViewColumn.Nine (far right)
    // 2. Check for known AI extension visibility state
    // 3. Check IDE app state (imperfect, but useful fallback)

    try {
        // If we can inspect the webview container, secondary sidebar is visible
        // This is implementation-dependent
        return true;  // Conservative: assume it's visible
    } catch {
        return false;
    }
}

async function preserveSecondarySidebar(): Promise<void> {
    // Before switching to Engine Mode, save secondary sidebar state
    const wasVisible = isSecondarySidebarVisible();

    // Switch layout (which might move panels around)
    // ...

    // After layout switch, restore secondary sidebar if it was visible
    if (wasVisible) {
        // Re-activate or restore the AI assistant view
        // (This is IDE-dependent; may not be possible to force activation)
        vscode.window.showInformationMessage(
            "AI assistant sidebar is available. You can toggle it with your IDE's keybinding."
        );
    }
}
```

## Feature Detection

Gracefully degrade when APIs are unavailable:

```typescript
class FeatureDetector {
    /// Check if WebviewPanel API supports retainContextWhenHidden (Phase 2 API).
    static supportsRetainContext(): boolean {
        try {
            return "retainContextWhenHidden" in vscode.window.createWebviewPanel.toString();
        } catch {
            return false;
        }
    }

    /// Check if viewColumn.Eight or beyond are supported (multi-monitor).
    static supportsMultiMonitorLayout(): boolean {
        // VSCode extended ViewColumns: One(1), Two(2), Three(3), Eight(8), Nine(9)
        // If host IDE supports Column Eight+, multi-monitor is available
        return vscode.ViewColumn.Eight !== undefined;
    }

    /// Check if extension can inspect other extensions (required for AI detection).
    static canInspectExtensions(): boolean {
        try {
            vscode.extensions.getExtension("github.copilot");  // Safe query
            return true;
        } catch {
            return false;
        }
    }

    /// Check if keyboard event API is available (for Ctrl+Shift+E shortcut).
    static supportsKeyboardShortcuts(): boolean {
        return vscode.commands !== undefined;
    }
}
```

## Testing Matrix

Test Engine Mode on each IDE to ensure compatibility:

| Test | VSCode | Cursor | Windsurf | Notes |
|---|---|---|---|---|
| Toggle Engine Mode | ✓ | ✓ | ✓ | Status bar button works |
| Keyboard shortcut (Ctrl+Shift+E) | ✓ | Check conflicts | Check conflicts | May conflict with IDE defaults |
| Layout switch (panels move) | ✓ | ✓ | ✓ | Webview creation is standard |
| AI sidebar preserved | ✓ | ✓ | ✓ | Detect + restore |
| State persistence (scroll, selection) | ✓ | ✓ | ✓ | Uses workspaceState |
| Viewport rendering | ✓ | ✓ | ✓ | HWND-based, IDE-agnostic |
| Panel drag-drop | ✓ | ? | ? | Test on each IDE |
| Multi-monitor layout | ✓ | ? | ? | Requires ViewColumn.Eight+ |
| Custom keybindings | ✓ | Partial | Partial | IDE may override some keys |
| Hot reload panels | ✓ | ✓ | ✓ | FileWatcher is standard |

## Known Limitations

### Cursor IDE
- Some keybindings may conflict (e.g., Ctrl+K for chat)
- Extension registry is filtered; some extensions unavailable
- Secondary sidebar can't be forcibly hidden/shown via API
- Recommendation: Provide user documentation on keybinding conflicts

### Windsurf IDE
- Similar to Cursor; keybinding conflicts possible
- Custom sidebar behavior may differ
- Recommendation: Test thoroughly; provide IDE-specific docs

### VSCode Insiders
- APIs may be unstable; report issues to VSCode team
- Test on both stable and insiders builds

## Fallback Strategies

When an IDE doesn't support a feature:

```typescript
async function switchToEngineMode() {
    const ide = IDEDetector.detectIDE();

    // Feature 1: Multi-monitor layout
    if (!FeatureDetector.supportsMultiMonitorLayout()) {
        // Fall back to single-monitor layout
        await layoutManager.switchToLayout("Default Engine");
    } else {
        // Use preferred layout (possibly multi-monitor)
        const layout = await restoreLayoutPreference();
        await layoutManager.switchToLayout(layout);
    }

    // Feature 2: Keyboard shortcuts
    if (!FeatureDetector.supportsKeyboardShortcuts()) {
        // Provide button-only toggle (no shortcut)
        showMessage("Engine Mode toggle available via status bar button");
    }

    // Feature 3: AI sidebar preservation
    const aiAssistant = await AIAssistantDetector.detectAIAssistant();
    if (aiAssistant && aiAssistant.type === "builtin") {
        showMessage(
            `${aiAssistant.name} is preserved in the sidebar. ` +
            `Toggle it with your IDE's default keybinding.`
        );
    }
}
```

## IDE-Specific Documentation

Provide per-IDE guidance:

### `docs/guides/engine-mode-cursor.md`
- Cursor-specific keybinding conflicts
- How to remap conflicting shortcuts
- AI sidebar behavior in Cursor

### `docs/guides/engine-mode-windsurf.md`
- Windsurf-specific shortcuts
- Cascade AI preservation
- Known quirks

### `docs/guides/engine-mode-vscode.md`
- Standard VSCode usage
- Copilot Chat integration
- Extension recommendations

## Related Docs

- **[Overview (§41.01)](01-overview.md)** — Architecture and AI sidebar detection
- **[Viewport Integration (§41.05)](05-viewport-integration.md)** — HWND rendering (IDE-agnostic)
- **[Guides](../../../guides/)** — User-facing IDE-specific documentation
