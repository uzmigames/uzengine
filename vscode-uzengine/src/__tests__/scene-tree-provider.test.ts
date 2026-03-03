/**
 * scene-tree-provider.test.ts — Unit tests for SceneTreeProvider and NodeItem.
 *
 * Tests cover:
 *  - NodeItem construction: label, contextValue, tooltip, description, icon
 *  - NodeItem icons for all NodeType values
 *  - NodeItem command property
 *  - NodeItem children from SceneNodeData
 *  - Collapsible state: collapsed when has children, none when leaf
 *  - getTreeItem() returns the element unchanged
 *  - getChildren(undefined) triggers IPC load on first call
 *  - getChildren(undefined) returns root NodeItems after load
 *  - getChildren(item) returns child NodeItems from cache
 *  - getChildren() returns [] when disposed
 *  - scene.treeChanged push event triggers a re-load
 *  - Refresh clears loaded flag and re-fetches
 *  - selectNode() sends scene.selectNode IPC and fires onNodeSelected
 *  - selectNode(undefined) fires onNodeSelected with undefined
 *  - selectNode() swallows IPC errors
 *  - addNode() sends scene.addNode IPC
 *  - addNode() shows error message on failure
 *  - deleteNode() sends scene.deleteNode IPC
 *  - deleteNode() shows error message on failure
 *  - duplicateNode() sends scene.duplicateNode IPC
 *  - duplicateNode() shows error message on failure
 *  - renameNode() prompts for name and sends scene.renameNode IPC
 *  - renameNode() does nothing when user cancels (returns undefined)
 *  - renameNode() does nothing when user enters empty string
 *  - renameNode() trims whitespace from new name
 *  - renameNode() shows error message on IPC failure
 *  - IPC failure during load results in empty tree (not a crash)
 *  - dispose() cleans up subscriptions and event emitters
 */

import { SceneTreeProvider, NodeItem, NodeType, SceneNodeData } from '../scene-tree-provider';
import { IpcClient, IpcClientError } from '../ipc-client';
import { window } from '../__mocks__/vscode';

// ---------------------------------------------------------------------------
// Mock IpcClient
// ---------------------------------------------------------------------------

