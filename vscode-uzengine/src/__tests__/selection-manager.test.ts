/**
 * selection-manager.test.ts — Unit tests for SelectionManager.
 *
 * Tests cover:
 *   - selectOnly: replaces selection with single ID
 *   - add / remove: individual node manipulation
 *   - toggle: Ctrl+click behavior (add if absent, remove if present)
 *   - selectRange: Shift+click contiguous range in tree order
 *   - selectMany / addMany: box-select batch operations
 *   - clear: empties selection
 *   - handleNodeRemoved: removes deleted node from selection
 *   - handleSceneLoad: clears on new scene
 *   - onChanged event fires on every mutation
 *   - primaryId tracking
 *   - dispose cleans up event emitter
 */

import { SelectionManager, FlatTreeNode } from '../editor/selectionManager';

describe('SelectionManager', () => {
    let mgr: SelectionManager;
    let firedSets: ReadonlySet<number>[];

    beforeEach(() => {
        mgr = new SelectionManager();
        firedSets = [];
        mgr.onChanged((set) => {
            firedSets.push(new Set(set));
        });
    });

    afterEach(() => {
        mgr.dispose();
    });

    // -----------------------------------------------------------------------
    // selectOnly
    // -----------------------------------------------------------------------

    describe('selectOnly', () => {
        it('replaces any existing selection with exactly one ID', () => {
            mgr.selectOnly(5);
            mgr.selectOnly(10);

            expect(mgr.count).toBe(1);
            expect(mgr.has(10)).toBe(true);
            expect(mgr.has(5)).toBe(false);
            expect(mgr.primaryId).toBe(10);
        });

        it('fires onChanged', () => {
            mgr.selectOnly(1);
            expect(firedSets).toHaveLength(1);
            expect(firedSets[0].has(1)).toBe(true);
        });

        it('ids returns array copy', () => {
            mgr.selectOnly(7);
            const ids = mgr.ids;
            expect(ids).toEqual([7]);
            // Mutating the copy should not affect internal state
            ids.push(99);
            expect(mgr.count).toBe(1);
        });
    });

    // -----------------------------------------------------------------------
    // add / remove
    // -----------------------------------------------------------------------

    describe('add', () => {
        it('adds a node without clearing existing selection', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.add(3);

            expect(mgr.count).toBe(3);
            expect(mgr.has(1)).toBe(true);
            expect(mgr.has(2)).toBe(true);
            expect(mgr.has(3)).toBe(true);
            expect(mgr.primaryId).toBe(3);
        });

        it('adding duplicate does not increase count', () => {
            mgr.selectOnly(1);
            mgr.add(1);
            expect(mgr.count).toBe(1);
        });
    });

    describe('remove', () => {
        it('removes a node from selection', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.remove(1);

            expect(mgr.count).toBe(1);
            expect(mgr.has(1)).toBe(false);
            expect(mgr.has(2)).toBe(true);
        });

        it('promotes next node to primaryId when primary is removed', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            // Primary is 2 (last added)
            expect(mgr.primaryId).toBe(2);
            mgr.remove(2);
            // Should promote first remaining (1)
            expect(mgr.primaryId).toBe(1);
        });

        it('sets primaryId to undefined when last node is removed', () => {
            mgr.selectOnly(1);
            mgr.remove(1);
            expect(mgr.primaryId).toBeUndefined();
            expect(mgr.count).toBe(0);
        });

        it('removing non-existent node still fires onChanged', () => {
            const countBefore = firedSets.length;
            mgr.remove(999);
            expect(firedSets.length).toBe(countBefore + 1);
        });
    });

    // -----------------------------------------------------------------------
    // toggle (Ctrl+click)
    // -----------------------------------------------------------------------

    describe('toggle', () => {
        it('adds a node if not selected', () => {
            mgr.toggle(5);
            expect(mgr.has(5)).toBe(true);
            expect(mgr.count).toBe(1);
        });

        it('removes a node if already selected', () => {
            mgr.selectOnly(5);
            mgr.toggle(5);
            expect(mgr.has(5)).toBe(false);
            expect(mgr.count).toBe(0);
        });

        it('preserves other selected nodes when toggling', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.toggle(3); // add 3
            expect(mgr.count).toBe(3);
            mgr.toggle(2); // remove 2
            expect(mgr.count).toBe(2);
            expect(mgr.has(1)).toBe(true);
            expect(mgr.has(2)).toBe(false);
            expect(mgr.has(3)).toBe(true);
        });
    });

    // -----------------------------------------------------------------------
    // selectRange (Shift+click)
    // -----------------------------------------------------------------------

    describe('selectRange', () => {
        const flatTree: FlatTreeNode[] = [
            { id: 1 }, { id: 2 }, { id: 3 }, { id: 4 }, { id: 5 },
        ];

        beforeEach(() => {
            mgr.setFlatTree(flatTree);
        });

        it('selects contiguous range from primaryId to target', () => {
            mgr.selectOnly(2); // primary = 2
            mgr.selectRange(4);

            expect(mgr.count).toBe(3);
            expect(mgr.has(2)).toBe(true);
            expect(mgr.has(3)).toBe(true);
            expect(mgr.has(4)).toBe(true);
            expect(mgr.has(1)).toBe(false);
            expect(mgr.has(5)).toBe(false);
        });

        it('works in reverse direction (target before primary)', () => {
            mgr.selectOnly(4); // primary = 4
            mgr.selectRange(2);

            expect(mgr.count).toBe(3);
            expect(mgr.has(2)).toBe(true);
            expect(mgr.has(3)).toBe(true);
            expect(mgr.has(4)).toBe(true);
        });

        it('selects single node when range start equals end', () => {
            mgr.selectOnly(3);
            mgr.selectRange(3);
            expect(mgr.count).toBe(1);
            expect(mgr.has(3)).toBe(true);
        });

        it('falls back to selectOnly when no primaryId', () => {
            mgr.clear();
            mgr.selectRange(4);
            expect(mgr.count).toBe(1);
            expect(mgr.has(4)).toBe(true);
            expect(mgr.primaryId).toBe(4);
        });

        it('falls back to selectOnly when flat tree is empty', () => {
            mgr.setFlatTree([]);
            mgr.selectOnly(1);
            mgr.selectRange(3);
            expect(mgr.count).toBe(1);
            expect(mgr.has(3)).toBe(true);
        });

        it('falls back to selectOnly when target not in flat tree', () => {
            mgr.selectOnly(1);
            mgr.selectRange(99); // not in tree
            expect(mgr.count).toBe(1);
            expect(mgr.has(99)).toBe(true);
        });

        it('preserves primaryId (anchor) after range select', () => {
            mgr.selectOnly(2);
            mgr.selectRange(4);
            // Primary should remain 2 (the anchor), not change to 4
            expect(mgr.primaryId).toBe(2);
        });
    });

    // -----------------------------------------------------------------------
    // selectMany / addMany (box-select)
    // -----------------------------------------------------------------------

    describe('selectMany', () => {
        it('replaces selection with multiple IDs', () => {
            mgr.selectOnly(99);
            mgr.selectMany([1, 2, 3]);
            expect(mgr.count).toBe(3);
            expect(mgr.has(99)).toBe(false);
            expect(mgr.has(1)).toBe(true);
            expect(mgr.has(2)).toBe(true);
            expect(mgr.has(3)).toBe(true);
            expect(mgr.primaryId).toBe(1);
        });

        it('handles empty array', () => {
            mgr.selectOnly(1);
            mgr.selectMany([]);
            expect(mgr.count).toBe(0);
            expect(mgr.primaryId).toBeUndefined();
        });
    });

    describe('addMany', () => {
        it('adds multiple IDs to existing selection', () => {
            mgr.selectOnly(1);
            mgr.addMany([2, 3]);
            expect(mgr.count).toBe(3);
            expect(mgr.has(1)).toBe(true);
            expect(mgr.has(2)).toBe(true);
            expect(mgr.has(3)).toBe(true);
            expect(mgr.primaryId).toBe(2); // first of the added batch
        });

        it('handles empty array without error', () => {
            mgr.selectOnly(1);
            mgr.addMany([]);
            // Still fires (no-op optimization not required)
            expect(mgr.count).toBe(1);
        });
    });

    // -----------------------------------------------------------------------
    // clear
    // -----------------------------------------------------------------------

    describe('clear', () => {
        it('empties the selection and unsets primaryId', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.clear();
            expect(mgr.count).toBe(0);
            expect(mgr.primaryId).toBeUndefined();
        });

        it('fires onChanged when clearing non-empty selection', () => {
            mgr.selectOnly(1);
            const countBefore = firedSets.length;
            mgr.clear();
            expect(firedSets.length).toBe(countBefore + 1);
            expect(firedSets[firedSets.length - 1].size).toBe(0);
        });

        it('does not fire onChanged when already empty', () => {
            // Fresh manager, already empty
            const countBefore = firedSets.length;
            mgr.clear();
            expect(firedSets.length).toBe(countBefore);
        });
    });

    // -----------------------------------------------------------------------
    // handleNodeRemoved
    // -----------------------------------------------------------------------

    describe('handleNodeRemoved', () => {
        it('removes the deleted node from selection', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.handleNodeRemoved(1);
            expect(mgr.count).toBe(1);
            expect(mgr.has(1)).toBe(false);
            expect(mgr.has(2)).toBe(true);
        });

        it('does not fire if node was not selected', () => {
            mgr.selectOnly(1);
            const countBefore = firedSets.length;
            mgr.handleNodeRemoved(99);
            expect(firedSets.length).toBe(countBefore);
        });

        it('updates primaryId when removed node was primary', () => {
            mgr.selectOnly(5);
            mgr.add(10);
            // primary = 10
            mgr.handleNodeRemoved(10);
            expect(mgr.primaryId).toBe(5);
        });

        it('sets primaryId to undefined when removing last selected node', () => {
            mgr.selectOnly(5);
            mgr.handleNodeRemoved(5);
            expect(mgr.primaryId).toBeUndefined();
            expect(mgr.count).toBe(0);
        });
    });

    // -----------------------------------------------------------------------
    // handleSceneLoad
    // -----------------------------------------------------------------------

    describe('handleSceneLoad', () => {
        it('clears selection on new scene', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            mgr.handleSceneLoad();
            expect(mgr.count).toBe(0);
            expect(mgr.primaryId).toBeUndefined();
        });
    });

    // -----------------------------------------------------------------------
    // onChanged event
    // -----------------------------------------------------------------------

    describe('onChanged', () => {
        it('fires on every mutation method', () => {
            mgr.selectOnly(1);   // 1
            mgr.add(2);          // 2
            mgr.remove(1);       // 3
            mgr.toggle(3);       // 4
            mgr.selectMany([4]); // 5
            mgr.addMany([5]);    // 6
            mgr.clear();         // 7
            expect(firedSets.length).toBe(7);
        });

        it('subscription can be disposed', () => {
            const extra: number[] = [];
            const sub = mgr.onChanged((set) => extra.push(set.size));
            mgr.selectOnly(1);
            expect(extra).toEqual([1]);
            sub.dispose();
            mgr.selectOnly(2);
            expect(extra).toEqual([1]); // no second fire
        });
    });

    // -----------------------------------------------------------------------
    // selected (read-only set accessor)
    // -----------------------------------------------------------------------

    describe('selected', () => {
        it('returns the current set (read-only reference)', () => {
            mgr.selectOnly(1);
            mgr.add(2);
            const s = mgr.selected;
            expect(s.size).toBe(2);
            expect(s.has(1)).toBe(true);
            expect(s.has(2)).toBe(true);
        });
    });

    // -----------------------------------------------------------------------
    // dispose
    // -----------------------------------------------------------------------

    describe('dispose', () => {
        it('stops firing events after dispose', () => {
            mgr.dispose();
            const countBefore = firedSets.length;
            mgr.selectOnly(1);
            // EventEmitter.fire is no-op after dispose
            expect(firedSets.length).toBe(countBefore);
        });
    });
});
