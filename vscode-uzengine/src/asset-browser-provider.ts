/**
 * asset-browser-provider.ts — TreeDataProvider for the Asset Browser panel.
 *
 * Groups assets by type (Scenes / Meshes / Textures / Materials / Shaders / Audio)
 * and shows import status indicators. Supports drag-and-drop as a drag source for
 * assets via the DragDropCoordinator.
 *
 * Data is fetched from the engine via the `asset.list` IPC call and refreshed
 * automatically when file changes are detected.
 */

import * as vscode from 'vscode';
import { DragDropCoordinator } from './editor/dragDropCoordinator';
import { AssetDragPayload, MIME_ASSET } from './editor/dragDropTypes';
import { IpcClient } from './ipc-client';

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

/** Import status returned by the engine. */
export type ImportStatus = 'Fresh' | 'Stale' | 'Error' | 'NotImported';

/** Raw asset entry from the `asset.list` IPC response. */
export interface AssetListEntry {
    readonly path: string;
    readonly type: string;
    readonly status: ImportStatus;
}

/** Type group keys used for top-level tree grouping. */
export type AssetGroupKey = 'Scene' | 'Mesh' | 'Texture' | 'Material' | 'Shader' | 'Audio';

/** Display labels and codicon IDs for each group. */
const GROUP_META: Record<AssetGroupKey, { label: string; icon: string }> = {
    Scene:    { label: 'Scenes',    icon: 'file-code' },
    Mesh:     { label: 'Meshes',    icon: 'package' },
    Texture:  { label: 'Textures',  icon: 'file-media' },
    Material: { label: 'Materials', icon: 'symbol-color' },
    Shader:   { label: 'Shaders',   icon: 'code' },
    Audio:    { label: 'Audio',     icon: 'unmute' },
};

const GROUP_ORDER: AssetGroupKey[] = ['Scene', 'Mesh', 'Texture', 'Material', 'Shader', 'Audio'];

/** Status icons for import status. */
const STATUS_ICON: Record<ImportStatus, string> = {
    Fresh:       '$(check)',
    Stale:       '$(warning)',
    Error:       '$(error)',
    NotImported: '$(circle-outline)',
};

// ---------------------------------------------------------------------------
// Tree items
// ---------------------------------------------------------------------------

/** A type-group header node in the tree (e.g. "Meshes (3)"). */
export class AssetGroupItem extends vscode.TreeItem {
    constructor(
        public readonly groupKey: AssetGroupKey,
        public readonly count: number,
    ) {
        const meta = GROUP_META[groupKey];
        super(`${meta.label} (${count})`, vscode.TreeItemCollapsibleState.Collapsed);
        this.iconPath = new vscode.ThemeIcon(meta.icon);
        this.contextValue = 'assetGroup';
    }
}

/** A single asset leaf node in the tree. */
export class AssetItem extends vscode.TreeItem {
    constructor(
        public readonly assetPath: string,
        public readonly assetType: string,
        public readonly importStatus: ImportStatus,
    ) {
        const displayName = assetPath.split('/').pop() || assetPath;
        const statusPrefix = STATUS_ICON[importStatus] || '';
        super(displayName, vscode.TreeItemCollapsibleState.None);
        this.description = `${statusPrefix} ${importStatus}`;
        this.tooltip = `${assetPath}\nStatus: ${importStatus}`;
        this.contextValue = 'assetItem';

        // Click to reveal in editor.
        this.command = {
            command: 'uzengine.assetBrowser.reveal',
            title: 'Reveal Asset',
            arguments: [this],
        };
    }
}

export type AssetTreeItem = AssetGroupItem | AssetItem;

// ---------------------------------------------------------------------------
// AssetBrowserDragController
// ---------------------------------------------------------------------------

export class AssetBrowserDragController implements vscode.TreeDragAndDropController<AssetTreeItem> {
    readonly dragMimeTypes: readonly string[] = [MIME_ASSET];
    readonly dropMimeTypes: readonly string[] = [];

    constructor(private readonly _provider: AssetBrowserProvider) {}