function makeMockIpc(): {
    client: IpcClient;
    request: jest.Mock;
    on: jest.Mock;
    unsubscribers: Map<string, (() => void)[]>;
    triggerEvent(eventType: string, data: Record<string, unknown>): void;
} {
    const unsubscribers: Map<string, (() => void)[]> = new Map();
    const eventHandlers: Map<string, Array<(data: Record<string, unknown>) => void>> = new Map();

    const request = jest.fn().mockResolvedValue({ nodes: [] });
    const on = jest.fn((eventType: string, handler: (data: Record<string, unknown>) => void) => {
        if (!eventHandlers.has(eventType)) {
            eventHandlers.set(eventType, []);
        }
        eventHandlers.get(eventType)!.push(handler);
        const unsub = jest.fn(() => {
            const arr = eventHandlers.get(eventType) ?? [];
            const idx = arr.indexOf(handler);
            if (idx !== -1) { arr.splice(idx, 1); }
        });
        if (!unsubscribers.has(eventType)) {
            unsubscribers.set(eventType, []);
        }
        unsubscribers.get(eventType)!.push(unsub);
        return unsub;
    });

    const client = { request, on } as unknown as IpcClient;

    function triggerEvent(eventType: string, data: Record<string, unknown>): void {
        const handlers = eventHandlers.get(eventType) ?? [];
        handlers.forEach(h => h(data));
    }

    return { client, request, on, unsubscribers, triggerEvent };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeNode(overrides: Partial<SceneNodeData> = {}): SceneNodeData {
    return {
        id: overrides.id ?? 'node-1',
        name: overrides.name ?? 'Root',
        type: overrides.type ?? 'Node3D',
        children: overrides.children,
    };
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// NodeItem tests
// ---------------------------------------------------------------------------

describe('NodeItem', () => {
    it('sets label, nodeId, nodeName, nodeType from SceneNodeData', () => {
        const data: SceneNodeData = { id: 'n1', name: 'Player', type: 'Camera' };
        const item = new NodeItem(data);

        expect(item.label).toBe('Player');
        expect(item.nodeId).toBe('n1');
        expect(item.nodeName).toBe('Player');
        expect(item.nodeType).toBe('Camera');
    });

    it('sets contextValue to "sceneNode"', () => {
        const item = new NodeItem(makeNode());
        expect(item.contextValue).toBe('sceneNode');
    });

    it('sets tooltip to "type [id]"', () => {
        const item = new NodeItem({ id: 'abc', name: 'Cube', type: 'StaticMesh' });
        expect(item.tooltip).toBe('StaticMesh [abc]');
    });

    it('sets description to the node type', () => {
        const item = new NodeItem(makeNode({ type: 'PointLight' }));
        expect(item.description).toBe('PointLight');
    });

    it('sets collapsibleState to Collapsed when node has children', () => {
        const item = new NodeItem({
            id: 'p',
            name: 'Parent',
            type: 'Node3D',
            children: [{ id: 'c', name: 'Child', type: 'Node3D' }],
        });
        // TreeItemCollapsibleState.Collapsed = 1
        expect(item.collapsibleState).toBe(1);
    });

    it('sets collapsibleState to None when node has no children', () => {
        const item = new NodeItem(makeNode({ children: [] }));
        // TreeItemCollapsibleState.None = 0
        expect(item.collapsibleState).toBe(0);
    });

    it('sets collapsibleState to None when children is undefined', () => {
        const item = new NodeItem(makeNode({ children: undefined }));
        expect(item.collapsibleState).toBe(0);
    });

    it('stores children array', () => {
        const childData: SceneNodeData = { id: 'c1', name: 'Child', type: 'Camera' };
        const item = new NodeItem({ id: 'p', name: 'Parent', type: 'Node3D', children: [childData] });
        expect(item.children).toHaveLength(1);
        expect(item.children[0]).toBe(childData);
    });

    it('has command uzengine.selectNode with the item as argument', () => {
        const item = new NodeItem(makeNode());
        expect(item.command?.command).toBe('uzengine.selectNode');
        expect(item.command?.arguments?.[0]).toBe(item);
    });

    describe('iconForType()', () => {
        const cases: Array<[NodeType, string]> = [
            ['Node3D',          'symbol-namespace'],
            ['StaticMesh',      'symbol-field'],
            ['DirectionalLight','symbol-event'],
            ['PointLight',      'symbol-event'],
            ['Camera',          'device-camera'],
            ['AudioSource',     'unmute'],
            ['Unknown',         'symbol-misc'],
        ];

        for (const [type, expectedId] of cases) {
            it(`returns correct icon for ${type}`, () => {
                const icon = NodeItem.iconForType(type);
                // ThemeIcon has an id property
                expect((icon as { id: string }).id).toBe(expectedId);
            });
        }
    });
});

// ---------------------------------------------------------------------------
// SceneTreeProvider tests
// ---------------------------------------------------------------------------

describe('SceneTreeProvider', () => {
    describe('construction', () => {
        it('subscribes to scene.treeChanged push event', () => {
            const { client, on } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            expect(on).toHaveBeenCalledWith('scene.treeChanged', expect.any(Function));
            provider.dispose();
        });
    });

    describe('getTreeItem()', () => {
        it('returns the element unchanged', () => {
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode());
            expect(provider.getTreeItem(item)).toBe(item);
            provider.dispose();
        });
    });

    describe('getChildren(undefined) — root level', () => {
        it('returns [] initially and triggers IPC load', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({ nodes: [makeNode({ id: 'r1', name: 'Root' })] });

            const provider = new SceneTreeProvider(client);

            const result = provider.getChildren(undefined);
            expect(Array.isArray(result)).toBe(true);
            expect((result as unknown[]).length).toBe(0);

            // Wait for the async load to complete.
            await new Promise(r => setTimeout(r, 20));

            expect(request).toHaveBeenCalledWith('scene.getTree', {});
            provider.dispose();
        });

        it('returns root NodeItems after IPC load completes', async () => {
            const rootNodes: SceneNodeData[] = [
                makeNode({ id: 'r1', name: 'Scene Root', type: 'Node3D' }),
                makeNode({ id: 'r2', name: 'Camera', type: 'Camera' }),
            ];
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({ nodes: rootNodes });

            const provider = new SceneTreeProvider(client);

            // First call triggers load.
            provider.getChildren(undefined);
            await new Promise(r => setTimeout(r, 20));

            // Second call uses cached nodes.
            const items = provider.getChildren(undefined) as NodeItem[];
            expect(items).toHaveLength(2);
            expect(items[0].nodeId).toBe('r1');
            expect(items[1].nodeId).toBe('r2');

            provider.dispose();
        });

        it('returns [] after IPC failure without crashing', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new IpcClientError('Engine down', 'scene.getTree', 'x'));

            const provider = new SceneTreeProvider(client);

            provider.getChildren(undefined);
            await new Promise(r => setTimeout(r, 20));

            const items = provider.getChildren(undefined) as NodeItem[];
            expect(items).toHaveLength(0);

            provider.dispose();
        });

        it('returns [] when disposed', () => {
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            provider.dispose();

            const result = provider.getChildren(undefined);
            expect(result).toEqual([]);
        });
    });

    describe('getChildren(item) — child level', () => {
        it('returns child NodeItems for a parent item', () => {
            const childData: SceneNodeData = { id: 'c1', name: 'Child', type: 'Camera' };
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);

            const parent = new NodeItem({
                id: 'p1',
                name: 'Parent',
                type: 'Node3D',
                children: [childData],
            });

            const children = provider.getChildren(parent) as NodeItem[];
            expect(children).toHaveLength(1);
            expect(children[0].nodeId).toBe('c1');
            expect(children[0].nodeName).toBe('Child');

            provider.dispose();
        });

        it('returns [] for a leaf node', () => {
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            const leaf = new NodeItem(makeNode({ children: [] }));

            const children = provider.getChildren(leaf) as NodeItem[];
            expect(children).toHaveLength(0);

            provider.dispose();
        });
    });

    describe('scene.treeChanged push event', () => {
        it('triggers a re-fetch and fires onDidChangeTreeData', async () => {
            const { client, request, triggerEvent } = makeMockIpc();

            let version = 1;
            request.mockImplementation(() => Promise.resolve({
                nodes: [makeNode({ id: `v${version++}` })],
            }));

            const provider = new SceneTreeProvider(client);

            // Initial load.
            provider.getChildren(undefined);
            await new Promise(r => setTimeout(r, 20));

            const changes: Array<unknown> = [];
            provider.onDidChangeTreeData(() => changes.push(1));

            // Trigger push event.
            triggerEvent('scene.treeChanged', {});
            await new Promise(r => setTimeout(r, 20));

            expect(request).toHaveBeenCalledTimes(2);
            expect(changes.length).toBeGreaterThanOrEqual(1);

            provider.dispose();
        });
    });

    describe('refresh()', () => {
        it('re-fetches the tree and fires onDidChangeTreeData', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({ nodes: [makeNode()] });

            const provider = new SceneTreeProvider(client);

            const changes: number[] = [];
            provider.onDidChangeTreeData(() => changes.push(1));

            // Initial load.
            provider.getChildren(undefined);
            await new Promise(r => setTimeout(r, 20));
            const countAfterLoad = changes.length;

            provider.refresh();
            await new Promise(r => setTimeout(r, 20));

            expect(changes.length).toBeGreaterThan(countAfterLoad);
            expect(request).toHaveBeenCalledTimes(2);

            provider.dispose();
        });

        it('does nothing when disposed', () => {
            const { client, request } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            provider.dispose();

            provider.refresh(); // Should not throw.
            expect(request).not.toHaveBeenCalled();
        });
    });

    describe('selectNode()', () => {
        it('sends scene.selectNode IPC and fires onNodeSelected', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'n1' }));

            const selected: Array<NodeItem | undefined> = [];
            provider.onNodeSelected(n => selected.push(n));

            await provider.selectNode(item);

            expect(request).toHaveBeenCalledWith('scene.selectNode', { id: 'n1' });
            expect(selected).toHaveLength(1);
            expect(selected[0]).toBe(item);

            provider.dispose();
        });

        it('fires onNodeSelected with undefined when item is undefined', async () => {
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);

            const selected: Array<NodeItem | undefined> = [];
            provider.onNodeSelected(n => selected.push(n));

            await provider.selectNode(undefined);

            expect(selected).toHaveLength(1);
            expect(selected[0]).toBeUndefined();

            provider.dispose();
        });

        it('swallows IPC errors and still fires onNodeSelected', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new IpcClientError('Disconnected', 'scene.selectNode', 'x'));

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'n1' }));

            const selected: Array<NodeItem | undefined> = [];
            provider.onNodeSelected(n => selected.push(n));

            await expect(provider.selectNode(item)).resolves.toBeUndefined();
            expect(selected).toHaveLength(1);
            expect(selected[0]).toBe(item);

            provider.dispose();
        });

        it('does nothing when disposed', async () => {
            const { client, request } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode());

            provider.dispose();
            await provider.selectNode(item); // Should not throw.

            // The ipc.request is only called for selectNode when not disposed.
            // Since provider is disposed, selectNode should return early.
            expect(request).not.toHaveBeenCalledWith('scene.selectNode', expect.anything());
        });
    });

    describe('addNode()', () => {
        it('sends scene.addNode IPC with parentId and type', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            const parent = new NodeItem(makeNode({ id: 'parent-1' }));

            await provider.addNode(parent, 'Camera');

            expect(request).toHaveBeenCalledWith('scene.addNode', {
                parentId: 'parent-1',
                type: 'Camera',
            });

            provider.dispose();
        });

        it('sends parentId=null when no parent given', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            await provider.addNode(undefined);

            expect(request).toHaveBeenCalledWith('scene.addNode', {
                parentId: null,
                type: 'Node3D',
            });

            provider.dispose();
        });

        it('shows error message when IPC fails', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new Error('Engine error'));

            const provider = new SceneTreeProvider(client);
            await provider.addNode(undefined);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to add node')
            );

            provider.dispose();
        });

        it('shows String(err) when IPC rejects with non-Error (addNode)', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue('raw add error');

            const provider = new SceneTreeProvider(client);
            await provider.addNode(undefined);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw add error')
            );

            provider.dispose();
        });
    });

    describe('deleteNode()', () => {
        it('sends scene.deleteNode IPC', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'del-1' }));

            await provider.deleteNode(item);

            expect(request).toHaveBeenCalledWith('scene.deleteNode', { id: 'del-1' });

            provider.dispose();
        });

        it('shows error message when IPC fails', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new Error('Not found'));

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'del-1' }));

            await provider.deleteNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to delete node')
            );

            provider.dispose();
        });

        it('shows String(err) when IPC rejects with non-Error (deleteNode)', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue('raw delete error');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'del-1' }));
            await provider.deleteNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw delete error')
            );

            provider.dispose();
        });
    });

    describe('duplicateNode()', () => {
        it('sends scene.duplicateNode IPC', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'dup-1' }));

            await provider.duplicateNode(item);

            expect(request).toHaveBeenCalledWith('scene.duplicateNode', { id: 'dup-1' });

            provider.dispose();
        });

        it('shows error message when IPC fails', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new Error('Dupe failed'));

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'dup-1' }));

            await provider.duplicateNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to duplicate node')
            );

            provider.dispose();
        });

        it('shows String(err) when IPC rejects with non-Error (duplicateNode)', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue('raw dupe error');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'dup-1' }));
            await provider.duplicateNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw dupe error')
            );

            provider.dispose();
        });
    });

    describe('renameNode()', () => {
        it('shows input box with current name and sends scene.renameNode IPC', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue('NewName');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));

            await provider.renameNode(item);

            expect(mockWindow.showInputBox).toHaveBeenCalledWith(
                expect.objectContaining({ value: 'OldName' })
            );
            expect(request).toHaveBeenCalledWith('scene.renameNode', { id: 'r1', name: 'NewName' });

            provider.dispose();
        });

        it('does nothing when user cancels (returns undefined)', async () => {
            const { client, request } = makeMockIpc();
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue(undefined);

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));

            await provider.renameNode(item);

            expect(request).not.toHaveBeenCalledWith('scene.renameNode', expect.anything());
            provider.dispose();
        });

        it('does nothing when user enters empty string', async () => {
            const { client, request } = makeMockIpc();
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue('   ');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));

            await provider.renameNode(item);

            expect(request).not.toHaveBeenCalledWith('scene.renameNode', expect.anything());
            provider.dispose();
        });

        it('trims whitespace from the new name', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue('  TrimmedName  ');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));

            await provider.renameNode(item);

            expect(request).toHaveBeenCalledWith('scene.renameNode', { id: 'r1', name: 'TrimmedName' });
            provider.dispose();
        });

        it('shows error message when IPC fails', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue(new Error('Rename failed'));

            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue('NewName');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));

            await provider.renameNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Failed to rename node')
            );
            provider.dispose();
        });

        it('shows String(err) when IPC rejects with non-Error (renameNode)', async () => {
            const { client, request } = makeMockIpc();
            request.mockRejectedValue('raw rename error');

            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockResolvedValue('NewName');

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));
            await provider.renameNode(item);

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('raw rename error')
            );
            provider.dispose();
        });
    });

    describe('renameNode() — validateInput callback', () => {
        it('validateInput returns error when value is empty', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            let capturedValidator: ((v: string) => string | undefined) | undefined;
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockImplementation((opts: { validateInput?: (v: string) => string | undefined }) => {
                capturedValidator = opts.validateInput;
                return Promise.resolve('ValidName');
            });

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));
            await provider.renameNode(item);

            expect(capturedValidator).toBeDefined();
            expect(capturedValidator!('')).toBe('Node name cannot be empty');
            expect(capturedValidator!('   ')).toBe('Node name cannot be empty');

            provider.dispose();
        });

        it('validateInput returns error when value is too long (> 128 chars)', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            let capturedValidator: ((v: string) => string | undefined) | undefined;
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockImplementation((opts: { validateInput?: (v: string) => string | undefined }) => {
                capturedValidator = opts.validateInput;
                return Promise.resolve('ValidName');
            });

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));
            await provider.renameNode(item);

            const longName = 'a'.repeat(129);
            expect(capturedValidator!(longName)).toBe('Node name is too long (max 128 chars)');

            provider.dispose();
        });

        it('validateInput returns undefined for a valid name', async () => {
            const { client, request } = makeMockIpc();
            request.mockResolvedValue({});

            let capturedValidator: ((v: string) => string | undefined) | undefined;
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };
            mockWindow.showInputBox.mockImplementation((opts: { validateInput?: (v: string) => string | undefined }) => {
                capturedValidator = opts.validateInput;
                return Promise.resolve('ValidName');
            });

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));
            await provider.renameNode(item);

            expect(capturedValidator!('GoodName')).toBeUndefined();
            expect(capturedValidator!('a'.repeat(128))).toBeUndefined();

            provider.dispose();
        });

        it('does nothing when renameNode called on disposed provider', async () => {
            const { client, request } = makeMockIpc();
            const mockWindow = window as typeof window & { showInputBox: jest.Mock };

            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'r1', name: 'OldName' }));
            provider.dispose();

            await provider.renameNode(item);
            expect(mockWindow.showInputBox).not.toHaveBeenCalled();
            expect(request).not.toHaveBeenCalledWith('scene.renameNode', expect.anything());
        });
    });

    describe('addNode() — disposed early return', () => {
        it('does nothing when disposed', async () => {
            const { client, request } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            provider.dispose();

            await provider.addNode(undefined);
            expect(request).not.toHaveBeenCalledWith('scene.addNode', expect.anything());
        });
    });

    describe('deleteNode() — disposed early return', () => {
        it('does nothing when disposed', async () => {
            const { client, request } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'del-1' }));
            provider.dispose();

            await provider.deleteNode(item);
            expect(request).not.toHaveBeenCalledWith('scene.deleteNode', expect.anything());
        });
    });

    describe('duplicateNode() — disposed early return', () => {
        it('does nothing when disposed', async () => {
            const { client, request } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            const item = new NodeItem(makeNode({ id: 'dup-1' }));
            provider.dispose();

            await provider.duplicateNode(item);
            expect(request).not.toHaveBeenCalledWith('scene.duplicateNode', expect.anything());
        });
    });

    describe('_loadTree() — disposed during async fetch', () => {
        it('does not update state when disposed while IPC is in-flight', async () => {
            const { client, request, triggerEvent } = makeMockIpc();

            // Make the IPC hang long enough that we can dispose mid-flight
            let resolveIpc!: (v: unknown) => void;
            request.mockImplementation(() => new Promise(res => { resolveIpc = res; }));

            const provider = new SceneTreeProvider(client);

            // Start load (via getChildren)
            provider.getChildren(undefined);

            // Dispose BEFORE the IPC resolves
            provider.dispose();

            // Now resolve the IPC
            resolveIpc({ nodes: [makeNode()] });
            await new Promise(r => setTimeout(r, 20));

            // Since provider is disposed, the tree update should be a no-op
            // (the disposed branch inside _loadTree handles this)
        });

        it('does not update state when disposed while IPC fails in-flight', async () => {
            const { client, request } = makeMockIpc();

            let rejectIpc!: (err: unknown) => void;
            request.mockImplementation(() => new Promise((_, rej) => { rejectIpc = rej; }));

            const provider = new SceneTreeProvider(client);
            provider.getChildren(undefined);

            // Dispose while IPC is pending
            provider.dispose();

            // Reject the IPC
            rejectIpc(new Error('Connection lost'));
            await new Promise(r => setTimeout(r, 20));
            // Should not throw or update any state
        });
    });

    describe('_loadTree() — nodes missing from response', () => {
        it('handles response with no nodes property', async () => {
            const { client, request } = makeMockIpc();
            // Response missing "nodes" field
            request.mockResolvedValue({});

            const provider = new SceneTreeProvider(client);
            provider.getChildren(undefined);
            await new Promise(r => setTimeout(r, 20));

            // Should not crash — _rootNodes = result.nodes ?? [] = []
            const items = provider.getChildren(undefined) as NodeItem[];
            expect(items).toHaveLength(0);

            provider.dispose();
        });
    });

    describe('dispose()', () => {
        it('unsubscribes from scene.treeChanged', () => {
            const { client, unsubscribers } = makeMockIpc();
            const provider = new SceneTreeProvider(client);

            provider.dispose();

            const unsub = unsubscribers.get('scene.treeChanged')?.[0];
            expect(unsub).toBeDefined();
            expect(unsub!).toHaveBeenCalled();
        });

        it('is idempotent — multiple dispose() calls do not throw', () => {
            const { client } = makeMockIpc();
            const provider = new SceneTreeProvider(client);
            expect(() => {
                provider.dispose();
                provider.dispose();
            }).not.toThrow();
        });
    });
});
