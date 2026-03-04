/**
 * scene-tree-provider.ts — VSCode TreeDataProvider for the UzEngine scene hierarchy.
 *
 * Implements `vscode.TreeDataProvider<NodeItem>` to populate the
 * `uzengine.sceneTree` view with the live scene node hierarchy fetched via IPC.
 *
 * Features:
 *  - Fetches the root node list via `scene.getTree` IPC on activation
 *  - Subscribes to `scene.treeChanged` IPC push events to refresh the tree
 *  - Maps engine NodeType → VSCode ThemeIcon
 *  - Supports context menu actions: Add Child, Duplicate, Delete, Rename
 *  - Fires `onNodeSelected` event when a node is clicked (for Inspector)
 */

import * as vscode from 'vscode';
import { IpcClient } from './ipc-client';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Node types recognised by the engine. */
export type NodeType =
    | 'Node3D'
    | 'StaticMesh'
    | 'DirectionalLight'
    | 'PointLight'
    | 'Camera'
    | 'AudioSource'
    | 'Unknown';

/** Raw node data returned by the `scene.getTree` IPC call. */
export interface SceneNodeData {
    readonly id: string;
    readonly name: string;
    readonly type: NodeType;
    readonly children?: SceneNodeData[];
}

/** Response shape from `scene.getTree`. */
interface GetTreeResponse {
    readonly nodes: SceneNodeData[];
}

// ---------------------------------------------------------------------------
// NodeItem
// ---------------------------------------------------------------------------

/**
 * A VSCode tree item representing a single engine scene node.
 *
 * The item carries the full node data so the Inspector can read it
 * without additional IPC calls.
 */
export class NodeItem extends vscode.TreeItem {
    /** Engine-assigned node ID. */
    public readonly nodeId: string;

    /** Human-readable node name. */
    public readonly nodeName: string;

    /** Engine node type (determines icon). */
    public readonly nodeType: NodeType;

    /** Child nodes — populated when the parent is expanded. */
    public readonly children: SceneNodeData[];

    constructor(data: SceneNodeData) {
        // Nodes with children are collapsible; leaf nodes are not.
        const collapsibleState =
            data.children && data.children.length > 0
                ? vscode.TreeItemCollapsibleState.Collapsed
                : vscode.TreeItemCollapsibleState.None;

        super(data.name, collapsibleState);

        this.nodeId = data.id;
        this.nodeName = data.name;
        this.nodeType = data.type;
        this.children = data.children ?? [];

        // Context value drives the right-click menu `when` conditions in package.json.
        this.contextValue = 'sceneNode';

        // Tooltip shows type and ID for debugging.
        this.tooltip = `${data.type} [${data.id}]`;
        this.description = data.type;

        // Icon based on node type.
        this.iconPath = NodeItem.iconForType(data.type);

        // Command that fires when the user clicks the item.
        this.command = {
            command: 'uzengine.selectNode',
            title: 'Select Node',
            arguments: [this],
        };
    }

    /**
     * Returns the appropriate VSCode ThemeIcon for a given NodeType.
     *
     * Icons are chosen from the built-in product icon set to avoid bundling
     * custom SVGs for Phase 1.
     */
    static iconForType(type: NodeType): vscode.ThemeIcon {
        switch (type) {
            case 'Node3D':         return new vscode.ThemeIcon('symbol-namespace');
            case 'StaticMesh':     return new vscode.ThemeIcon('symbol-field');
            case 'DirectionalLight': return new vscode.ThemeIcon('symbol-event');
            case 'PointLight':     return new vscode.ThemeIcon('symbol-event');
            case 'Camera':         return new vscode.ThemeIcon('device-camera');
            case 'AudioSource':    return new vscode.ThemeIcon('unmute');
            default:               return new vscode.ThemeIcon('symbol-misc');
        }
    }
}

// ---------------------------------------------------------------------------
// SceneTreeProvider
// ---------------------------------------------------------------------------