    handleDrag(
        source: readonly AssetTreeItem[],
        dataTransfer: vscode.DataTransfer,
        _token: vscode.CancellationToken,
    ): void {
        const items = source.filter((s): s is AssetItem => s instanceof AssetItem);
        if (items.length === 0) { return; }

        // Serialize first asset as drag payload.
        const item = items[0];
        const payload: AssetDragPayload = {
            resPath: `res://${item.assetPath}`,
            type: this._mapAssetType(item.assetType),
            displayName: item.label as string,
        };
        dataTransfer.set(MIME_ASSET, new vscode.DataTransferItem(JSON.stringify(payload)));

        // Notify coordinator of drag start.
        const coordinator = this._provider.getCoordinator();
        if (coordinator) {
            coordinator.startDrag(payload);
        }
    }

    handleDrop(): void {
        // Asset browser does not accept drops.
    }

    private _mapAssetType(engineType: string): AssetDragPayload['type'] {
        switch (engineType) {
            case 'Mesh': return 'Mesh';
            case 'Material': return 'Material';
            case 'Texture': return 'Texture';
            case 'Audio': return 'AudioClip';
            case 'Shader': return 'Script';
            default: return 'Mesh';
        }
    }
}

// ---------------------------------------------------------------------------
// AssetBrowserProvider
// ---------------------------------------------------------------------------

export class AssetBrowserProvider implements vscode.TreeDataProvider<AssetTreeItem>, vscode.Disposable {
    private readonly _onDidChangeTreeData = new vscode.EventEmitter<AssetTreeItem | undefined | void>();
    readonly onDidChangeTreeData = this._onDidChangeTreeData.event;

    private _disposed = false;
    private _assets: AssetListEntry[] = [];
    private _groups: Map<AssetGroupKey, AssetListEntry[]> = new Map();
    private _coordinator: DragDropCoordinator | null = null;

    readonly dragController: AssetBrowserDragController;

    constructor(private readonly _ipc: IpcClient) {
        this.dragController = new AssetBrowserDragController(this);
    }

    // -- Public API -----------------------------------------------------------

    /** Refresh the asset list from the engine. */
    async refresh(): Promise<void> {
        if (this._disposed) { return; }
        try {
            const result = await this._ipc.request('asset.list', {});
            const assets = (result as { assets?: AssetListEntry[] }).assets;
            if (Array.isArray(assets)) {
                this._assets = assets;
            } else {
                this._assets = [];
            }
        } catch {
            this._assets = [];
        }
        this._rebuildGroups();
        this._onDidChangeTreeData.fire();
    }

    /** Set assets directly (used in tests or for loading from file). */
    loadAssets(assets: AssetListEntry[]): void {
        this._assets = assets;
        this._rebuildGroups();
        this._onDidChangeTreeData.fire();
    }

    /** Get the current list of assets. */
    getAssets(): readonly AssetListEntry[] {
        return this._assets;
    }

    /** Set the drag-and-drop coordinator. */
    setDragDropCoordinator(coordinator: DragDropCoordinator): void {
        this._coordinator = coordinator;
    }

    /** Get the current coordinator. */
    getCoordinator(): DragDropCoordinator | null {
        return this._coordinator;
    }

    /** Get the IPC client. */
    getIpc(): IpcClient {
        return this._ipc;
    }

    // -- TreeDataProvider<AssetTreeItem> --------------------------------------

    getTreeItem(element: AssetTreeItem): vscode.TreeItem {
        return element;
    }

    getChildren(element?: AssetTreeItem): AssetTreeItem[] {
        if (this._disposed) { return []; }

        // Root level: return type groups that have at least one asset.
        if (!element) {
            return GROUP_ORDER
                .filter(key => (this._groups.get(key)?.length ?? 0) > 0)
                .map(key => new AssetGroupItem(key, this._groups.get(key)!.length));
        }

        // Group level: return asset items.
        if (element instanceof AssetGroupItem) {
            const entries = this._groups.get(element.groupKey) || [];
            return entries.map(e => new AssetItem(e.path, e.type, e.status));
        }

        // Leaf level: no children.
        return [];
    }

    // -- Context menu actions -------------------------------------------------

    /** Reimport a single asset via IPC. */
    async reimportAsset(item: AssetItem): Promise<void> {
        if (this._disposed) { return; }
        try {
            const result = await this._ipc.request('asset.import', { path: item.assetPath });
            if ((result as { ok?: boolean }).ok === false) {
                const err = (result as { error?: string }).error || 'unknown error';
                vscode.window.showErrorMessage(`Reimport failed: ${err}`);
            } else {
                await this.refresh();
            }
        } catch (e) {
            vscode.window.showErrorMessage(
                `Reimport failed: ${e instanceof Error ? e.message : String(e)}`
            );
        }
    }

