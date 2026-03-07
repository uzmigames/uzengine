/**
 * scene-tree-drag-drop.test.ts — Tests for SceneTreeDragController and
 * DragDropCoordinator integration with SceneTreeProvider.
 */

import { jest } from '@jest/globals';
import * as vscode from 'vscode';
import { DragDropCoordinator } from '../editor/dragDropCoordinator';
import { MIME_ASSET, MIME_SCENE_TREE } from '../editor/dragDropTypes';
import { IpcClient } from '../ipc-client';
import { NodeItem, SceneTreeDragController, SceneTreeProvider } from '../scene-tree-provider';

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

function makeNodeItem(id: string, name: string): NodeItem {
    return new NodeItem({ id, name, type: 'Node3D' });
}

// ---------------------------------------------------------------------------
// SceneTreeDragController tests
// ---------------------------------------------------------------------------

describe('SceneTreeDragController', () => {
    let ipc: IpcClient;
    let provider: SceneTreeProvider;
    let coordinator: DragDropCoordinator;
    let controller: SceneTreeDragController;

    beforeEach(() => {
        ipc = makeMockIpc();
        provider = new SceneTreeProvider(ipc);
        coordinator = new DragDropCoordinator(ipc);
        provider.setDragDropCoordinator(coordinator);
        controller = provider.dragController;
    });

    afterEach(() => {
        coordinator.dispose();
        provider.dispose();
    });

    // -- MIME types -----------------------------------------------------------

    it('exposes correct dragMimeTypes', () => {
        expect(controller.dragMimeTypes).toContain(MIME_SCENE_TREE);
    });

    it('exposes correct dropMimeTypes', () => {
        expect(controller.dropMimeTypes).toContain(MIME_SCENE_TREE);
        expect(controller.dropMimeTypes).toContain(MIME_ASSET);
    });

    // -- handleDrag -----------------------------------------------------------

    it('serialises node IDs into DataTransfer', () => {
        const items = [makeNodeItem('1', 'A'), makeNodeItem('2', 'B')];
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag(items, dt, token);

        const data = dt.get(MIME_SCENE_TREE);
        expect(data).toBeDefined();
        expect(JSON.parse(data!.value as string)).toEqual(['1', '2']);
    });

    it('does nothing for empty source array', () => {
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag([], dt, token);

        expect(dt.get(MIME_SCENE_TREE)).toBeUndefined();
    });

    // -- handleDrop (scene tree re-parent) ------------------------------------

    it('calls reparentNode for each dragged node', async () => {
        const reparentSpy = jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: true });

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['3', '4'])));
        const target = makeNodeItem('1', 'Parent');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(reparentSpy).toHaveBeenCalledWith('3', '1');
        expect(reparentSpy).toHaveBeenCalledWith('4', '1');
        expect(reparentSpy).toHaveBeenCalledTimes(2);
    });

    it('skips reparenting a node onto itself', async () => {
        const reparentSpy = jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: true });

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['1'])));
        const target = makeNodeItem('1', 'Same');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(reparentSpy).not.toHaveBeenCalled();
    });

    it('uses parent id "0" when dropping at root level (no target)', async () => {
        const reparentSpy = jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: true });

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['5'])));
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(undefined, dt, token);

        expect(reparentSpy).toHaveBeenCalledWith('5', '0');
    });

    it('shows error message on reparent failure', async () => {
        jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: false, error: 'circular ref' });

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['2'])));
        const target = makeNodeItem('1', 'P');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('circular ref'),
        );
    });

    it('handles invalid JSON in DataTransfer gracefully', async () => {
        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem('not json'));
        const target = makeNodeItem('1', 'P');
        const token = new vscode.CancellationTokenSource().token;

        // Should not throw
        await expect(controller.handleDrop(target, dt, token)).resolves.toBeUndefined();
    });

    it('uses asString() when DataTransfer value is not a string', async () => {
        const reparentSpy = jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: true });

        const dt = new vscode.DataTransfer();
        // Simulate non-string value (e.g. a buffer), asString() fallback
        const item = new vscode.DataTransferItem(['7'] as unknown);
        dt.set(MIME_SCENE_TREE, item);
        const target = makeNodeItem('2', 'Parent');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(reparentSpy).toHaveBeenCalledWith('7', '2');
    });

    it('uses asString() for asset data when value is not a string', async () => {
        const payload = { resPath: 'res://a.glb', type: 'Mesh' };
        const dt = new vscode.DataTransfer();
        const item = new vscode.DataTransferItem(payload as unknown);
        dt.set(MIME_ASSET, item);
        const target = makeNodeItem('3', 'N');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
            nodeId: 3,
            assetPath: 'res://a.glb',
            assetType: 'Mesh',
        });
    });

    it('shows unknown error when reparent error message is undefined', async () => {
        jest.spyOn(coordinator, 'reparentNode')
            .mockResolvedValue({ ok: false });

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['2'])));
        const target = makeNodeItem('1', 'P');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('unknown error'),
        );
    });

    it('handles non-Error exception from assignAsset', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce('string err');

        const dt = new vscode.DataTransfer();
        dt.set(MIME_ASSET, new vscode.DataTransferItem(JSON.stringify({
            resPath: 'res://x.glb', type: 'Mesh',
        })));
        const target = makeNodeItem('1', 'N');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('string err'),
        );
    });

    it('does nothing when DataTransfer is empty', async () => {
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        await expect(controller.handleDrop(makeNodeItem('1', 'A'), dt, token))
            .resolves.toBeUndefined();
    });

    it('does nothing when no coordinator is set', async () => {
        provider.setDragDropCoordinator(null as unknown as DragDropCoordinator);

        const dt = new vscode.DataTransfer();
        dt.set(MIME_SCENE_TREE, new vscode.DataTransferItem(JSON.stringify(['1'])));
        const target = makeNodeItem('2', 'P');
        const token = new vscode.CancellationTokenSource().token;

        // Should not throw
        await expect(controller.handleDrop(target, dt, token)).resolves.toBeUndefined();
    });

    // -- handleDrop (asset assignment) ----------------------------------------

    it('calls scene.assignAsset IPC when asset data is dropped on a node', async () => {
        const assetPayload = { resPath: 'res://meshes/house.glb', type: 'Mesh' };
        const dt = new vscode.DataTransfer();
        dt.set(MIME_ASSET, new vscode.DataTransferItem(JSON.stringify(assetPayload)));
        const target = makeNodeItem('5', 'MyNode');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
            nodeId: 5,
            assetPath: 'res://meshes/house.glb',
            assetType: 'Mesh',
        });
    });

    it('shows error on assignAsset IPC failure', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('not found'));

        const dt = new vscode.DataTransfer();
        dt.set(MIME_ASSET, new vscode.DataTransferItem(JSON.stringify({
            resPath: 'res://bad.glb', type: 'Mesh',
        })));
        const target = makeNodeItem('1', 'N');
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(target, dt, token);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('not found'),
        );
    });

    it('ignores asset drop with no target node', async () => {
        const dt = new vscode.DataTransfer();
        dt.set(MIME_ASSET, new vscode.DataTransferItem(JSON.stringify({
            resPath: 'res://x.glb', type: 'Mesh',
        })));
        const token = new vscode.CancellationTokenSource().token;

        await controller.handleDrop(undefined, dt, token);

        // scene.assignAsset should NOT be called (no target node)
        expect(ipc.request).not.toHaveBeenCalledWith('scene.assignAsset', expect.anything());
    });
});

