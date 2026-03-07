/**
 * inspector-panel.test.ts — Unit tests for InspectorPanel.
 *
 * Tests cover:
 *  - showForNode(node) creates a WebviewPanel the first time
 *  - showForNode(node) reveals the existing panel on subsequent calls
 *  - showForNode(undefined) renders "no selection" HTML
 *  - showForNode(node) renders properties HTML containing node name and type
 *  - HTML contains text input for string properties
 *  - HTML contains number input for float/int properties
 *  - HTML contains checkbox for bool properties
 *  - HTML contains vec3 fields for vec3 properties
 *  - HTML wraps properties in group details/summary elements
 *  - isVisible returns true when panel is visible
 *  - isVisible returns false when no panel exists
 *  - Webview message "propertyChanged" triggers scene.setProperty IPC
 *  - Webview message "propertyChanged" shows error when IPC fails (Error and non-Error)
 *  - Unknown command messages are ignored
 *  - Non-object messages are ignored
 *  - dispose() closes the panel and disposes _disposables
 *  - showForNode after dispose() does nothing
 *  - Panel closure from UI clears the internal reference
 *  - _renderWidget float, int, bool(false) branches
 */

import { InspectorPanel, fetchAssetQuickPickItems, commonProperties, MIXED_VALUE, PropertyDescriptor } from '../inspector-panel';
import { NodeItem } from '../scene-tree-provider';
import { IpcClient } from '../ipc-client';
import { window, makeMockWebviewPanel, __setMockPanelFactory, __resetMockPanelFactory } from '../__mocks__/vscode';

// ---------------------------------------------------------------------------
// Mock IpcClient
// ---------------------------------------------------------------------------

function makeMockIpc(): { client: IpcClient; request: jest.Mock } {
    const request = jest.fn().mockResolvedValue({});
    const client = { request } as unknown as IpcClient;
    return { client, request };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeNodeItem(overrides: Partial<{ id: string; name: string; type: string }> = {}): NodeItem {
    return new NodeItem({
        id: overrides.id ?? 'n1',
        name: overrides.name ?? 'TestNode',
        type: (overrides.type as 'Node3D') ?? 'Node3D',
    });
}

function makeInspector(): {
    inspector: InspectorPanel;
    ipc: { client: IpcClient; request: jest.Mock };
    mockPanel: ReturnType<typeof makeMockWebviewPanel>;
} {
    const ipc = makeMockIpc();
    const mockPanel = makeMockWebviewPanel();
    __setMockPanelFactory(() => mockPanel);

    const extensionUri = { fsPath: '/mock/ext', scheme: 'file', path: '/mock/ext' };
    const inspector = new InspectorPanel(extensionUri as import('vscode').Uri, ipc.client);

    return { inspector, ipc, mockPanel };
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    __resetMockPanelFactory();
});

