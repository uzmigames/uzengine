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

import { InspectorViewProvider } from '../inspector-panel';
import { NodeItem } from '../scene-tree-provider';
import { IpcClient } from '../ipc-client';
import { window as vscodeWindow } from '../__mocks__/vscode';

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
        on: jest.fn().mockReturnValue(() => {}),
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

        it('renders node HTML immediately when node was set before resolveWebviewView', () => {
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

            expect(view.webview.html).toContain('PreBoundNode');
            expect(view.webview.html).toContain('Camera');
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

        it('showForNode(node) after resolveWebviewView renders node name in HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'PlayerMesh', 'StaticMesh'));

            expect(view.webview.html).toContain('PlayerMesh');
        });

        it('showForNode(node) after resolveWebviewView renders node type in HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'SomeNode', 'Camera'));

            expect(view.webview.html).toContain('Camera');
        });

        it('switching from node to undefined clears node name from HTML', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            const view = makeMockWebviewView();

            provider.resolveWebviewView(
                view as unknown as import('vscode').WebviewView,
                stubContext,
                stubToken
            );

            provider.showForNode(makeNode('n1', 'OldNode', 'Node3D'));
            expect(view.webview.html).toContain('OldNode');

            provider.showForNode(undefined);
            expect(view.webview.html).not.toContain('OldNode');
            expect(view.webview.html).toContain('Select a node');
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

        it('renders Transform group', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('Transform');
        });

        it('renders Rendering group with LOD Bias and Layer labels', () => {
            const { view } = resolveWithNode();
            expect(view.webview.html).toContain('Rendering');
            expect(view.webview.html).toContain('LOD Bias');
            expect(view.webview.html).toContain('Layer');
        });

        it('HTML escapes special characters in node name', () => {
            const { view } = resolveWithNode('<xss>"name"');
            expect(view.webview.html).not.toContain('<xss>');
            expect(view.webview.html).toContain('&lt;xss&gt;');
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

    describe('_renderWidget branch coverage', () => {
        it('bool false renders checkbox without checked attribute', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const p = provider as any;
            const html = p._renderWidget(
                { key: 'myBool', label: 'My Bool', type: 'bool', value: false, group: 'Test' },
                'node-id'
            ) as string;

            expect(html).toContain('type="checkbox"');
            expect(html).not.toContain('checked');
        });

        it('vec3 with null value uses default {x:0,y:0,z:0}', () => {
            const ipc = makeMockIpc();
            const provider = new InspectorViewProvider(ipc as unknown as IpcClient);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const p = provider as any;
            const html = p._renderWidget(
                { key: 'pos', label: 'Position', type: 'vec3', value: null, group: 'Transform' },
                'node-id'
            ) as string;

            expect(html).toContain('vec3-group');
            expect(html).toContain('value="0"');
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
});