    /** Create a new material via IPC and open it in the editor. */
    async createMaterial(): Promise<string | undefined> {
        if (this._disposed) { return; }
        const name = await vscode.window.showInputBox({
            prompt: 'Material name',
            placeHolder: 'MyMaterial',
        });
        if (!name) { return; }
        return this._createAsset('asset.createMaterial', name);
    }

    /** Create a new scene via IPC and open it in the editor. */
    async createScene(): Promise<string | undefined> {
        if (this._disposed) { return; }
        const name = await vscode.window.showInputBox({
            prompt: 'Scene name',
            placeHolder: 'MyScene',
        });
        if (!name) { return; }
        return this._createAsset('asset.createScene', name);
    }

    /** Create a new shader via IPC and open it in the editor. */
    async createShader(): Promise<string | undefined> {
        if (this._disposed) { return; }
        const name = await vscode.window.showInputBox({
            prompt: 'Shader name',
            placeHolder: 'MyShader',
        });
        if (!name) { return; }
        return this._createAsset('asset.createShader', name);
    }

    /** Import external files into the assets directory via file dialog. */
    async importFiles(): Promise<void> {
        if (this._disposed) { return; }
        const uris = await vscode.window.showOpenDialog({
            canSelectMany: true,
            openLabel: 'Import',
            filters: {
                'All Assets': ['glb', 'gltf', 'fbx', 'obj', 'png', 'jpg', 'jpeg', 'tga', 'bmp', 'hdr', 'wav', 'ogg', 'mp3', 'uzmat', 'hlsl'],
                'Meshes': ['glb', 'gltf', 'fbx', 'obj'],
                'Textures': ['png', 'jpg', 'jpeg', 'tga', 'bmp', 'hdr'],
                'Audio': ['wav', 'ogg', 'mp3'],
            },
        });
        if (!uris || uris.length === 0) { return; }

        const wsFolder = vscode.workspace.workspaceFolders?.[0];
        if (!wsFolder) { return; }

        for (const uri of uris) {
            const fileName = uri.fsPath.split(/[\\/]/).pop() || 'asset';
            const destUri = vscode.Uri.joinPath(wsFolder.uri, 'assets', fileName);
            try {
                await vscode.workspace.fs.copy(uri, destUri, { overwrite: true });
                await this._ipc.request('asset.import', { path: destUri.fsPath });
            } catch (e) {
                vscode.window.showErrorMessage(
                    `Import failed for ${fileName}: ${e instanceof Error ? e.message : String(e)}`
                );
            }
        }
        await this.refresh();
    }

    // -- Dispose --------------------------------------------------------------

    dispose(): void {
        this._disposed = true;
        this._assets = [];
        this._groups.clear();
        this._onDidChangeTreeData.dispose();
    }

    // -- Private helpers ------------------------------------------------------

    private _rebuildGroups(): void {
        this._groups.clear();
        for (const entry of this._assets) {
            const key = this._typeToGroupKey(entry.type);
            if (!key) { continue; }
            let arr = this._groups.get(key);
            if (!arr) {
                arr = [];
                this._groups.set(key, arr);
            }
            arr.push(entry);
        }
    }

    private _typeToGroupKey(type: string): AssetGroupKey | null {
        switch (type) {
            case 'Scene':    return 'Scene';
            case 'Mesh':     return 'Mesh';
            case 'Texture':  return 'Texture';
            case 'Material': return 'Material';
            case 'Shader':   return 'Shader';
            case 'Audio':    return 'Audio';
            default:         return null;
        }
    }

    private async _createAsset(method: string, name: string): Promise<string | undefined> {
        try {
            const result = await this._ipc.request(method, { name });
            const r = result as { ok?: boolean; path?: string; error?: string };
            if (r.ok === false) {
                vscode.window.showErrorMessage(`Create failed: ${r.error || 'unknown error'}`);
                return;
            }
            await this.refresh();
            // Open the created file in the editor.
            if (r.path && vscode.workspace.workspaceFolders?.[0]) {
                const uri = vscode.Uri.joinPath(
                    vscode.workspace.workspaceFolders[0].uri,
                    r.path,
                );
                await vscode.commands.executeCommand('vscode.open', uri);
            }
            return r.path;
        } catch (e) {
            vscode.window.showErrorMessage(
                `Create failed: ${e instanceof Error ? e.message : String(e)}`
            );
            return;
        }
    }
}
