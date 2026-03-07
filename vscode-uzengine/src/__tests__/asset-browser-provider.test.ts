/**
 * asset-browser-provider.test.ts — Tests for AssetBrowserProvider,
 * AssetBrowserDragController, AssetGroupItem, and AssetItem.
 */

import { jest } from '@jest/globals';
import * as vscode from 'vscode';
import {
    AssetBrowserDragController,
    AssetBrowserProvider,
    AssetGroupItem,
    AssetItem,
    AssetListEntry,
    AssetTreeItem,
} from '../asset-browser-provider';
import { DragDropCoordinator } from '../editor/dragDropCoordinator';
import { MIME_ASSET } from '../editor/dragDropTypes';
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

function makeAssets(): AssetListEntry[] {
    return [
        { path: 'meshes/cube.glb', type: 'Mesh', status: 'Fresh' },
        { path: 'meshes/sphere.glb', type: 'Mesh', status: 'Stale' },
        { path: 'textures/grass.png', type: 'Texture', status: 'Fresh' },
        { path: 'scenes/level1.uzscene', type: 'Scene', status: 'NotImported' },
        { path: 'materials/stone.uzmat', type: 'Material', status: 'Error' },
        { path: 'shaders/unlit.hlsl', type: 'Shader', status: 'Fresh' },
        { path: 'audio/click.wav', type: 'Audio', status: 'Fresh' },
    ];
}

// ---------------------------------------------------------------------------
// AssetGroupItem tests
// ---------------------------------------------------------------------------

describe('AssetGroupItem', () => {
    it('creates with correct label and collapsible state', () => {
        const item = new AssetGroupItem('Mesh', 5);
        expect(item.label).toBe('Meshes (5)');
        expect(item.collapsibleState).toBe(vscode.TreeItemCollapsibleState.Collapsed);
        expect(item.contextValue).toBe('assetGroup');
        expect(item.groupKey).toBe('Mesh');
        expect(item.count).toBe(5);
    });

    it('uses correct icon for each group', () => {
        const groups: Array<{ key: 'Scene' | 'Mesh' | 'Texture' | 'Material' | 'Shader' | 'Audio'; icon: string }> = [
            { key: 'Scene', icon: 'file-code' },
            { key: 'Mesh', icon: 'package' },
            { key: 'Texture', icon: 'file-media' },
            { key: 'Material', icon: 'symbol-color' },
            { key: 'Shader', icon: 'code' },
            { key: 'Audio', icon: 'unmute' },
        ];
        for (const { key, icon } of groups) {
            const item = new AssetGroupItem(key, 1);
            expect(item.iconPath).toBeInstanceOf(vscode.ThemeIcon);
            expect((item.iconPath as vscode.ThemeIcon).id).toBe(icon);
        }
    });
});

// ---------------------------------------------------------------------------
// AssetItem tests
// ---------------------------------------------------------------------------

describe('AssetItem', () => {
    it('creates with correct display name from path', () => {
        const item = new AssetItem('meshes/cube.glb', 'Mesh', 'Fresh');
        expect(item.label).toBe('cube.glb');
        expect(item.assetPath).toBe('meshes/cube.glb');
        expect(item.assetType).toBe('Mesh');
        expect(item.importStatus).toBe('Fresh');
        expect(item.collapsibleState).toBe(vscode.TreeItemCollapsibleState.None);
        expect(item.contextValue).toBe('assetItem');
    });

    it('shows status icon in description', () => {
        expect(new AssetItem('a.glb', 'Mesh', 'Fresh').description).toContain('$(check)');
        expect(new AssetItem('a.glb', 'Mesh', 'Stale').description).toContain('$(warning)');
        expect(new AssetItem('a.glb', 'Mesh', 'Error').description).toContain('$(error)');
        expect(new AssetItem('a.glb', 'Mesh', 'NotImported').description).toContain('$(circle-outline)');
    });

    it('includes tooltip with full path and status', () => {
        const item = new AssetItem('textures/grass.png', 'Texture', 'Stale');
        expect(item.tooltip).toContain('textures/grass.png');
        expect(item.tooltip).toContain('Stale');
    });

    it('has a reveal command', () => {
        const item = new AssetItem('a.glb', 'Mesh', 'Fresh');
        expect(item.command).toBeDefined();
        expect(item.command!.command).toBe('uzengine.assetBrowser.reveal');
    });

    it('handles path with no slashes', () => {
        const item = new AssetItem('file.glb', 'Mesh', 'Fresh');
        expect(item.label).toBe('file.glb');
    });
});

