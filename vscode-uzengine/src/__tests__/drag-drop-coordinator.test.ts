/**
 * drag-drop-coordinator.test.ts — Tests for DragDropCoordinator.
 */

import { jest } from '@jest/globals';
import { DragDropCoordinator, DragDropState } from '../editor/dragDropCoordinator';
import { AssetDragPayload } from '../editor/dragDropTypes';
import { IpcClient } from '../ipc-client';

// ---------------------------------------------------------------------------
// Mock IPC client
// ---------------------------------------------------------------------------

function makeMockIpc(): IpcClient {
    return {
        request: jest.fn<IpcClient['request']>().mockResolvedValue({ ok: true }),
        on: jest.fn(() => () => {}),
        connect: jest.fn(),
        disconnect: jest.fn(),
        state: 'connected' as const,
        isConnected: true,
    } as unknown as IpcClient;
}

function makeAsset(overrides?: Partial<AssetDragPayload>): AssetDragPayload {
    return {
        resPath: 'res://meshes/cube.glb',
        type: 'Mesh',
        displayName: 'cube.glb',
        ...overrides,
    };
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('DragDropCoordinator', () => {
    let ipc: IpcClient;
    let coordinator: DragDropCoordinator;

    beforeEach(() => {
        ipc = makeMockIpc();
        coordinator = new DragDropCoordinator(ipc);
    });

    afterEach(() => {
        coordinator.dispose();
    });

    // -- Initial state -------------------------------------------------------

    it('starts in idle state with no pending asset', () => {
        expect(coordinator.state).toBe('idle');
        expect(coordinator.pendingAsset).toBeNull();
    });

    // -- startDrag -----------------------------------------------------------

    it('transitions to dragging on startDrag', () => {
        const asset = makeAsset();
        coordinator.startDrag(asset);
        expect(coordinator.state).toBe('dragging');
        expect(coordinator.pendingAsset).toEqual(asset);
    });

    it('fires onStateChanged when transitioning to dragging', () => {
        const states: DragDropState[] = [];
        coordinator.onStateChanged(s => states.push(s));

        coordinator.startDrag(makeAsset());
        expect(states).toEqual(['dragging']);
    });

    it('replaces a pending drag if startDrag is called while dragging', () => {
        const asset1 = makeAsset({ displayName: 'a.glb' });
        const asset2 = makeAsset({ displayName: 'b.glb' });

        coordinator.startDrag(asset1);
        coordinator.startDrag(asset2);

        expect(coordinator.state).toBe('dragging');
        expect(coordinator.pendingAsset).toEqual(asset2);
    });

    it('is a no-op when disposed', () => {
        coordinator.dispose();
        coordinator.startDrag(makeAsset());
        expect(coordinator.state).toBe('idle');
        expect(coordinator.pendingAsset).toBeNull();
    });

    // -- endDrag -------------------------------------------------------------

    it('transitions back to idle on endDrag', () => {
        coordinator.startDrag(makeAsset());
        coordinator.endDrag();
        expect(coordinator.state).toBe('idle');
        expect(coordinator.pendingAsset).toBeNull();
    });

    it('fires onStateChanged to idle on endDrag', () => {
        const states: DragDropState[] = [];
        coordinator.onStateChanged(s => states.push(s));

        coordinator.startDrag(makeAsset());
        coordinator.endDrag();
        expect(states).toEqual(['dragging', 'idle']);
    });

    it('endDrag is a no-op when already idle', () => {
        const states: DragDropState[] = [];
        coordinator.onStateChanged(s => states.push(s));
        coordinator.endDrag();
        expect(states).toEqual([]);
    });

    it('endDrag is a no-op when disposed', () => {
        coordinator.startDrag(makeAsset());
        coordinator.dispose();
        coordinator.endDrag();
        expect(coordinator.state).toBe('idle');
    });

    // -- handleViewportDrop --------------------------------------------------

    it('calls editor.placeAsset IPC on viewport drop', async () => {
        const asset = makeAsset();
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true, nodeId: 42 });

        coordinator.startDrag(asset);
        const result = await coordinator.handleViewportDrop(100, 200);

        expect(result).toEqual({ ok: true, nodeId: 42 });
        expect(ipc.request).toHaveBeenCalledWith('editor.placeAsset', {
            resPath: asset.resPath,
            assetType: asset.type,
            screenX: 100,
            screenY: 200,
        });
    });

    it('transitions through dropping → idle during viewport drop', async () => {
        const states: DragDropState[] = [];
        coordinator.onStateChanged(s => states.push(s));

        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true, nodeId: 1 });

        coordinator.startDrag(makeAsset());
        await coordinator.handleViewportDrop(10, 20);

        expect(states).toEqual(['dragging', 'dropping', 'idle']);
    });

    it('returns error if no active drag', async () => {
        const result = await coordinator.handleViewportDrop(0, 0);
        expect(result).toEqual({ ok: false, error: 'No active drag' });
    });

    it('returns error if disposed', async () => {
        coordinator.dispose();
        const result = await coordinator.handleViewportDrop(0, 0);
        expect(result).toEqual({ ok: false, error: 'Coordinator disposed' });
    });

    it('returns error message on IPC failure', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('timeout'));

        coordinator.startDrag(makeAsset());
        const result = await coordinator.handleViewportDrop(10, 20);

        expect(result).toEqual({ ok: false, error: 'timeout' });
        expect(coordinator.state).toBe('idle');
    });

    it('returns error message for non-Error IPC failure', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce('string error');

        coordinator.startDrag(makeAsset());
        const result = await coordinator.handleViewportDrop(10, 20);

        expect(result).toEqual({ ok: false, error: 'string error' });
    });

    it('clears pendingAsset after viewport drop', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true });

        coordinator.startDrag(makeAsset());
        await coordinator.handleViewportDrop(0, 0);

        expect(coordinator.pendingAsset).toBeNull();
    });

    // -- reparentNode --------------------------------------------------------

    it('calls scene.reparentNode IPC', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true });

        const result = await coordinator.reparentNode('5', '2');

        expect(result).toEqual({ ok: true, error: undefined });
        expect(ipc.request).toHaveBeenCalledWith('scene.reparentNode', {
            id: 5,
            newParentId: 2,
        });
    });

    it('returns error on IPC failure for reparent', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('circular ref'));

        const result = await coordinator.reparentNode('3', '3');

        expect(result).toEqual({ ok: false, error: 'circular ref' });
    });

    it('returns error for non-Error on reparent failure', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(42);

        const result = await coordinator.reparentNode('3', '1');

        expect(result).toEqual({ ok: false, error: '42' });
    });

    it('returns error when disposed for reparent', async () => {
        coordinator.dispose();
        const result = await coordinator.reparentNode('1', '2');
        expect(result).toEqual({ ok: false, error: 'Coordinator disposed' });
    });

    it('parses string nodeIds to integers', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true });

        await coordinator.reparentNode('42', '0');

        expect(ipc.request).toHaveBeenCalledWith('scene.reparentNode', {
            id: 42,
            newParentId: 0,
        });
    });

    // -- edge cases: result.ok missing → defaults to true -------------------

    it('defaults ok to true when IPC result has no ok field (viewport drop)', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ nodeId: 7 });

        coordinator.startDrag(makeAsset());
        const result = await coordinator.handleViewportDrop(1, 2);

        expect(result.ok).toBe(true);
        expect(result.nodeId).toBe(7);
    });

    it('defaults ok to true when IPC result has no ok field (reparent)', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({});

        const result = await coordinator.reparentNode('1', '2');

        expect(result.ok).toBe(true);
    });

    // -- dispose -------------------------------------------------------------

    it('clears state on dispose', () => {
        coordinator.startDrag(makeAsset());
        coordinator.dispose();

        expect(coordinator.state).toBe('idle');
        expect(coordinator.pendingAsset).toBeNull();
    });

    it('double dispose is safe', () => {
        coordinator.dispose();
        expect(() => coordinator.dispose()).not.toThrow();
    });
});
