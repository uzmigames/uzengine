/**
 * dragDropCoordinator.ts — Singleton coordinator for drag-and-drop operations.
 *
 * Manages state transitions for cross-panel drag operations:
 *   idle → dragging → dropping → idle
 *
 * Used by:
 *  - Scene Tree (re-parenting via TreeDragAndDropController)
 *  - Asset Browser → Scene Tree (asset assignment)
 *  - Asset Browser → Viewport (world placement)
 */

import * as vscode from 'vscode';
import { IpcClient } from '../ipc-client';
import { AssetDragPayload } from './dragDropTypes';

/** Drag operation state machine states. */
export type DragDropState = 'idle' | 'dragging' | 'dropping';

/**
 * Coordinates drag-and-drop across editor panels.
 *
 * Holds the pending asset payload while a drag is in progress,
 * notifies the viewport webview to enter/exit drop mode, and
 * dispatches the final drop action via IPC.
 */
export class DragDropCoordinator implements vscode.Disposable {
    private _state: DragDropState = 'idle';
    private _pendingAsset: AssetDragPayload | null = null;
    private _disposed = false;

    private readonly _onStateChanged = new vscode.EventEmitter<DragDropState>();
    readonly onStateChanged: vscode.Event<DragDropState> = this._onStateChanged.event;

    constructor(private readonly _ipc: IpcClient) {}

    /** Current drag state. */
    get state(): DragDropState {
        return this._state;
    }

    /** The asset being dragged, or null if idle. */
    get pendingAsset(): AssetDragPayload | null {
        return this._pendingAsset;
    }

    /**
     * Begin a drag operation with an asset payload.
     * Transitions from idle → dragging.
     */
    startDrag(asset: AssetDragPayload): void {
        if (this._disposed) { return; }
        if (this._state !== 'idle') {
            this.endDrag();
        }
        this._pendingAsset = asset;
        this._setState('dragging');
    }

    /**
     * Cancel or finish a drag operation.
     * Transitions from dragging/dropping → idle.
     */
    endDrag(): void {
        if (this._disposed) { return; }
        this._pendingAsset = null;
        this._setState('idle');
    }

    /**
     * Handle a drop on the viewport at screen coordinates.
     * Transitions dragging → dropping → idle.
     *
     * Calls `editor.placeAsset` IPC and returns the result.
     */
    async handleViewportDrop(x: number, y: number): Promise<{ ok: boolean; nodeId?: number; error?: string }> {
        if (this._disposed) {
            return { ok: false, error: 'Coordinator disposed' };
        }
        if (this._state !== 'dragging' || !this._pendingAsset) {
            return { ok: false, error: 'No active drag' };
        }

        const asset = this._pendingAsset;
        this._setState('dropping');

        try {
            const result = await this._ipc.request('editor.placeAsset', {
                resPath: asset.resPath,
                assetType: asset.type,
                screenX: x,
                screenY: y,
            });

            return {
                ok: result.ok as boolean ?? true,
                nodeId: result.nodeId as number | undefined,
            };
        } catch (err) {
            return {
                ok: false,
                error: err instanceof Error ? err.message : String(err),
            };
        } finally {
            this._pendingAsset = null;
            this._setState('idle');
        }
    }

    /**
     * Reparent a scene node via IPC.
     * Used by the TreeDragAndDropController for scene tree re-ordering.
     */
    async reparentNode(nodeId: string, newParentId: string): Promise<{ ok: boolean; error?: string }> {
        if (this._disposed) {
            return { ok: false, error: 'Coordinator disposed' };
        }

        try {
            const result = await this._ipc.request('scene.reparentNode', {
                id: parseInt(nodeId, 10),
                newParentId: parseInt(newParentId, 10),
            });

            return {
                ok: result.ok as boolean ?? true,
                error: result.error as string | undefined,
            };
        } catch (err) {
            return {
                ok: false,
                error: err instanceof Error ? err.message : String(err),
            };
        }
    }

    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;
        this._pendingAsset = null;
        this._state = 'idle';
        this._onStateChanged.dispose();
    }

    private _setState(newState: DragDropState): void {
        if (this._state === newState) { return; }
        this._state = newState;
        this._onStateChanged.fire(newState);
    }
}