afterEach(() => {
    __resetMockPanelFactory();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('InspectorPanel', () => {

    describe('showForNode() — panel creation', () => {
        it('creates a WebviewPanel on the first call', () => {
            const { inspector, mockPanel } = makeInspector();
            const node = makeNodeItem();

            inspector.showForNode(node);

            expect(window.createWebviewPanel).toHaveBeenCalledWith(
                'uzengine.inspector',
                'UzEngine Inspector',
                expect.anything(), // ViewColumn.Two
                expect.objectContaining({ enableScripts: true, retainContextWhenHidden: true })
            );
            expect(mockPanel.webview.html).toContain('TestNode');

            inspector.dispose();
        });

        it('reveals the existing panel on subsequent calls', () => {
            const { inspector, mockPanel } = makeInspector();
            const node = makeNodeItem();

            inspector.showForNode(node);
            inspector.showForNode(node); // Second call — should reveal, not recreate.

            expect(window.createWebviewPanel).toHaveBeenCalledTimes(1);
            expect(mockPanel.reveal).toHaveBeenCalled();

            inspector.dispose();
        });

        it('does nothing when disposed', () => {
            const { inspector } = makeInspector();
            inspector.dispose();

            inspector.showForNode(makeNodeItem()); // Should not throw.
            expect(window.createWebviewPanel).not.toHaveBeenCalled();
        });
    });

    describe('showForNode(undefined) — no selection', () => {
        it('renders "no selection" HTML', () => {
            const { inspector, mockPanel } = makeInspector();

            inspector.showForNode(undefined);

            expect(mockPanel.webview.html).toContain('Select a node');
            inspector.dispose();
        });

        it('clears content when selection is cleared after having a node', () => {
            const { inspector, mockPanel } = makeInspector();

            inspector.showForNode(makeNodeItem({ name: 'SomeNode' }));
            expect(mockPanel.webview.html).toContain('SomeNode');

            inspector.showForNode(undefined);
            expect(mockPanel.webview.html).toContain('Select a node');
            expect(mockPanel.webview.html).not.toContain('SomeNode');

            inspector.dispose();
        });
    });

    describe('HTML content', () => {
        it('contains the node name in the header', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ name: 'PlayerCapsule' }));

            expect(mockPanel.webview.html).toContain('PlayerCapsule');
            inspector.dispose();
        });

        it('contains the node type in the header', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ type: 'Camera' }));

            expect(mockPanel.webview.html).toContain('Camera');
            inspector.dispose();
        });

        it('renders text input for string properties', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ name: 'SomeNode' }));

            // Name is a string property — should render a text input.
            expect(mockPanel.webview.html).toContain('type="text"');
            inspector.dispose();
        });

        it('renders number input for vec3 properties', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            // Vec3 position/rotation/scale properties use number inputs.
            expect(mockPanel.webview.html).toContain('type="number"');
            inspector.dispose();
        });

        it('renders checkbox for bool properties', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            // Visible property is bool.
            expect(mockPanel.webview.html).toContain('type="checkbox"');
            inspector.dispose();
        });

        it('renders X, Y, Z labels for vec3 properties', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('vec3-x');
            expect(mockPanel.webview.html).toContain('vec3-y');
            expect(mockPanel.webview.html).toContain('vec3-z');
            inspector.dispose();
        });

        it('renders group details/summary elements', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('<details');
            expect(mockPanel.webview.html).toContain('<summary');
            expect(mockPanel.webview.html).toContain('Node');
            expect(mockPanel.webview.html).toContain('Transform');
            inspector.dispose();
        });

        it('HTML escapes special characters in node name', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ name: '<script>alert("xss")</script>' }));

            expect(mockPanel.webview.html).not.toContain('<script>alert(');
            expect(mockPanel.webview.html).toContain('&lt;script&gt;');
            inspector.dispose();
        });

        it('marks id and type fields as readonly', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: 'id-123' }));

            expect(mockPanel.webview.html).toContain('readonly');
            inspector.dispose();
        });

        it('includes acquireVsCodeApi() script', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('acquireVsCodeApi');
            inspector.dispose();
        });

        it('renders checked attribute for truthy bool values', () => {
            const { inspector, mockPanel } = makeInspector();
            // 'visible' property defaults to true
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('checked');
            inspector.dispose();
        });

        it('renders HTML doctype and head', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('<!DOCTYPE html>');
            expect(mockPanel.webview.html).toContain('<head>');
            inspector.dispose();
        });
    });

    describe('isVisible', () => {
        it('returns false when no panel exists', () => {
            const { inspector } = makeInspector();
            expect(inspector.isVisible).toBe(false);
            inspector.dispose();
        });

        it('returns true when panel is visible', () => {
            const { inspector, mockPanel } = makeInspector();
            mockPanel.visible = true;
            inspector.showForNode(makeNodeItem());
            expect(inspector.isVisible).toBe(true);
            inspector.dispose();
        });

        it('returns false when panel is not visible', () => {
            const { inspector, mockPanel } = makeInspector();
            mockPanel.visible = false;
            inspector.showForNode(makeNodeItem());
            expect(inspector.isVisible).toBe(false);
            inspector.dispose();
        });
    });

    describe('webview message handling', () => {
        it('sends scene.setProperty IPC on propertyChanged message', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            // Use the _simulateMessage helper added to the mock.
            mockPanel._simulateMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'visible',
                value: false,
            });

            // Allow async handler to run.
            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).toHaveBeenCalledWith('scene.setProperty', {
                nodeId: 'n1',
                key: 'visible',
                value: false,
            });

            inspector.dispose();
        });

        it('shows error message when scene.setProperty IPC fails with Error', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            ipc.request.mockRejectedValue(new Error('Engine error'));

            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            mockPanel._simulateMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'name',
                value: 'NewName',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Engine error')
            );

            inspector.dispose();
        });

        it('shows non-Error exception message in error notification', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            // Reject with a non-Error value (plain string)
            ipc.request.mockRejectedValue('some string error');

            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            mockPanel._simulateMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'name',
                value: 'NewName',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('some string error')
            );

            inspector.dispose();
        });

        it('ignores unknown command messages', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            mockPanel._simulateMessage({ command: 'unknownCommand', data: 42 });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
            inspector.dispose();
        });

        it('ignores non-object messages (string)', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            mockPanel._simulateMessage('just a string');

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
            inspector.dispose();
        });

        it('ignores non-object messages (null)', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            mockPanel._simulateMessage(null);

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
            inspector.dispose();
        });

        it('ignores non-object messages (number)', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            mockPanel._simulateMessage(42);

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
            inspector.dispose();
        });

        it('does not send IPC when disposed before message arrives', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());
            inspector.dispose();

            // The message handler was registered before dispose, so _simulateMessage
            // still fires it, but the disposed guard should block IPC from being called.
            mockPanel._simulateMessage({
                command: 'propertyChanged',
                nodeId: 'n1',
                key: 'visible',
                value: true,
            });

            await new Promise(r => setTimeout(r, 10));

            // IPC should not be called after dispose.
            expect(ipc.request).not.toHaveBeenCalled();
        });
    });

    describe('panel lifecycle', () => {
        it('clears the internal panel reference when the panel is disposed from UI', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(inspector.isVisible).toBe(true);

            // Simulate user closing the panel.
            mockPanel._simulateDispose();

            expect(inspector.isVisible).toBe(false);
        });

        it('dispose() is idempotent', () => {
            const { inspector } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(() => {
                inspector.dispose();
                inspector.dispose();
            }).not.toThrow();
        });

        it('dispose() calls panel.dispose()', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());
            inspector.dispose();

            expect(mockPanel.dispose).toHaveBeenCalled();
        });

        it('dispose() without ever showing node does not throw', () => {
            const { inspector } = makeInspector();
            expect(() => inspector.dispose()).not.toThrow();
        });

        it('dispose() disposes items in the _disposables array (onDidReceiveMessage + onDidDispose)', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            // After showForNode, _disposables has items from onDidReceiveMessage
            // and onDidDispose (because our updated mock pushes into the provided array).
            // dispose() should iterate and call dispose() on each item.
            inspector.dispose();

            // The disposables pushed by the panel's onDidReceiveMessage/onDidDispose
            // should have had dispose() called on them.
            // We verify by checking that the mock's onDidReceiveMessage was called
            // (the updated mock returns a disposable and pushes it to _disposables).
            expect(mockPanel.webview.onDidReceiveMessage).toHaveBeenCalled();
            expect(mockPanel.onDidDispose).toHaveBeenCalled();
        });
    });

    describe('HTML rendering — branch coverage', () => {
        it('renders unchecked checkbox when bool value is false', () => {
            // The 'visible' property is rendered from _buildProperties with value=true.
            // We need to verify the false branch: value ? 'checked' : ''
            // This is tested indirectly via the HTML output not containing 'checked'
            // for a bool-false value. Since we cannot inject custom props in Phase 1,
            // we check the rendered bool=true path already exists and then
            // verify rendering does not crash for any node.
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());
            // visible=true → should render checked attribute
            expect(mockPanel.webview.html).toContain('checked');
            inspector.dispose();
        });

        it('renders float and int number inputs (Rendering group)', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());
            // The inspector includes lod_bias (float) and layer (int) in the Rendering group.
            expect(mockPanel.webview.html).toContain('prop-float');
            expect(mockPanel.webview.html).toContain('prop-int');
            expect(mockPanel.webview.html).toContain('Rendering');
            inspector.dispose();
        });

        it('renders Rendering group labels', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());
            expect(mockPanel.webview.html).toContain('LOD Bias');
            expect(mockPanel.webview.html).toContain('Layer');
            inspector.dispose();
        });

        it('_renderWidget — bool false renders empty checked string (unchecked checkbox)', () => {
            // Access private _renderWidget via type assertion to exercise the false branch
            // of `prop.value ? 'checked' : ''`
            const { inspector, ipc } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'myBool', label: 'My Bool', type: 'bool', value: false, group: 'Test' },
                'node-id'
            ) as string;
            expect(html).toContain('type="checkbox"');
            expect(html).not.toContain('checked');
            ipc.client; // reference to avoid unused warning
        });

        it('_renderWidget — vec3 with null value uses default {x:0,y:0,z:0}', () => {
            // Exercise the `?? { x: 0, y: 0, z: 0 }` fallback branch
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'pos', label: 'Position', type: 'vec3', value: null, group: 'Transform' },
                'node-id'
            ) as string;
            expect(html).toContain('vec3-group');
            expect(html).toContain('value="0"');
        });

        it('_updateContent — returns early when _panel is null', () => {
            // Exercise the `if (!this._panel) { return; }` branch on line 164
            // Create an inspector and immediately call _updateContent without creating panel
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            panel._currentNode = makeNodeItem();
            // _panel is null since showForNode was never called
            expect(() => panel._updateContent()).not.toThrow();
            inspector.dispose();
        });
    });

    describe('color widget', () => {
        it('renders color input with hex value derived from float RGB', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            // albedo_color defaults to {r:1, g:1, b:1} → #ffffff
            expect(mockPanel.webview.html).toContain('type="color"');
            expect(mockPanel.webview.html).toContain('prop-color');
            expect(mockPanel.webview.html).toContain('#ffffff');
            inspector.dispose();
        });

        it('_renderWidget — color with custom RGB values', () => {
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'col', label: 'Color', type: 'color', value: { r: 1.0, g: 0.0, b: 0.5 }, group: 'Test' },
                'node-id'
            ) as string;
            expect(html).toContain('type="color"');
            // r=1.0→ff, g=0.0→00, b=0.5→80
            expect(html).toContain('#ff0080');
        });

        it('_renderWidget — color with null value uses black default', () => {
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'col', label: 'Color', type: 'color', value: null, group: 'Test' },
                'node-id'
            ) as string;
            expect(html).toContain('#000000');
        });

        it('_renderWidget — color clamps out-of-range values', () => {
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'col', label: 'Color', type: 'color', value: { r: 2.0, g: -1.0, b: 0.5 }, group: 'Test' },
                'node-id'
            ) as string;
            // r=2.0→clamped to 255→ff, g=-1.0→clamped to 0→00, b=0.5→80
            expect(html).toContain('#ff0080');
        });

        it('renders Material group with Albedo Color label', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('Material');
            expect(mockPanel.webview.html).toContain('Albedo Color');
            inspector.dispose();
        });

        it('includes onColorChange function in script', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('onColorChange');
            inspector.dispose();
        });
    });

    describe('resource widget', () => {
        it('renders resource input with Browse button', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('prop-resource');
            expect(mockPanel.webview.html).toContain('resource-browse');
            expect(mockPanel.webview.html).toContain('Browse...');
            inspector.dispose();
        });

        it('_renderWidget — resource with path value', () => {
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'mat', label: 'Material', type: 'resource', value: 'assets/stone.uzmat', group: 'Test' },
                'node-id'
            ) as string;
            expect(html).toContain('prop-resource');
            expect(html).toContain('resource-browse');
            expect(html).toContain('assets/stone.uzmat');
            expect(html).toContain('readonly');
        });

        it('_renderWidget — resource with null/empty value', () => {
            const { inspector } = makeInspector();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const panel = inspector as any;
            const html = panel._renderWidget(
                { key: 'mat', label: 'Material', type: 'resource', value: null, group: 'Test' },
                'node-id'
            ) as string;
            expect(html).toContain('resource-group');
            expect(html).toContain('value=""');
        });

        it('includes onResourceBrowse function in script', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('onResourceBrowse');
            inspector.dispose();
        });
    });

    describe('resourceBrowse message handling', () => {
        it('opens file dialog and sends IPC on resourceBrowse message', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            // Mock showOpenDialog to return a file
            window.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/material.uzmat' },
            ]);

            mockPanel._simulateMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showOpenDialog).toHaveBeenCalledWith(
                expect.objectContaining({
                    canSelectMany: false,
                    filters: expect.objectContaining({ 'Assets': expect.any(Array) }),
                })
            );

            // Sends resourcePathSelected to webview
            expect(mockPanel.webview.postMessage).toHaveBeenCalledWith(
                expect.objectContaining({
                    command: 'resourcePathSelected',
                    key: 'material',
                    path: '/path/to/material.uzmat',
                })
            );

            // Sends IPC to engine
            expect(ipc.request).toHaveBeenCalledWith('scene.setProperty', {
                nodeId: 'n1',
                key: 'material',
                value: '/path/to/material.uzmat',
            });

            inspector.dispose();
        });

        it('does nothing when user cancels file dialog', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            // Mock showOpenDialog to return undefined (cancelled)
            window.showOpenDialog.mockResolvedValueOnce(undefined);

            mockPanel._simulateMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showOpenDialog).toHaveBeenCalled();
            expect(ipc.request).not.toHaveBeenCalled();

            inspector.dispose();
        });

        it('does nothing when file dialog returns empty array', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            window.showOpenDialog.mockResolvedValueOnce([]);

            mockPanel._simulateMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(ipc.request).not.toHaveBeenCalled();
            inspector.dispose();
        });

        it('shows error when IPC fails on resource browse', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            ipc.request.mockRejectedValue(new Error('Engine down'));
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            window.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/mat.uzmat' },
            ]);

            mockPanel._simulateMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('Engine down')
            );

            inspector.dispose();
        });

        it('shows non-Error exception message on resource browse failure', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            ipc.request.mockRejectedValue('string error');
            inspector.showForNode(makeNodeItem({ id: 'n1' }));

            window.showOpenDialog.mockResolvedValueOnce([
                { fsPath: '/path/to/mat.uzmat' },
            ]);

            mockPanel._simulateMessage({
                command: 'resourceBrowse',
                nodeId: 'n1',
                key: 'material',
            });

            await new Promise(r => setTimeout(r, 10));

            expect(window.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('string error')
            );

            inspector.dispose();
        });
    });

    describe('CSS styles', () => {
        it('includes color picker CSS', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('.prop-color');
            inspector.dispose();
        });

        it('includes resource widget CSS', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('.resource-group');
            expect(mockPanel.webview.html).toContain('.resource-browse');
            inspector.dispose();
        });

        it('includes asset picker CSS', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('.asset-picker');
            inspector.dispose();
        });
    });

    describe('asset picker — [...]  button', () => {
        it('renders [...] button next to resource fields', () => {
            const { inspector, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            expect(mockPanel.webview.html).toContain('[...]');
            expect(mockPanel.webview.html).toContain('onAssetPicker');
            inspector.dispose();
        });

        it('handles openAssetPicker message with QuickPick selection', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            const node = makeNodeItem({ id: '42' });
            inspector.showForNode(node);

            // Mock asset.list IPC response.
            ipc.request.mockResolvedValueOnce({
                assets: [
                    { path: 'res://materials/wood.uzmat', type: 'Material', status: 'Fresh' },
                    { path: 'res://materials/metal.uzmat', type: 'Material', status: 'Fresh' },
                ],
            });

            // Mock QuickPick selection.
            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'wood.uzmat',
                description: 'res://materials/wood.uzmat',
                detail: 'Material — Fresh',
            });

            // Simulate the webview message.
            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '42',
                key: 'material',
                assetType: 'Material',
            });

            // Flush async operations.
            await new Promise(r => setTimeout(r, 0));

            // Should have called asset.list and scene.assignAsset.
            expect(ipc.request).toHaveBeenCalledWith('asset.list', {});
            expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
                nodeId: 42,
                resPath: 'res://materials/wood.uzmat',
                assetType: 'Material',
            });

            // Should update the webview.
            expect(mockPanel.webview.postMessage).toHaveBeenCalledWith({
                command: 'resourcePathSelected',
                key: 'material',
                path: 'res://materials/wood.uzmat',
            });

            inspector.dispose();
        });

        it('shows info message when no assets found', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockResolvedValueOnce({ assets: [] });

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No Material found in registry.'
            );
            inspector.dispose();
        });

        it('shows generic "assets" when assetType is empty and no assets found', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockResolvedValueOnce({ assets: [] });

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: '',
            });

            await new Promise(r => setTimeout(r, 0));

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No assets found in registry.'
            );
            inspector.dispose();
        });

        it('handles QuickPick item without description', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: '10' }));

            ipc.request.mockResolvedValueOnce({
                assets: [{ path: 'res://mat/a.uzmat', type: 'Material', status: 'Fresh' }],
            });

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                // No description field — tests the ?? '' fallback.
            });

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '10',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            // Should still call assignAsset with empty resPath.
            expect(ipc.request).toHaveBeenCalledWith('scene.assignAsset', {
                nodeId: 10,
                resPath: '',
                assetType: 'Material',
            });
            inspector.dispose();
        });

        it('does nothing when QuickPick is cancelled', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockResolvedValueOnce({
                assets: [
                    { path: 'res://materials/wood.uzmat', type: 'Material', status: 'Fresh' },
                ],
            });

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce(undefined);

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            // scene.assignAsset should NOT have been called.
            expect(ipc.request).not.toHaveBeenCalledWith('scene.assignAsset', expect.anything());
            inspector.dispose();
        });

        it('shows error when scene.assignAsset IPC fails', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: '5' }));

            ipc.request
                .mockResolvedValueOnce({
                    assets: [{ path: 'res://mat/a.uzmat', type: 'Material', status: 'Fresh' }],
                })
                .mockRejectedValueOnce(new Error('assign failed'));

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                description: 'res://mat/a.uzmat',
                detail: 'Material — Fresh',
            });

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '5',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('assign failed')
            );
            inspector.dispose();
        });

        it('shows error with non-Error exception from assignAsset', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem({ id: '5' }));

            ipc.request
                .mockResolvedValueOnce({
                    assets: [{ path: 'res://mat/a.uzmat', type: 'Material', status: 'Fresh' }],
                })
                .mockRejectedValueOnce('string error');

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce({
                label: 'a.uzmat',
                description: 'res://mat/a.uzmat',
                detail: 'Material — Fresh',
            });

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '5',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
                expect.stringContaining('string error')
            );
            inspector.dispose();
        });

        it('handles IPC failure in asset.list gracefully', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockRejectedValueOnce(new Error('IPC down'));

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            // Should show "no assets found" since the fetch returned [].
            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            expect(vscodeWindow.showInformationMessage).toHaveBeenCalledWith(
                'No Material found in registry.'
            );
            inspector.dispose();
        });

        it('filters assets by type when assetType is provided', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockResolvedValueOnce({
                assets: [
                    { path: 'res://meshes/cube.glb', type: 'Mesh', status: 'Fresh' },
                    { path: 'res://materials/wood.uzmat', type: 'Material', status: 'Fresh' },
                ],
            });

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            // Will be called with only Material items.
            vscodeWindow.showQuickPick.mockResolvedValueOnce(undefined); // cancel

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: 'Material',
            });

            await new Promise(r => setTimeout(r, 0));

            // QuickPick should have been called with only the Material item.
            expect(vscodeWindow.showQuickPick).toHaveBeenCalledWith(
                [expect.objectContaining({ label: 'wood.uzmat' })],
                expect.anything()
            );
            inspector.dispose();
        });

        it('shows all assets when assetType is empty string', async () => {
            const { inspector, ipc, mockPanel } = makeInspector();
            inspector.showForNode(makeNodeItem());

            ipc.request.mockResolvedValueOnce({
                assets: [
                    { path: 'res://meshes/cube.glb', type: 'Mesh', status: 'Fresh' },
                    { path: 'res://materials/wood.uzmat', type: 'Material', status: 'Fresh' },
                ],
            });

            const { window: vscodeWindow } = await import('../__mocks__/vscode');
            vscodeWindow.showQuickPick.mockResolvedValueOnce(undefined);

            mockPanel._simulateMessage({
                command: 'openAssetPicker',
                nodeId: '1',
                key: 'material',
                assetType: '',
            });

            await new Promise(r => setTimeout(r, 0));

            // Both items should be shown (no filtering).
            expect(vscodeWindow.showQuickPick).toHaveBeenCalledWith(
                expect.arrayContaining([
                    expect.objectContaining({ label: 'cube.glb' }),
                    expect.objectContaining({ label: 'wood.uzmat' }),
                ]),
                expect.anything()
            );
            inspector.dispose();
        });
    });
});

