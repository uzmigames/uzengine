/// UzEngine Scene Tree panel — TreeDataProvider for the entity hierarchy.
///
/// Queries `addon.getSceneTree()` which returns a JSON string describing the
/// world's entity tree. The tree is refreshed on demand via `refresh()`, which
/// is called after `loadScene()` or whenever the user triggers the
/// `uzengine.refreshSceneTree` command.

import * as vscode from 'vscode';
import type { UzEngineAddon } from '../uzengine';

// ── Internal types ────────────────────────────────────────────────────────────

/** Raw shape returned by `addon.getSceneTree()` (parsed from JSON). */
interface SceneEntity {
    readonly id: number;
    readonly name: string;
    readonly children: SceneEntity[];
}

/** Parsed wrapper around the top-level `{ "entities": [...] }` response. */
interface SceneTreeResponse {
    readonly entities: SceneEntity[];
}

// ── SceneTreeProvider ─────────────────────────────────────────────────────────

/// TreeDataProvider that renders the entity hierarchy in the Scene Hierarchy view.
///
/// Data is fetched from the native addon via `getSceneTree()`. The provider
/// is purely read-only — mutations are routed through inspector commands, not
/// this view.
export class SceneTreeProvider implements vscode.TreeDataProvider<SceneEntity> {
    private readonly _onDidChangeTreeData =
        new vscode.EventEmitter<SceneEntity | undefined>();

    /// Fired whenever the tree needs to refresh in VSCode's UI.
    readonly onDidChangeTreeData: vscode.Event<SceneEntity | undefined> =
        this._onDidChangeTreeData.event;

    /// Top-level entities currently displayed. Empty before the first `refresh()`.
    private entities: SceneEntity[] = [];

    constructor(private readonly addon: UzEngineAddon) {}

    // ── Public API ────────────────────────────────────────────────────────────

    /// Re-queries the native addon for the latest scene tree and fires a full
    /// refresh event so VSCode re-renders the view.
    ///
    /// Safe to call when no viewport/world is active — the addon returns `null`
    /// in that case and the tree is cleared.
    refresh(): void {
        try {
            const json = this.addon.getSceneTree();
            if (json) {
                const parsed = JSON.parse(json) as SceneTreeResponse;
                // The response has either top-level `entities` array or IS the array.
                if (Array.isArray(parsed)) {
                    this.entities = parsed as unknown as SceneEntity[];
                } else if (Array.isArray(parsed.entities)) {
                    this.entities = parsed.entities;
                } else {
                    this.entities = [];
                }
            } else {
                this.entities = [];
            }
        } catch {
            this.entities = [];
        }
        this._onDidChangeTreeData.fire(undefined);
    }

    // ── TreeDataProvider impl ─────────────────────────────────────────────────

    getTreeItem(element: SceneEntity): vscode.TreeItem {
        const label = element.name.length > 0 ? element.name : `Entity ${element.id}`;
        const collapsible =
            element.children.length > 0
                ? vscode.TreeItemCollapsibleState.Expanded
                : vscode.TreeItemCollapsibleState.None;

        const item = new vscode.TreeItem(label, collapsible);
        item.id = String(element.id);
        item.contextValue = 'entity';
        item.tooltip = `ID: ${element.id}`;
        item.description = `#${element.id}`;
        item.iconPath = new vscode.ThemeIcon('symbol-class');
        return item;
    }

    getChildren(element?: SceneEntity): SceneEntity[] {
        if (!element) {
            return this.entities;
        }
        return element.children;
    }
}