// ---------------------------------------------------------------------------
// AssetBrowserProvider tests
// ---------------------------------------------------------------------------

describe('AssetBrowserProvider', () => {
    let ipc: IpcClient;
    let provider: AssetBrowserProvider;

    beforeEach(() => {
        ipc = makeMockIpc();
        provider = new AssetBrowserProvider(ipc);
    });

    afterEach(() => {
        provider.dispose();
    });

    // -- Basic API ------------------------------------------------------------

    it('has a dragController', () => {
        expect(provider.dragController).toBeInstanceOf(AssetBrowserDragController);
    });

    it('getIpc returns the injected IPC client', () => {
        expect(provider.getIpc()).toBe(ipc);
    });

    it('getCoordinator returns null when none set', () => {
        expect(provider.getCoordinator()).toBeNull();
    });

    it('setDragDropCoordinator / getCoordinator round-trips', () => {
        const coordinator = new DragDropCoordinator(ipc);
        provider.setDragDropCoordinator(coordinator);
        expect(provider.getCoordinator()).toBe(coordinator);
        coordinator.dispose();
    });

    it('getAssets returns empty initially', () => {
        expect(provider.getAssets()).toEqual([]);
    });

    // -- loadAssets (sync) ----------------------------------------------------

    it('loadAssets populates the tree', () => {
        provider.loadAssets(makeAssets());
        expect(provider.getAssets()).toHaveLength(7);
    });

    it('fires onDidChangeTreeData on loadAssets', () => {
        const fired: unknown[] = [];
        provider.onDidChangeTreeData(() => fired.push(true));
        provider.loadAssets(makeAssets());
        expect(fired).toHaveLength(1);
    });

    // -- getChildren (root level) ---------------------------------------------

    it('returns group items at root level', () => {
        provider.loadAssets(makeAssets());
        const roots = provider.getChildren();
        expect(roots.length).toBe(6); // all 6 groups have assets
        expect(roots.every(r => r instanceof AssetGroupItem)).toBe(true);
    });

    it('groups have correct counts', () => {
        provider.loadAssets(makeAssets());
        const roots = provider.getChildren() as AssetGroupItem[];
        const meshGroup = roots.find(g => g.groupKey === 'Mesh')!;
        expect(meshGroup.count).toBe(2);
    });

    it('omits empty groups', () => {
        provider.loadAssets([{ path: 'a.glb', type: 'Mesh', status: 'Fresh' }]);
        const roots = provider.getChildren();
        expect(roots).toHaveLength(1);
        expect((roots[0] as AssetGroupItem).groupKey).toBe('Mesh');
    });

    it('returns empty array at root when no assets', () => {
        expect(provider.getChildren()).toEqual([]);
    });

    it('returns empty when disposed', () => {
        provider.loadAssets(makeAssets());
        provider.dispose();
        expect(provider.getChildren()).toEqual([]);
    });

    // -- getChildren (group level) --------------------------------------------

    it('returns asset items for a group', () => {
        provider.loadAssets(makeAssets());
        const roots = provider.getChildren() as AssetGroupItem[];
        const meshGroup = roots.find(g => g.groupKey === 'Mesh')!;
        const children = provider.getChildren(meshGroup);
        expect(children).toHaveLength(2);
        expect(children.every(c => c instanceof AssetItem)).toBe(true);
    });

    it('returns empty for unknown group key', () => {
        provider.loadAssets(makeAssets());
        // Simulate group with key that has no entries
        const fakeGroup = new AssetGroupItem('Audio', 0);
        provider.loadAssets([]);
        expect(provider.getChildren(fakeGroup)).toEqual([]);
    });

    // -- getChildren (leaf level) ---------------------------------------------

    it('returns empty for a leaf AssetItem', () => {
        const item = new AssetItem('a.glb', 'Mesh', 'Fresh');
        expect(provider.getChildren(item)).toEqual([]);
    });

    // -- getTreeItem ----------------------------------------------------------

    it('returns the element itself', () => {
        const item = new AssetItem('a.glb', 'Mesh', 'Fresh');
        expect(provider.getTreeItem(item)).toBe(item);
    });

    // -- refresh (IPC) --------------------------------------------------------

    it('fetches assets from IPC on refresh', async () => {
        const assets = makeAssets();
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({
            ok: true,
            assets,
        });

        await provider.refresh();
        expect(ipc.request).toHaveBeenCalledWith('asset.list', {});
        expect(provider.getAssets()).toHaveLength(7);
    });

    it('fires onDidChangeTreeData after refresh', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({
            ok: true,
            assets: [],
        });

        const fired: unknown[] = [];
        provider.onDidChangeTreeData(() => fired.push(true));
        await provider.refresh();
        expect(fired).toHaveLength(1);
    });

    it('clears assets on IPC failure', async () => {
        provider.loadAssets(makeAssets());
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('timeout'));

        await provider.refresh();
        expect(provider.getAssets()).toEqual([]);
    });

    it('clears assets when response has no assets field', async () => {
        provider.loadAssets(makeAssets());
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: true });

        await provider.refresh();
        expect(provider.getAssets()).toEqual([]);
    });

    it('refresh is a no-op when disposed', async () => {
        provider.dispose();
        await provider.refresh();
        expect(ipc.request).not.toHaveBeenCalled();
    });

    // -- reimportAsset --------------------------------------------------------

    it('calls asset.import IPC and refreshes on success', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>)
            .mockResolvedValueOnce({ ok: true })         // reimport
            .mockResolvedValueOnce({ ok: true, assets: [] }); // refresh

        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);

        expect(ipc.request).toHaveBeenCalledWith('asset.import', { path: 'a.glb' });
    });

    it('shows error on reimport failure (ok=false)', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({
            ok: false,
            error: 'file corrupt',
        });

        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('file corrupt'),
        );
    });

    it('shows unknown error when reimport error field is missing', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({
            ok: false,
        });

        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('unknown error'),
        );
    });

    it('shows error on reimport IPC exception', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('network'));

        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('network'),
        );
    });

    it('shows error on reimport non-Error exception', async () => {
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce('string err');

        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);

        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('string err'),
        );
    });

    it('reimportAsset is a no-op when disposed', async () => {
        provider.dispose();
        const item = new AssetItem('a.glb', 'Mesh', 'Stale');
        await provider.reimportAsset(item);
        expect(ipc.request).not.toHaveBeenCalled();
    });

    // -- createMaterial -------------------------------------------------------

    it('creates material via IPC, refreshes, and opens file', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Stone' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>)
            .mockResolvedValueOnce({ ok: true, path: 'Stone.uzmat' })  // create
            .mockResolvedValueOnce({ ok: true, assets: [] });          // refresh

        const result = await provider.createMaterial();
        expect(result).toBe('Stone.uzmat');
        expect(ipc.request).toHaveBeenCalledWith('asset.createMaterial', { name: 'Stone' });
        expect(vscode.commands.executeCommand).toHaveBeenCalledWith(
            'vscode.open',
            expect.objectContaining({ fsPath: expect.stringContaining('Stone.uzmat') }),
        );
    });

    it('createMaterial returns undefined when input is cancelled', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce(undefined as never);

        const result = await provider.createMaterial();
        expect(result).toBeUndefined();
        expect(ipc.request).not.toHaveBeenCalled();
    });

    it('createMaterial shows error on IPC failure', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Bad' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({
            ok: false,
            error: 'disk full',
        });

        const result = await provider.createMaterial();
        expect(result).toBeUndefined();
        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('disk full'),
        );
    });

    it('createMaterial shows error on IPC exception', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Bad' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(new Error('timeout'));

        const result = await provider.createMaterial();
        expect(result).toBeUndefined();
        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('timeout'),
        );
    });

    it('createMaterial shows error on non-Error exception', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Bad' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockRejectedValueOnce(42);

        const result = await provider.createMaterial();
        expect(result).toBeUndefined();
        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('42'),
        );
    });

    it('createMaterial is a no-op when disposed', async () => {
        provider.dispose();
        const result = await provider.createMaterial();
        expect(result).toBeUndefined();
    });

    it('createMaterial shows unknown error when error field is missing', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Bad' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>).mockResolvedValueOnce({ ok: false });

        await provider.createMaterial();
        expect(vscode.window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('unknown error'),
        );
    });

    // -- createScene ----------------------------------------------------------

    it('creates scene via IPC', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Level2' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>)
            .mockResolvedValueOnce({ ok: true, path: 'Level2.uzscene' })
            .mockResolvedValueOnce({ ok: true, assets: [] });

        const result = await provider.createScene();
        expect(result).toBe('Level2.uzscene');
        expect(ipc.request).toHaveBeenCalledWith('asset.createScene', { name: 'Level2' });
    });

    it('createScene returns undefined when input cancelled', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce(undefined as never);

        const result = await provider.createScene();
        expect(result).toBeUndefined();
    });

    it('createScene is a no-op when disposed', async () => {
        provider.dispose();
        const result = await provider.createScene();
        expect(result).toBeUndefined();
    });

    // -- createShader ---------------------------------------------------------

    it('creates shader via IPC', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce('Toon' as never);
        (ipc.request as jest.MockedFunction<IpcClient['request']>)
            .mockResolvedValueOnce({ ok: true, path: 'Toon.hlsl' })
            .mockResolvedValueOnce({ ok: true, assets: [] });

        const result = await provider.createShader();
        expect(result).toBe('Toon.hlsl');
        expect(ipc.request).toHaveBeenCalledWith('asset.createShader', { name: 'Toon' });
    });

    it('createShader returns undefined when input cancelled', async () => {
        (vscode.window.showInputBox as jest.MockedFunction<typeof vscode.window.showInputBox>)
            .mockResolvedValueOnce(undefined as never);

        const result = await provider.createShader();
        expect(result).toBeUndefined();
    });

    it('createShader is a no-op when disposed', async () => {
        provider.dispose();
        const result = await provider.createShader();
        expect(result).toBeUndefined();
    });

    // -- Unknown type grouping ------------------------------------------------

    it('ignores assets with unknown type', () => {
        provider.loadAssets([
            { path: 'readme.txt', type: 'Unknown', status: 'Fresh' },
            { path: 'cube.glb', type: 'Mesh', status: 'Fresh' },
        ]);
        const roots = provider.getChildren();
        expect(roots).toHaveLength(1);
        expect((roots[0] as AssetGroupItem).groupKey).toBe('Mesh');
    });

    // -- GROUP_ORDER ----------------------------------------------------------

    it('groups appear in correct order', () => {
        provider.loadAssets(makeAssets());
        const roots = provider.getChildren() as AssetGroupItem[];
        const keys = roots.map(r => r.groupKey);
        expect(keys).toEqual(['Scene', 'Mesh', 'Texture', 'Material', 'Shader', 'Audio']);
    });

    // -- dispose --------------------------------------------------------------

    it('clears state on dispose', () => {
        provider.loadAssets(makeAssets());
        provider.dispose();
        expect(provider.getAssets()).toEqual([]);
        expect(provider.getChildren()).toEqual([]);
    });

    it('double dispose is safe', () => {
        provider.dispose();
        expect(() => provider.dispose()).not.toThrow();
    });
});