// ---------------------------------------------------------------------------
// SceneTreeProvider drag-drop integration
// ---------------------------------------------------------------------------

describe('SceneTreeProvider drag-drop integration', () => {
    let ipc: IpcClient;
    let provider: SceneTreeProvider;

    beforeEach(() => {
        ipc = makeMockIpc();
        provider = new SceneTreeProvider(ipc);
    });

    afterEach(() => {
        provider.dispose();
    });

    // -- selectNodeById / findNodeById (coverage for lines 280-313) ---------

    it('selectNodeById with null clears selection', async () => {
        const selectSpy = jest.spyOn(provider, 'selectNode').mockResolvedValue(undefined);
        await provider.selectNodeById(null);
        expect(selectSpy).toHaveBeenCalledWith(undefined);
    });

    it('selectNodeById finds node in cached tree and selects it', async () => {
        provider.loadFromFile([
            { id: '1', name: 'Root', type: 'Node3D', children: [
                { id: '2', name: 'Child', type: 'StaticMesh' },
            ] },
        ]);

        const selectSpy = jest.spyOn(provider, 'selectNode').mockResolvedValue(undefined);
        await provider.selectNodeById(2);

        expect(selectSpy).toHaveBeenCalledWith(expect.objectContaining({ nodeId: '2' }));
    });

    it('selectNodeById does nothing when node not found', async () => {
        provider.loadFromFile([{ id: '1', name: 'Root', type: 'Node3D' }]);
        const selectSpy = jest.spyOn(provider, 'selectNode').mockResolvedValue(undefined);

        await provider.selectNodeById(999);

        expect(selectSpy).not.toHaveBeenCalled();
    });

    it('selectNodeById does nothing when disposed', async () => {
        provider.dispose();
        // After dispose, selectNodeById should early-return.
        // No spy needed — just confirm no throw.
        await expect(provider.selectNodeById(1)).resolves.toBeUndefined();
    });

    it('findNodeById returns NodeItem for existing id', () => {
        provider.loadFromFile([
            { id: '10', name: 'A', type: 'Node3D', children: [
                { id: '20', name: 'B', type: 'Camera' },
            ] },
        ]);

        const result = provider.findNodeById('20');
        expect(result).toBeDefined();
        expect(result!.nodeId).toBe('20');
    });

    it('findNodeById returns undefined for missing id', () => {
        provider.loadFromFile([{ id: '1', name: 'A', type: 'Node3D' }]);
        expect(provider.findNodeById('999')).toBeUndefined();
    });

    // -- coordinator / IPC ---------------------------------------------------

    it('setDragDropCoordinator / getCoordinator round-trips', () => {
        const coordinator = new DragDropCoordinator(ipc);
        provider.setDragDropCoordinator(coordinator);
        expect(provider.getCoordinator()).toBe(coordinator);
        coordinator.dispose();
    });

    it('getCoordinator returns null when none set', () => {
        expect(provider.getCoordinator()).toBeNull();
    });

    it('getIpc returns the injected IPC client', () => {
        expect(provider.getIpc()).toBe(ipc);
    });

    it('dragController is an instance of SceneTreeDragController', () => {
        expect(provider.dragController).toBeInstanceOf(SceneTreeDragController);
    });
});