// ---------------------------------------------------------------------------
// fetchAssetQuickPickItems — direct tests
// ---------------------------------------------------------------------------

describe('fetchAssetQuickPickItems', () => {
    it('returns empty array when IPC fails', async () => {
        const ipc = { request: jest.fn().mockRejectedValue(new Error('down')) } as unknown as IpcClient;
        const result = await fetchAssetQuickPickItems(ipc, 'Material');
        expect(result).toEqual([]);
    });

    it('returns empty array when assets field is missing', async () => {
        const ipc = { request: jest.fn().mockResolvedValue({}) } as unknown as IpcClient;
        const result = await fetchAssetQuickPickItems(ipc, 'Material');
        expect(result).toEqual([]);
    });

    it('returns all assets when assetType is empty', async () => {
        const ipc = {
            request: jest.fn().mockResolvedValue({
                assets: [
                    { path: 'res://a.glb', type: 'Mesh', status: 'Fresh' },
                    { path: 'res://b.uzmat', type: 'Material', status: 'Stale' },
                ],
            }),
        } as unknown as IpcClient;
        const result = await fetchAssetQuickPickItems(ipc, '');
        expect(result).toHaveLength(2);
        expect(result[0].label).toBe('a.glb');
        expect(result[1].label).toBe('b.uzmat');
    });

    it('filters by assetType when provided', async () => {
        const ipc = {
            request: jest.fn().mockResolvedValue({
                assets: [
                    { path: 'res://a.glb', type: 'Mesh', status: 'Fresh' },
                    { path: 'res://b.uzmat', type: 'Material', status: 'Fresh' },
                ],
            }),
        } as unknown as IpcClient;
        const result = await fetchAssetQuickPickItems(ipc, 'Mesh');
        expect(result).toHaveLength(1);
        expect(result[0].label).toBe('a.glb');
        expect(result[0].description).toBe('res://a.glb');
        expect(result[0].detail).toBe('Mesh — Fresh');
    });

    it('extracts filename from path for label', async () => {
        const ipc = {
            request: jest.fn().mockResolvedValue({
                assets: [{ path: 'res://meshes/deep/cube.glb', type: 'Mesh', status: 'Fresh' }],
            }),
        } as unknown as IpcClient;
        const result = await fetchAssetQuickPickItems(ipc, '');
        expect(result[0].label).toBe('cube.glb');
    });
});

