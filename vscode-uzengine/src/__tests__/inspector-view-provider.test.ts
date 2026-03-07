/**
 * inspector-view-provider.test.ts — Unit tests for InspectorViewProvider.
 *
 * Coverage targets (≥95% line + branch):
 *  - Constructor (no-arg, just takes IpcClient)
 *  - viewType static property
 *  - resolveWebviewView() sets enableScripts=true, html is non-empty
 *  - resolveWebviewView() wires onDidReceiveMessage handler
 *  - resolveWebviewView() calls _render() immediately (placeholder shown)
 *  - showForNode(undefined) before resolveWebviewView — stores node, no crash
 *  - showForNode(undefined) after resolveWebviewView — placeholder HTML
 *  - showForNode(node) before resolveWebviewView — stores node, no crash
 *  - showForNode(node) after resolveWebviewView — HTML contains node name/type
 *  - currentNode getter returns the currently stored node
 *  - currentNode getter returns undefined when nothing selected
 *  - HTML contains DOCTYPE html, head, script
 *  - HTML renders string, float, int, bool, vec3 widget types
 *  - HTML renders group details/summary elements (Node, Transform, Rendering)
 *  - HTML escapes special characters in node name
 *  - id and type fields are marked readonly
 *  - bool true → renders 'checked' attribute
 *  - bool false → no 'checked' attribute  (_renderWidget private branch)
 *  - vec3 null value → uses default {x:0,y:0,z:0}  (_renderWidget private branch)
 *  - _render() returns early when _view is not set
 *  - Message 'propertyChanged' → calls ipc.request('scene.setProperty', ...)
 *  - Message 'propertyChanged' — IPC failure with Error → showErrorMessage
 *  - Message 'propertyChanged' — IPC failure with non-Error string → showErrorMessage
 *  - Message with unknown command → no IPC call
 *  - Non-object message (string) → no IPC call, no crash
 *  - Non-object message (null) → no IPC call, no crash
 *  - Non-object message (number) → no IPC call, no crash
 *  - dispose() sets _disposed; subsequent showForNode() is a no-op
 *  - Message arriving after dispose() does not call IPC
 */

import { window as vscodeWindow } from '../__mocks__/vscode';
import { InspectorViewProvider } from '../inspector-panel';
import { IpcClient } from '../ipc-client';
import { NodeItem } from '../scene-tree-provider';

// ---------------------------------------------------------------------------
// Inline WebviewView mock (no existing mock for WebviewView)
// ---------------------------------------------------------------------------

function makeMockWebviewView() {
    const messageHandlers: Array<(msg: unknown) => void> = [];
    const webview = {
        options: {} as { enableScripts?: boolean },
        html: '',
        postMessage: jest.fn().mockResolvedValue(true),
        onDidReceiveMessage: jest.fn((cb: (msg: unknown) => void) => {
            messageHandlers.push(cb);
            return { dispose: jest.fn() };
        }),
        cspSource: '',
    };
    return {
        webview,
        _triggerMessage(msg: unknown) {
            messageHandlers.forEach(h => h(msg));
        },
    };
}

// ---------------------------------------------------------------------------
// IpcClient mock
// ---------------------------------------------------------------------------

function makeMockIpc() {
    return {
        request: jest.fn().mockResolvedValue({}),
        on: jest.fn().mockReturnValue(() => { }),
        connect: jest.fn(),
        disconnect: jest.fn(),
        dispose: jest.fn(),
    };
}

// ---------------------------------------------------------------------------
// NodeItem helper
// ---------------------------------------------------------------------------

function makeNode(
    id = 'node-1',
    name = 'TestNode',
    type: 'Node3D' | 'Camera' | 'StaticMesh' = 'Node3D'
): NodeItem {
    return new NodeItem({ id, name, type });
}

// ---------------------------------------------------------------------------
// Minimal stubs for WebviewViewResolveContext and CancellationToken
// ---------------------------------------------------------------------------