// ---------------------------------------------------------------------------
// AssetBrowserDragController tests
// ---------------------------------------------------------------------------

describe('AssetBrowserDragController', () => {
    let ipc: IpcClient;
    let provider: AssetBrowserProvider;
    let controller: AssetBrowserDragController;

    beforeEach(() => {
        ipc = makeMockIpc();
        provider = new AssetBrowserProvider(ipc);
        controller = provider.dragController;
    });

    afterEach(() => {
        provider.dispose();
    });

    it('exposes correct dragMimeTypes', () => {
        expect(controller.dragMimeTypes).toContain(MIME_ASSET);
    });

    it('exposes empty dropMimeTypes', () => {
        expect(controller.dropMimeTypes).toEqual([]);
    });

    it('serialises first AssetItem into DataTransfer', () => {
        const items: AssetTreeItem[] = [
            new AssetItem('meshes/cube.glb', 'Mesh', 'Fresh'),
            new AssetItem('meshes/sphere.glb', 'Mesh', 'Fresh'),
        ];
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag(items, dt, token);

        const data = dt.get(MIME_ASSET);
        expect(data).toBeDefined();
        const payload = JSON.parse(data!.value as string);
        expect(payload.resPath).toBe('res://meshes/cube.glb');
        expect(payload.type).toBe('Mesh');
        expect(payload.displayName).toBe('cube.glb');
    });

    it('does nothing for empty source array', () => {
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag([], dt, token);
        expect(dt.get(MIME_ASSET)).toBeUndefined();
    });

    it('skips AssetGroupItem in source', () => {
        const items: AssetTreeItem[] = [
            new AssetGroupItem('Mesh', 3),
        ];
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag(items, dt, token);
        expect(dt.get(MIME_ASSET)).toBeUndefined();
    });

    it('calls coordinator.startDrag when coordinator is set', () => {
        const coordinator = new DragDropCoordinator(ipc);
        provider.setDragDropCoordinator(coordinator);
        const startSpy = jest.spyOn(coordinator, 'startDrag');

        const items: AssetTreeItem[] = [
            new AssetItem('textures/grass.png', 'Texture', 'Fresh'),
        ];
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        controller.handleDrag(items, dt, token);

        expect(startSpy).toHaveBeenCalledWith(expect.objectContaining({
            resPath: 'res://textures/grass.png',
            type: 'Texture',
        }));

        coordinator.dispose();
    });

    it('does not call coordinator when none is set', () => {
        const items: AssetTreeItem[] = [
            new AssetItem('a.glb', 'Mesh', 'Fresh'),
        ];
        const dt = new vscode.DataTransfer();
        const token = new vscode.CancellationTokenSource().token;

        // Should not throw
        expect(() => controller.handleDrag(items, dt, token)).not.toThrow();
    });

    it('maps asset types correctly', () => {
        const types = [
            { engine: 'Mesh', expected: 'Mesh' },
            { engine: 'Material', expected: 'Material' },
            { engine: 'Texture', expected: 'Texture' },
            { engine: 'Audio', expected: 'AudioClip' },
            { engine: 'Shader', expected: 'Script' },
            { engine: 'Unknown', expected: 'Mesh' },
        ];

        for (const { engine, expected } of types) {
            const dt = new vscode.DataTransfer();
            const token = new vscode.CancellationTokenSource().token;
            controller.handleDrag([new AssetItem('a', engine, 'Fresh')], dt, token);
            const payload = JSON.parse(dt.get(MIME_ASSET)!.value as string);
            expect(payload.type).toBe(expected);
        }
    });

    it('handleDrop is a no-op', () => {
        // Just confirm it exists and doesn't throw.
        expect(() => controller.handleDrop()).not.toThrow();
    });
});