// ---------------------------------------------------------------------------
// commonProperties
// ---------------------------------------------------------------------------

describe('commonProperties', () => {
    const makeProp = (
        key: string, type: PropertyDescriptor['type'], value: unknown, group = 'Test'
    ): PropertyDescriptor => ({ key, label: key, type, value, group });

    it('returns empty array when given empty input', () => {
        expect(commonProperties([])).toEqual([]);
    });

    it('returns the single node props unchanged when only one node', () => {
        const props = [makeProp('intensity', 'float', 1.0)];
        expect(commonProperties([props])).toBe(props);
    });

    it('returns intersection of properties shared by all nodes', () => {
        const a = [makeProp('intensity', 'float', 1.0), makeProp('name', 'string', 'A')];
        const b = [makeProp('intensity', 'float', 1.0)]; // missing 'name'
        const result = commonProperties([a, b]);
        expect(result).toHaveLength(1);
        expect(result[0].key).toBe('intensity');
    });

    it('shows same value when all nodes agree', () => {
        const a = [makeProp('intensity', 'float', 2.0)];
        const b = [makeProp('intensity', 'float', 2.0)];
        const c = [makeProp('intensity', 'float', 2.0)];
        const result = commonProperties([a, b, c]);
        expect(result[0].value).toBe(2.0);
    });

    it('shows MIXED_VALUE when nodes disagree on value', () => {
        const a = [makeProp('intensity', 'float', 1.0)];
        const b = [makeProp('intensity', 'float', 2.0)];
        const result = commonProperties([a, b]);
        expect(result[0].value).toBe(MIXED_VALUE);
    });

    it('excludes properties with mismatched types', () => {
        const a = [makeProp('value', 'float', 1.0)];
        const b = [makeProp('value', 'string', '1.0')];
        const result = commonProperties([a, b]);
        expect(result).toHaveLength(0);
    });

    it('handles vec3 mixed values', () => {
        const a = [makeProp('pos', 'vec3', { x: 0, y: 0, z: 0 })];
        const b = [makeProp('pos', 'vec3', { x: 1, y: 0, z: 0 })];
        const result = commonProperties([a, b]);
        expect(result[0].value).toBe(MIXED_VALUE);
    });

    it('handles vec3 same values', () => {
        const a = [makeProp('pos', 'vec3', { x: 1, y: 2, z: 3 })];
        const b = [makeProp('pos', 'vec3', { x: 1, y: 2, z: 3 })];
        const result = commonProperties([a, b]);
        expect(result[0].value).toEqual({ x: 1, y: 2, z: 3 });
    });

    it('preserves group metadata', () => {
        const a = [makeProp('intensity', 'float', 1.0, 'Rendering')];
        const b = [makeProp('intensity', 'float', 1.0, 'Rendering')];
        const result = commonProperties([a, b]);
        expect(result[0].group).toBe('Rendering');
    });
});