const stubContext = {} as import('vscode').WebviewViewResolveContext;
const stubToken = {} as import('vscode').CancellationToken;

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('InspectorViewProvider', () => {

    // -------------------------------------------------------------------------
    // Static properties
    // -------------------------------------------------------------------------

    describe('static properties', () => {
        it('viewType is uzengine.inspector', () => {
            expect(InspectorViewProvider.viewType).toBe('uzengine.inspector');
        });
    });

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------

    describe('constructor', () => {
        it('constructs without throwing', () => {
            const ipc = makeMockIpc();
            expect(() => new InspectorViewProvider(ipc as unknown as IpcClient)).not.toThrow();
        });

        it('currentNode is undefined immediately after construction', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            expect(provider.currentNode).toBeUndefined();
        });
    });

    // -------------------------------------------------------------------------
    // resolveWebviewView()
    // -------------------------------------------------------------------------

    describe('resolveWebviewView()', () => {
        it('sets webview.options.enableScripts to true', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.options.enableScripts).toBe(true);
        });

        it('sets webview.html to a non-empty string', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.html.length).toBeGreaterThan(0);
        });

        it('html starts with DOCTYPE html', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.html).toContain('<!DOCTYPE html>');
        });

        it('wires onDidReceiveMessage handler', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.onDidReceiveMessage).toHaveBeenCalledTimes(1);
        });

        it('renders the placeholder immediately (no node selected)', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.html).toContain('Select a node');
        });

        it('sends postMessage with node data when node was set before resolveWebviewView', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            // Set node before view is resolved.
            provider.showForNode(makeNode('id1', 'PreBoundNode', 'Camera'));

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    nodeName: 'PreBoundNode',
                    nodeType: 'Camera',
                })
            );
        });
    });

    // -------------------------------------------------------------------------
    // showForNode()
    // -------------------------------------------------------------------------

    describe('showForNode()', () => {
        it('stores node and updates currentNode getter', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const node = makeNode('n1', 'MyMesh', 'StaticMesh');

            provider.showForNode(node);

            expect(provider.currentNode).toBe(node);
        });

        it('showForNode(undefined) sets currentNode to undefined', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            provider.showForNode(makeNode());
            provider.showForNode(undefined);

            expect(provider.currentNode).toBeUndefined();
        });

        it('showForNode(undefined) before resolveWebviewView does not throw', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            expect(() => provider.showForNode(undefined)).not.toThrow();
        });

        it('showForNode(node) before resolveWebviewView does not throw', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            expect(() => provider.showForNode(makeNode())).not.toThrow();
        });

        it('showForNode(undefined) after resolveWebviewView renders placeholder HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(undefined);

            expect(view.webview.html).toContain('Select a node');
        });

        it('showForNode(node) after resolveWebviewView posts updateNode with node name', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'PlayerMesh', 'StaticMesh'));

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ command: 'updateNode', nodeName: 'PlayerMesh' })
            );
        });

        it('showForNode(node) after resolveWebviewView posts updateNode with node type', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'SomeNode', 'Camera'));

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ command: 'updateNode', nodeType: 'Camera' })
            );
        });

        it('switching from node to undefined sends clearNode message', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'OldNode', 'Node3D'));
            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ command: 'updateNode', nodeName: 'OldNode' })
            );

            provider.showForNode(undefined);
            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ command: 'clearNode' })
            );
        });
    });

    // -------------------------------------------------------------------------
    // HTML content — widget rendering
    // -------------------------------------------------------------------------

    describe('HTML content', () => {
        function resolveWithNode(name = 'RenderNode', type: 'Node3D' | 'Camera' | 'StaticMesh' = 'Node3D') {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', name, type));
            return { provider, view, ipc };
        }

        it('includes text input for string properties', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('type="text"');
        });

        it('includes number input for float properties (lod_bias)', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('prop-float');
        });

        it('includes number input for int properties (layer)', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('prop-int');
        });

        it('includes checkbox for bool properties (visible)', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('type="checkbox"');
        });

        it('includes vec3 group for transform properties', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('vec3-x');
            expect(view.webview.html).toContain('vec3-y');
            expect(view.webview.html).toContain('vec3-z');
        });

        it('renders group details/summary elements for Node group', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('<details');
            expect(view.webview.html).toContain('<summary');
            expect(view.webview.html).toContain('Node');
        });

        it('posts properties with Transform group', () => {
            const { view } = resolveWithNode();
            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({ group: 'Transform' }),
                    ]),
                })
            );
        });

        it('posts properties with Rendering group (LOD Bias and Layer)', () => {
            const { view } = resolveWithNode();
            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({ group: 'Rendering', label: 'LOD Bias' }),
                        expect.objectContaining({ group: 'Rendering', label: 'Layer' }),
                    ]),
                })
            );
        });

        it('posts node name with special characters unmodified in message', () => {
            const { view } = resolveWithNode('<xss>"name"');
            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    nodeName: '<xss>"name"',
                })
            );
        });

        it('marks id and type fields as readonly', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('readonly');
        });

        it('renders checked attribute for truthy bool value (visible=true)', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('checked');
        });

        it('includes acquireVsCodeApi script', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('acquireVsCodeApi');
        });

        it('renders inspector-header with node-name and node-icon spans', () => {
            const { view } = resolveWithNode('HeaderNode');
            expect(view.webview.html).toContain('inspector-header');
            expect(view.webview.html).toContain('node-name');
            expect(view.webview.html).toContain('node-icon');
        });
    });

    // -------------------------------------------------------------------------
    // HTML rendering — private branch coverage via cast
    // -------------------------------------------------------------------------

    describe('_buildProperties coverage', () => {
        it('posts properties with bool visible=true', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'TestNode', 'Node3D'));

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({ key: 'visible', type: 'bool', value: true }),
                    ]),
                })
            );
        });

        it('posts vec3 properties with default values', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'TestNode', 'Node3D'));

            expect(view.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({ key: 'position', type: 'vec3', value: { x: 0, y: 0, z: 0 } }),
                        expect.objectContaining({ key: 'scale', type: 'vec3', value: { x: 1, y: 1, z: 1 } }),
                    ]),
                })
            );
        });

        it('_render() returns early without throwing when _view is not set', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const p = provider as any;
            p._currentNode = makeNode();
            // _view is undefined — should not throw
            expect(() => p._render()).not.toThrow();
        });
    });

    // -------------------------------------------------------------------------
    // Message handling — propertyChanged
    // -------------------------------------------------------------------------

    describe('message handling', () => {
        it('sends scene.setProperty IPC on propertyChanged message', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'MyNode', 'Node3D'));

            view._triggerMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'visible',
                value: false,
            });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).toHaveBeenCalledWith('scene.setProperty', {
                nodeId: 'n1',
                key: 'visible',
                value: false,
            });
        });

        it('shows error message when IPC fails with an Error instance', async () => {
            const ipc = makeMockIpc();
            (ipc.request as jest.Mock).mockRejectedValue(new Error('IPC failure'));

            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1'));

            view._triggerMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'name',
                value: 'NewName',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('IPC failure')
            );
        });

        it('shows error message when IPC fails with a non-Error (plain string)', async () => {
            const ipc = makeMockIpc();
            (ipc.request as jest.Mock).mockRejectedValue('plain string error');

            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1'));

            view._triggerMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'name',
                value: 'N',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('plain string error')
            );
        });

        it('ignores messages with unknown command — no IPC call', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            view._triggerMessage({ command: 'unknownCommand', data: 42 });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('ignores non-object message (string) — no IPC call, no crash', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            view._triggerMessage('just a string');

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('ignores non-object message (null) — no IPC call, no crash', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            view._triggerMessage(null);

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('ignores non-object message (number) — no IPC call, no crash', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            view._triggerMessage(42);

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('message arriving after dispose() does not call IPC', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1'));
            provider.dispose();

            view._triggerMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'visible',
                value: true,
            });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });
    });

    // -------------------------------------------------------------------------
    // dispose()
    // -------------------------------------------------------------------------

    describe('dispose()', () => {
        it('showForNode() is a no-op after dispose', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.dispose();

            // Set html to a known value to detect whether _render() was called.
            view.webview.html = 'SENTINEL';
            provider.showForNode(makeNode('n2', 'PostDispose', 'Node3D'));

            // html should remain unchanged because showForNode is a no-op.
            expect(view.webview.html).toBe('SENTINEL');
        });

        it('currentNode is NOT updated after dispose', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            provider.showForNode(makeNode('n1', 'Before', 'Node3D'));
            provider.dispose();

            provider.showForNode(makeNode('n2', 'After', 'Node3D'));

            // After dispose, showForNode is a no-op — currentNode stays as 'Before' node
            // BUT dispose() itself does NOT clear _currentNode, so it remains 'Before'.
            // The important thing is the second showForNode('After') does nothing.
            expect(provider.currentNode?.nodeName).toBe('Before');
        });

        it('dispose() does not throw when called without resolveWebviewView', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            expect(() => provider.dispose()).not.toThrow();
        });

        it('dispose() is idempotent — calling twice does not throw', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            expect(() => {
                provider.dispose();
                provider.dispose();
            }).not.toThrow();
        });
    });

    // -------------------------------------------------------------------------
    // currentNode getter
    // -------------------------------------------------------------------------

    describe('currentNode getter', () => {
        it('returns undefined initially', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            expect(provider.currentNode).toBeUndefined();
        });

        it('returns the node passed to showForNode()', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const node = makeNode('n1', 'Getter', 'Camera');

            provider.showForNode(node);

            expect(provider.currentNode).toBe(node);
        });

        it('returns undefined after showForNode(undefined)', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);

            provider.showForNode(makeNode());
            provider.showForNode(undefined);

            expect(provider.currentNode).toBeUndefined();
        });

        it('returns the most recently set node', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const node1 = makeNode('n1', 'First', 'Node3D');
            const node2 = makeNode('n2', 'Second', 'Camera');

            provider.showForNode(node1);
            provider.showForNode(node2);

            expect(provider.currentNode).toBe(node2);
        });
    });

    // -------------------------------------------------------------------------
    // Resource browse message handling
    // -------------------------------------------------------------------------

    describe('resourceBrowse message handling', () => {
        it('opens file dialog and sends IPC on resourceBrowse', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            vscodeWindow.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/mat.uzmat' },
            ]);

            mockView._triggerMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(vscodeWindow.showOpenDialog).toHaveBeenCalledWith(
                expect.objectContaining({ canSelectMany: false })
            );

            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'resourcePathSelected',
                    key: 'material',
                    path: '/path/to/mat.uzmat',
                })
            );

            expect(ipc.request).toHaveBeenCalledWith('scene.setProperty', {
                nodeId: 'n1',
                key: 'material',
                value: '/path/to/mat.uzmat',
            });
        });

        it('does nothing when user cancels file dialog', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            vscodeWindow.showOpenDialog.mockResolvedValueOnce(undefined);

            mockView._triggerMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('does nothing when file dialog returns empty array', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            vscodeWindow.showOpenDialog.mockResolvedValueOnce([]);

            mockView._triggerMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
        });

        it('shows error when IPC fails on resource browse', async () => {
            const ipc = makeMockIpc();
            ipc.request.mockRejectedValue(new Error('Engine crashed'));
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            vscodeWindow.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/mat.uzmat' },
            ]);

            mockView._triggerMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Engine crashed')
            );
        });

        it('shows non-Error exception message on resource browse failure', async () => {
            const ipc = makeMockIpc();
            ipc.request.mockRejectedValue('plain string error');
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            vscodeWindow.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/mat.uzmat' },
            ]);

            mockView._triggerMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('plain string error')
            );
        });
    });

    // -------------------------------------------------------------------------
    // Color and resource widgets in shell HTML
    // -------------------------------------------------------------------------

    describe('shell HTML includes color/resource widgets', () => {
        it('shell HTML contains color and resource widget JS functions', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            expect(mockView.webview.html).toContain('onColorChange');
            expect(mockView.webview.html).toContain('onResourceBrowse');
            expect(mockView.webview.html).toContain('resourcePathSelected');
        });

        it('shell HTML contains color/resource CSS', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            expect(mockView.webview.html).toContain('.prop-color');
            expect(mockView.webview.html).toContain('.resource-group');
            expect(mockView.webview.html).toContain('.resource-browse');
        });

        it('shell renderWidget handles color type', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            // Show node to trigger postMessage with properties including color
            provider.showForNode(makeNode());

            // The postMessage should include albedo_color property
            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({
                            key: 'albedo_color',
                            type: 'color',
                        }),
                    ]),
                })
            );
        });

        it('shell renderWidget handles resource type', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            provider.showForNode(makeNode());

            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    properties: expect.arrayContaining([
                        expect.objectContaining({
                            key: 'material',
                            type: 'resource',
                            assetType: 'Material',
                        }),
                    ]),
                })
            );
        });
    });

    describe('asset picker — openAssetPicker message', () => {
        it('handles openAssetPicker with QuickPick selection and calls scene.assignAsset', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode('7', 'TestNode'));

            ipc.request.mockResolvedValueOnce({
                assets: [
                    { path: 'res://materials/wood.uzmat', type: 'Material', status: 'Fresh' },
                ],
            });

            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'wood.uzmat',
                description: 'res://materials/wood.uzmat',
                detail: 'Material — Fresh',
            });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '7',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(ipc.request).toHaveBeenCalledWith('asset.list', {});
            expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
                nodeId: 7,
                resPath: 'res://materials/wood.uzmat',
                assetType: 'Material',
            });

            expect(mockView.webview.postMessage).toHaveBeenCalledWith({
                command: 'resourcePathSelected',
                key: 'material',
                path: 'res://materials/wood.uzmat',
            });
        });

        it('shows info when no matching assets are found', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request.mockResolvedValueOnce({ assets: [] });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No Material found in registry.'
            );
        });

        it('does nothing when QuickPick is cancelled', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request.mockResolvedValueOnce({
                assets: [{ path: 'res://mat/a.uzmat', type: 'Material', status: 'Fresh' }],
            });
            vscodeWindow.showQuickPick.mockResolvedValueOnce(undefined);

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(ipc.request).not.toHaveBeenCalledWith('scene.assignAsset', expect.anything());
        });

        it('shows error when scene.assignAsset fails', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request
                .mockResolvedValueOnce({ assets: [{ path: 'res://a.uzmat', type: 'Material', status: 'Fresh' }] })
                .mockRejectedValueOnce(new Error('assign failed'));

            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                description: 'res://a.uzmat',
                detail: 'Material — Fresh',
            });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('assign failed')
            );
        });

        it('shows error with non-Error exception from assignAsset', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request
                .mockResolvedValueOnce({ assets: [{ path: 'res://a.uzmat', type: 'Material', status: 'Fresh' }] })
                .mockRejectedValueOnce('string error');

            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                description: 'res://a.uzmat',
                detail: 'Material — Fresh',
            });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('string error')
            );
        });

        it('handles asset.list IPC failure gracefully', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request.mockRejectedValueOnce(new Error('IPC down'));

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No Material found in registry.'
            );
        });

        it('shows generic "assets" when assetType is empty and no assets found', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request.mockResolvedValueOnce({ assets: [] });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: '',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No assets found in registry.'
            );
        });

        it('handles QuickPick item without description field', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode('10'));

            ipc.request.mockResolvedValueOnce({
                assets: [{ path: 'res://mat/a.uzmat', type: 'Material', status: 'Fresh' }],
            });

            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                // No description — tests the ?? '' fallback.
            });

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '10',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
                nodeId: 10,
                resPath: '',
                assetType: 'Material',
            });
        });

        it('uses generic placeholder when assetType is empty', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());

            ipc.request.mockResolvedValueOnce({
                assets: [{ path: 'res://a.glb', type: 'Mesh', status: 'Fresh' }],
            });
            vscodeWindow.showQuickPick.mockResolvedValueOnce(undefined);

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: '',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showQuickPick).toHaveBeenCalledWith(
                expect.anything(),
                expect.objectContaining({ placeHolder: 'Select asset for "material"' })
            );
        });

        it('does not handle openAssetPicker after dispose', async () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.showForNode(makeNode());
            provider.dispose();

            mockView._triggerMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            // asset.list should not have been called.
            expect(ipc.request).not.toHaveBeenCalledWith('asset.list', expect.anything());
        });
    });

    describe('shell HTML — asset picker', () => {
        it('includes asset-picker CSS class in shell HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            expect(mockView.webview.html).toContain('.asset-picker');
            expect(mockView.webview.html).toContain('onAssetPicker');
        });
    });

    // -------------------------------------------------------------------------
    // showForNodes (multi-select)
    // -------------------------------------------------------------------------

    describe('showForNodes()', () => {
        it('sends updateMultiSelect message with count and common properties', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            const nodeA = new NodeItem({ id: '1', name: 'A', type: 'Node3D' });
            const nodeB = new NodeItem({ id: '2', name: 'B', type: 'StaticMesh' });
            provider.showForNodes([nodeA, nodeB]);

            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateMultiSelect',
                    count: 2,
                    properties: expect.any(Array),
                })
            );
        });

        it('falls back to showForNode when given one node', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            const nodeA = new NodeItem({ id: '1', name: 'A', type: 'Node3D' });
            provider.showForNodes([nodeA]);

            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'updateNode',
                    nodeId: '1',
                })
            );
        });

        it('clears when given empty array', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            provider.showForNodes([]);

            expect(mockView.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({ command: 'clearNode' })
            );
        });

        it('does nothing when disposed', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );
            provider.dispose();
            mockView.webview.postMessage.mockClear();

            const node = new NodeItem({ id: '1', name: 'A', type: 'Node3D' });
            provider.showForNodes([node, node]);

            expect(mockView.webview.postMessage).not.toHaveBeenCalled();
        });

        it('includes mixed-value CSS class in shell HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const mockView = makeMockWebviewView();
            provider.resolveWebviewView(
                mockView as unknown as import('vscode').WebviewView,
                stubContext, stubToken
            );

            expect(mockView.webview.html).toContain('.mixed-value');
            expect(mockView.webview.html).toContain('updateMultiSelect');
        });
    });
});
