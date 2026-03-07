/**
 * asset-watcher.ts — Watches the project assets directory for new files
 * and auto-imports them via the `asset.import` IPC call.
 *
 * Uses a 200ms debounce to avoid duplicate imports when editors save
 * temporary files alongside the real asset.
 */

import * as vscode from 'vscode';
import { IpcClient } from './ipc-client';

/** File extensions that can be imported as engine assets. */
const IMPORTABLE_EXTENSIONS = new Set([
    '.glb', '.gltf', '.fbx', '.obj',       // Meshes
    '.png', '.jpg', '.jpeg', '.tga', '.bmp', '.hdr', // Textures
    '.wav', '.ogg', '.mp3',                 // Audio
    '.uzmat',                                // Materials
    '.uzscene',                              // Scenes
    '.hlsl',                                 // Shaders
]);

export class AssetWatcher implements vscode.Disposable {
    private readonly _disposables: vscode.Disposable[] = [];
    private _debounceTimer: ReturnType<typeof setTimeout> | null = null;
    private _pendingPaths = new Set<string>();
    private _disposed = false;
    private _onDidImport = new vscode.EventEmitter<string>();
    readonly onDidImport: vscode.Event<string> = this._onDidImport.event;

    constructor(
        private readonly _ipc: IpcClient,
        private readonly _refreshCallback?: () => void,
    ) {
        const watcher = vscode.workspace.createFileSystemWatcher('**/assets/**/*');
        watcher.onDidCreate((uri) => this._onFileCreated(uri), null, this._disposables);
        this._disposables.push(watcher);
    }

    private _onFileCreated(uri: vscode.Uri): void {
        if (this._disposed) { return; }

        const ext = this._getExtension(uri.fsPath);
        if (!ext || !IMPORTABLE_EXTENSIONS.has(ext)) { return; }

        this._pendingPaths.add(uri.fsPath);

        // Debounce: wait 200ms before importing, in case multiple files arrive together.
        if (this._debounceTimer) {
            clearTimeout(this._debounceTimer);
        }
        this._debounceTimer = setTimeout(() => {
            void this._importPending();
        }, 200);
    }

    private async _importPending(): Promise<void> {
        if (this._disposed) { return; }

        const paths = [...this._pendingPaths];
        this._pendingPaths.clear();
        this._debounceTimer = null;

        for (const filePath of paths) {
            try {
                await this._ipc.request('asset.import', { path: filePath });
                this._onDidImport.fire(filePath);

                const name = filePath.split(/[\\/]/).pop() || filePath;
                vscode.window.showInformationMessage(`Imported asset: ${name}`);
            } catch {
                // Import failures are non-fatal — the asset browser will show
                // the Error status on next refresh.
            }
        }

        if (paths.length > 0 && this._refreshCallback) {
            this._refreshCallback();
        }
    }

    private _getExtension(filePath: string): string | null {
        const dot = filePath.lastIndexOf('.');
        if (dot < 0) { return null; }
        return filePath.substring(dot).toLowerCase();
    }

    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;
        if (this._debounceTimer) {
            clearTimeout(this._debounceTimer);
            this._debounceTimer = null;
        }
        this._pendingPaths.clear();
        this._onDidImport.dispose();
        for (const d of this._disposables) {
            d.dispose();
        }
        this._disposables.length = 0;
    }
}
