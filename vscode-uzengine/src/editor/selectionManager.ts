/**
 * selectionManager.ts — Manages multi-node selection state for the UzEngine editor.
 *
 * Provides Blender-style selection: single-click replaces, Ctrl+click toggles,
 * Shift+click range-selects, box-select adds multiple nodes.
 *
 * Fires `onChanged` whenever the selection mutates so that the Scene Tree,
 * Viewport, and Inspector panels can react.
 */

import * as vscode from 'vscode';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Flat node entry used for depth-first range selection. */
export interface FlatTreeNode {
    readonly id: number;
}

// ---------------------------------------------------------------------------
// SelectionManager
// ---------------------------------------------------------------------------

export class SelectionManager implements vscode.Disposable {
    /** The set of currently selected node IDs. */
    private _selected = new Set<number>();

    /** The most recently plain-clicked node (anchor for Shift+click, Inspector title). */
    private _primaryId: number | undefined;

    /** Cached flat tree order for range selection. */
    private _flatTree: FlatTreeNode[] = [];

    private readonly _onChanged = new vscode.EventEmitter<ReadonlySet<number>>();

    /** Fires whenever the selection set changes. */
    readonly onChanged: vscode.Event<ReadonlySet<number>> = this._onChanged.event;

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /** The most recently selected node (anchor). */
    get primaryId(): number | undefined {
        return this._primaryId;
    }

    /** All selected node IDs as a read-only set. */
    get selected(): ReadonlySet<number> {
        return this._selected;
    }

    /** Array copy of selected IDs (for serialization / IPC). */
    get ids(): number[] {
        return [...this._selected];
    }

    /** Number of selected nodes. */
    get count(): number {
        return this._selected.size;
    }

    /** Check if a node is selected. */
    has(id: number): boolean {
        return this._selected.has(id);
    }

    // -----------------------------------------------------------------------
    // Mutation methods (all fire onChanged)
    // -----------------------------------------------------------------------

    /** Replace selection with exactly one node. */
    selectOnly(id: number): void {
        this._selected.clear();
        this._selected.add(id);
        this._primaryId = id;
        this._fire();
    }

    /** Add a single node to the selection without clearing others. */
    add(id: number): void {
        this._selected.add(id);
        this._primaryId = id;
        this._fire();
    }

    /** Remove a single node from the selection. */
    remove(id: number): void {
        this._selected.delete(id);
        if (this._primaryId === id) {
            // Promote the first remaining node or undefined.
            const first = this._selected.values().next();
            this._primaryId = first.done ? undefined : first.value;
        }
        this._fire();
    }

    /** Toggle a node: add if absent, remove if present (Ctrl+click). */
    toggle(id: number): void {
        if (this._selected.has(id)) {
            this.remove(id);
        } else {
            this.add(id);
        }
    }

    /**
     * Select a contiguous range from the current primaryId to `toId`
     * using depth-first tree order (Shift+click).
     *
     * If no primaryId exists, falls back to selectOnly(toId).
     */
    selectRange(toId: number): void {
        const fromId = this._primaryId;
        if (fromId === undefined || this._flatTree.length === 0) {
            this.selectOnly(toId);
            return;
        }

        let fromIdx = -1;
        let toIdx = -1;
        for (let i = 0; i < this._flatTree.length; i++) {
            if (this._flatTree[i].id === fromId) fromIdx = i;
            if (this._flatTree[i].id === toId) toIdx = i;
        }

        if (fromIdx < 0 || toIdx < 0) {
            this.selectOnly(toId);
            return;
        }

        const lo = Math.min(fromIdx, toIdx);
        const hi = Math.max(fromIdx, toIdx);

        this._selected.clear();
        for (let i = lo; i <= hi; i++) {
            this._selected.add(this._flatTree[i].id);
        }
        // Keep primaryId unchanged (anchor stays at fromId).
        this._fire();
    }

    /** Add multiple node IDs at once (box-select result). */
    addMany(ids: number[]): void {
        for (const id of ids) {
            this._selected.add(id);
        }
        if (ids.length > 0) {
            this._primaryId = ids[0];
        }
        this._fire();
    }

    /** Replace selection with multiple IDs (box-select without Ctrl). */
    selectMany(ids: number[]): void {
        this._selected.clear();
        for (const id of ids) {
            this._selected.add(id);
        }
        this._primaryId = ids.length > 0 ? ids[0] : undefined;
        this._fire();
    }

    /** Clear all selection. */
    clear(): void {
        if (this._selected.size === 0 && this._primaryId === undefined) return;
        this._selected.clear();
        this._primaryId = undefined;
        this._fire();
    }

    // -----------------------------------------------------------------------
    // Tree order cache (for Shift+click range)
    // -----------------------------------------------------------------------

    /** Update the flat depth-first tree order used by selectRange. */
    setFlatTree(nodes: FlatTreeNode[]): void {
        this._flatTree = nodes;
    }

    // -----------------------------------------------------------------------
    // Scene lifecycle
    // -----------------------------------------------------------------------

    /** Remove a node that was deleted from the scene. */
    handleNodeRemoved(id: number): void {
        if (this._selected.has(id)) {
            this._selected.delete(id);
            if (this._primaryId === id) {
                const first = this._selected.values().next();
                this._primaryId = first.done ? undefined : first.value;
            }
            this._fire();
        }
    }

    /** Clear selection when a new scene is loaded. */
    handleSceneLoad(): void {
        this.clear();
    }

    // -----------------------------------------------------------------------
    // Internals
    // -----------------------------------------------------------------------

    private _fire(): void {
        this._onChanged.fire(this._selected);
    }

    dispose(): void {
        this._onChanged.dispose();
    }
}
