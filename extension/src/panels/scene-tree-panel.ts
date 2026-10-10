/// UzEngine Scene Tree panel — TreeDataProvider for the entity hierarchy.
///
/// Queries `client.getSceneTree()` which returns a JSON string describing the
/// world's entity tree. The tree is refreshed on demand via `refresh()`, which
/// is called after `loadScene()` or whenever the user triggers the
/// `uzengine.refreshSceneTree` command.
///
/// The provider accepts a lazy getter `() => RuntimeClient | undefined` so
/// that the tree view can be registered in VSCode at activation time without
/// forcing the native .node addon to load. The client is only accessed when
/// `refresh()` is actually called (i.e. when a scene is loaded).
///
/// `getSceneTree()` is an async IPC call; `refresh()` fires the tree-change
/// event optimistically (clears the tree immediately) and updates it once the
/// round-trip completes. This keeps the UI responsive.

import * as vscode from 'vscode';
import type { RuntimeClient } from '../runtime-client';

// ── Internal types ────────────────────────────────────────────────────────────

/** Raw shape returned by `client.getSceneTree()` (parsed from JSON). */
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
/// Data is fetched from the `RuntimeClient` via `getSceneTree()`. The provider
/// is purely read-only — mutations are routed through inspector commands, not
/// this view.
///
/// # Client lifetime
/// The constructor accepts a getter `() => RuntimeClient | undefined` rather
/// than a `RuntimeClient` directly. This allows the tree view to be created
/// and registered at extension activation time without forcing the native .node
/// addon to load. The getter is only invoked inside `refresh()`, which runs
/// only when a scene is actually open.
export class SceneTreeProvider implements vscode.TreeDataProvider<SceneEntity> {
  private readonly _onDidChangeTreeData = new vscode.EventEmitter<SceneEntity | undefined>();

  /// Fired whenever the tree needs to refresh in VSCode's UI.
  readonly onDidChangeTreeData: vscode.Event<SceneEntity | undefined> =
    this._onDidChangeTreeData.event;

  /// Top-level entities currently displayed. Empty before the first `refresh()`.
  private entities: SceneEntity[] = [];

  /// Lazy getter for the RuntimeClient. Only invoked when a scene is open.
  private readonly getClient: () => RuntimeClient | undefined;

  constructor(clientGetter: () => RuntimeClient | undefined) {
    this.getClient = clientGetter;
  }

  // ── Public API ────────────────────────────────────────────────────────────

  /// Re-queries the RuntimeClient for the latest scene tree and fires a full
  /// refresh event so VSCode re-renders the view.
  ///
  /// Safe to call when no viewport/world is active — returns immediately if
  /// the client has not yet been created, clearing the tree instead.
  ///
  /// The method is async but does not need to be awaited — the tree view will
  /// update automatically when the data arrives via the EventEmitter.
  refresh(): void {
    const client = this.getClient();
    if (!client) {
      this.entities = [];
      this._onDidChangeTreeData.fire(undefined);
      return;
    }

    // Fire immediately with the stale data so the view doesn't flash blank,
    // then update asynchronously once the IPC round-trip completes.
    client
      .getSceneTree()
      .then((json) => {
        if (json) {
          try {
            const parsed = JSON.parse(json) as SceneTreeResponse;
            if (Array.isArray(parsed)) {
              this.entities = parsed as unknown as SceneEntity[];
            } else if (Array.isArray(parsed.entities)) {
              this.entities = parsed.entities;
            } else {
              this.entities = [];
            }
          } catch {
            this.entities = [];
          }
        } else {
          this.entities = [];
        }
        this._onDidChangeTreeData.fire(undefined);
      })
      .catch(() => {
        this.entities = [];
        this._onDidChangeTreeData.fire(undefined);
      });
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