/**
 * Provides the scene node hierarchy to the `uzengine.sceneTree` TreeView.
 *
 * Fetches the tree via `scene.getTree` IPC and listens for `scene.treeChanged`
 * push events to auto-refresh.
 */
export class SceneTreeProvider
    implements vscode.TreeDataProvider<NodeItem>, vscode.Disposable {

    // -------------------------------------------------------------------------
    // TreeDataProvider event emitter
    // -------------------------------------------------------------------------

    private readonly _onDidChangeTreeData =
        new vscode.EventEmitter<NodeItem | undefined | null | void>();

    /** Required by vscode.TreeDataProvider — fires when the tree should refresh. */
    readonly onDidChangeTreeData: vscode.Event<NodeItem | undefined | null | void> =
        this._onDidChangeTreeData.event;

    // -------------------------------------------------------------------------
    // Node selection event (consumed by InspectorPanel)
    // -------------------------------------------------------------------------

    private readonly _onNodeSelected =
        new vscode.EventEmitter<NodeItem | undefined>();

    /**
     * Fires when the user clicks a node in the scene tree.
     * InspectorPanel subscribes to this to refresh its property view.
     */
    readonly onNodeSelected: vscode.Event<NodeItem | undefined> =
        this._onNodeSelected.event;

    // -------------------------------------------------------------------------
    // Internal state
    // -------------------------------------------------------------------------

    /** Cached root nodes from the last `scene.getTree` call. */
    private _rootNodes: SceneNodeData[] = [];

    /** Whether the initial tree load has completed. */
    private _loaded = false;

    /** Whether this provider has been disposed. */
    private _disposed = false;

    /** Unsubscribe function for the `scene.treeChanged` IPC event. */
    private _unsubTreeChanged: (() => void) | null = null;

    /** Disposables owned by this provider. */
    private readonly _disposables: vscode.Disposable[] = [];

    constructor(private readonly _ipc: IpcClient) {
        // Subscribe to push events from the engine.
        this._unsubTreeChanged = this._ipc.on('scene.treeChanged', () => {
            void this._loadTree();
        });
    }

    // -------------------------------------------------------------------------
    // TreeDataProvider implementation
    // -------------------------------------------------------------------------

    /**
     * Returns the tree item for display. Required by vscode.TreeDataProvider.
     */
    getTreeItem(element: NodeItem): vscode.TreeItem {
        return element;
    }

    /**
     * Returns children for the given element, or root nodes when element is
     * undefined.
     *
     * On the first root-level call, triggers an async IPC fetch. Subsequent
     * calls use the cached tree so child expansion is immediate.
     */
    getChildren(element?: NodeItem): vscode.ProviderResult<NodeItem[]> {
        if (this._disposed) {
            return [];
        }

        if (!element) {
            // Root level: load tree if not yet loaded.
            if (!this._loaded) {
                void this._loadTree();
                return [];
            }
            return this._rootNodes.map(n => new NodeItem(n));
        }

        // Child level: expand from the cached children.
        return element.children.map(child => new NodeItem(child));
    }

    // -------------------------------------------------------------------------
    // Node selection
    // -------------------------------------------------------------------------

    /**
     * Called when the user clicks a node item (via the `uzengine.selectNode`
     * command). Sends `scene.selectNode` IPC and fires `onNodeSelected`.
     *
     * @param item  The clicked NodeItem (or undefined to clear selection).
     */
    async selectNode(item: NodeItem | undefined): Promise<void> {
        if (this._disposed) { return; }

        // Fire UI event immediately so the inspector updates without waiting
        // for the IPC round-trip.
        this._onNodeSelected.fire(item);

        // Notify the engine in the background (fire-and-forget).
        if (item) {
            try {
                await this._ipc.request('scene.selectNode', { id: item.nodeId });
            } catch {
                // Swallow — engine may not be connected; selection still reflected in UI.
            }
        }
    }

    // -------------------------------------------------------------------------
    // Tree manipulation commands
    // -------------------------------------------------------------------------

    /**
     * Sends `scene.addNode` IPC to create a child node under the given parent.
     *
     * @param parent     The parent node item (or undefined for a root node).
     * @param nodeType   Type of node to create.
     */
    async addNode(parent: NodeItem | undefined, nodeType: NodeType = 'Node3D'): Promise<void> {
        if (this._disposed) { return; }
        try {
            await this._ipc.request('scene.addNode', {
                parentId: parent?.nodeId ?? null,
                type: nodeType,
            });
            // Tree refresh comes via the `scene.treeChanged` push event.
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to add node — ${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Sends `scene.deleteNode` IPC to remove the given node.
     *
     * @param item  The node to delete.
     */
    async deleteNode(item: NodeItem): Promise<void> {
        if (this._disposed) { return; }
        try {
            await this._ipc.request('scene.deleteNode', { id: item.nodeId });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to delete node — ${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Sends `scene.duplicateNode` IPC to duplicate the given node.
     *
     * @param item  The node to duplicate.
     */
    async duplicateNode(item: NodeItem): Promise<void> {
        if (this._disposed) { return; }
        try {
            await this._ipc.request('scene.duplicateNode', { id: item.nodeId });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to duplicate node — ${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Prompts the user for a new name and sends `scene.renameNode` IPC.
     *
     * @param item  The node to rename.
     */
    async renameNode(item: NodeItem): Promise<void> {
        if (this._disposed) { return; }

        const newName = await vscode.window.showInputBox({
            prompt: 'Enter new node name',
            value: item.nodeName,
            validateInput: (value) => {
                const trimmed = value.trim();
                if (!trimmed) { return 'Node name cannot be empty'; }
                if (trimmed.length > 128) { return 'Node name is too long (max 128 chars)'; }
                return undefined;
            },
        });

        if (newName === undefined || newName.trim() === '') {
            return; // User cancelled or entered empty string.
        }

        try {
            await this._ipc.request('scene.renameNode', {
                id: item.nodeId,
                name: newName.trim(),
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to rename node — ${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Forces a full tree refresh by re-fetching from the engine.
     * Can be called explicitly (e.g., after a scene load).
     */
    refresh(): void {
        if (this._disposed) { return; }
        void this._loadTree();
    }

    /**
     * Populates the tree from locally-parsed scene data (no IPC required).
     * Called when a .uzscene file is opened in the editor.
     *
     * @param nodes  Root nodes from `parseUzScene().roots`.
     */
    loadFromFile(nodes: SceneNodeData[]): void {
        if (this._disposed) { return; }
        this._rootNodes = nodes;
        this._loaded = true;
        this._onDidChangeTreeData.fire();
    }

    /**
     * Clears the tree (e.g., when the .uzscene file is closed).
     */
    clear(): void {
        if (this._disposed) { return; }
        this._rootNodes = [];
        this._loaded = false;
        this._onDidChangeTreeData.fire();
    }

    // -------------------------------------------------------------------------
    // Dispose
    // -------------------------------------------------------------------------

    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;

        this._unsubTreeChanged?.();
        this._unsubTreeChanged = null;

        this._onDidChangeTreeData.dispose();
        this._onNodeSelected.dispose();

        for (const d of this._disposables) {
            d.dispose();
        }
        this._disposables.length = 0;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /** Fetches the scene tree from the engine and triggers a UI refresh. */
    private async _loadTree(): Promise<void> {
        if (this._disposed) { return; }

        try {
            const result = await this._ipc.request('scene.getTree', {}) as unknown as GetTreeResponse;
            if (this._disposed) { return; }

            this._rootNodes = result.nodes ?? [];
            this._loaded = true;
            this._onDidChangeTreeData.fire();
        } catch {
            // Engine not connected — show empty tree without crashing.
            if (!this._disposed) {
                this._rootNodes = [];
                this._loaded = true;
                this._onDidChangeTreeData.fire();
            }
        }
    }
}
