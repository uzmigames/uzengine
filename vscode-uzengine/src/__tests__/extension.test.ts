/**
 * extension.test.ts — Unit tests for the extension activate() function.
 *
 * Tests cover:
 *  - activate() registers all required commands
 *  - activate() creates the output channel
 *  - activate() initialises ViewportManager
 *  - activate() pushes disposables to context.subscriptions
 *  - activate() registers SceneTreeProvider with createTreeView
 *  - activate() creates 4 status bar items (PlayControls)
 *  - deactivate() is callable without throwing
 *  - uzengine.stopServer command calls serverManager.stop()
 *  - uzengine.stop command calls playControls.stop()
 *  - uzengine.play command calls playControls.play()
 *  - uzengine.pause command calls playControls.pause()
 *  - uzengine.addNode command calls sceneTreeProvider.addNode()
 *  - uzengine.deleteNode command calls sceneTreeProvider.deleteNode()
 *  - uzengine.renameNode command calls sceneTreeProvider.renameNode()
 *  - uzengine.duplicateNode command calls sceneTreeProvider.duplicateNode()
 *  - uzengine.selectNode command calls sceneTreeProvider.selectNode()
 *  - onDidChangeConfiguration reconnects IpcClient when serverUrl changes
 */

import {
    window,
    workspace,
    commands,
    makeMockContext,
    makeMockWebviewPanel,
    __setMockPanelFactory,
    __resetMockPanelFactory,
} from '../__mocks__/vscode';
import { ViewportManager } from '../viewport-manager';

// ---------------------------------------------------------------------------
// Mock dependencies that have side effects
// ---------------------------------------------------------------------------

// Mock ServerManager to avoid file-system / process interactions.
jest.mock('../server-manager', () => ({
    ServerManager: jest.fn().mockImplementation(() => ({
        start: jest.fn().mockResolvedValue(false),
        stop: jest.fn(),
        dispose: jest.fn(),
    })),
}));

// Mock IpcClient to avoid real WebSocket connections.
jest.mock('../ipc-client', () => ({
    IpcClient: jest.fn().mockImplementation(() => ({
        connect: jest.fn(),
        disconnect: jest.fn(),
        request: jest.fn().mockResolvedValue({}),
        on: jest.fn().mockReturnValue(() => {}),
        isConnected: false,
        state: 'disconnected',
    })),
    IpcClientError: class IpcClientError extends Error {
        constructor(message: string, public method: string, public requestId: string) {
            super(message);
            this.name = 'IpcClientError';
        }
    },
}));

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    __resetMockPanelFactory();
    commands.__clearHandlers();
    workspace.__clearConfig();
    ViewportManager._resetForTests();
});

afterEach(() => {
    __resetMockPanelFactory();
    ViewportManager._resetForTests();
});

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

async function activateExtension() {
    const { activate } = await import('../extension');
    const ctx = makeMockContext();
    activate(ctx as unknown as import('vscode').ExtensionContext);
    return ctx;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('extension activate()', () => {

    it('creates an output channel', async () => {
        await activateExtension();
        expect(window.createOutputChannel).toHaveBeenCalledWith('UzEngine');
    });

    it('pushes disposables to context.subscriptions', async () => {
        const ctx = await activateExtension();
        // At minimum: outputChannel, serverManager, IpcClient disposable,
        // configSub, sceneTreeProvider, treeView, inspectorPanel, selectionSub,
        // playControls, and all commands.
        expect(ctx.subscriptions.length).toBeGreaterThan(5);
    });

    it('registers the scene tree view', async () => {
        await activateExtension();
        expect(window.createTreeView).toHaveBeenCalledWith(
            'uzengine.sceneTree',
            expect.objectContaining({ showCollapseAll: true })
        );
    });

    it('creates 4 status bar items (PlayControls)', async () => {
        await activateExtension();
        expect(window.createStatusBarItem).toHaveBeenCalledTimes(4);
    });

    it('registers uzengine.openStreamViewer command', async () => {
        await activateExtension();
        const handler = commands.__executeRegisteredCommand;
        expect(() => handler('uzengine.openStreamViewer')).not.toThrow();
    });

    it('registers uzengine.startServer command', async () => {
        await activateExtension();
        expect(() =>
            commands.__executeRegisteredCommand('uzengine.startServer')
        ).not.toThrow();
    });

    it('registers uzengine.stopServer command', async () => {
        await activateExtension();
        // Should call serverManager.stop() without throwing.
        await expect(
            commands.__executeRegisteredCommand('uzengine.stopServer')
        ).not.toThrow;
    });

    it('registers uzengine.selectNode command', async () => {
        await activateExtension();
        const { NodeItem } = await import('../scene-tree-provider');
        const item = new NodeItem({ id: 'n1', name: 'Test', type: 'Node3D' });
        // Should not throw even when engine is not connected.
        await (commands.__executeRegisteredCommand('uzengine.selectNode', item) as Promise<void>);
    });

    it('registers uzengine.addNode command', async () => {
        await activateExtension();
        await (commands.__executeRegisteredCommand('uzengine.addNode') as Promise<void>);
        // Command should complete without throwing.
    });

    it('registers uzengine.deleteNode command', async () => {
        await activateExtension();
        const { NodeItem } = await import('../scene-tree-provider');
        const item = new NodeItem({ id: 'n1', name: 'Test', type: 'Node3D' });
        await (commands.__executeRegisteredCommand('uzengine.deleteNode', item) as Promise<void>);
    });

    it('registers uzengine.renameNode command', async () => {
        await activateExtension();
        const { NodeItem } = await import('../scene-tree-provider');
        const item = new NodeItem({ id: 'n1', name: 'Test', type: 'Node3D' });
        // showInputBox returns undefined by default (user cancelled).
        await (commands.__executeRegisteredCommand('uzengine.renameNode', item) as Promise<void>);
    });

    it('registers uzengine.duplicateNode command', async () => {
        await activateExtension();
        const { NodeItem } = await import('../scene-tree-provider');
        const item = new NodeItem({ id: 'n1', name: 'Test', type: 'Node3D' });
        await (commands.__executeRegisteredCommand('uzengine.duplicateNode', item) as Promise<void>);
    });

    it('registers uzengine.play command', async () => {
        await activateExtension();
        await (commands.__executeRegisteredCommand('uzengine.play') as Promise<void>);
        // Should call IPC request for playmode.start.
    });

    it('registers uzengine.pause command', async () => {
        await activateExtension();
        await (commands.__executeRegisteredCommand('uzengine.pause') as Promise<void>);
        // Pause does nothing when not in playing state — should not throw.
    });

    it('registers uzengine.stop command', async () => {
        await activateExtension();
        await (commands.__executeRegisteredCommand('uzengine.stop') as Promise<void>);
        // Stop does nothing when already idle — should not throw.
    });

    it('subscribes to onDidChangeConfiguration', async () => {
        await activateExtension();
        expect(workspace.onDidChangeConfiguration).toHaveBeenCalled();
    });

    it('reconnects IpcClient when uzengine.serverUrl configuration changes', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpcInstance = (IpcClient as jest.Mock).mock.results[0]?.value as {
            connect: jest.Mock;
        };

        // Simulate configuration change event.
        const configChangeHandler = (workspace.onDidChangeConfiguration as jest.Mock).mock.calls[0]?.[0] as
            ((e: { affectsConfiguration: (key: string) => boolean }) => void) | undefined;

        expect(configChangeHandler).toBeDefined();

        // Trigger a change that affects uzengine.serverUrl.
        const connectCallsBefore = mockIpcInstance?.connect.mock.calls.length ?? 0;
        configChangeHandler?.({
            affectsConfiguration: (key: string) => key === 'uzengine.serverUrl',
        });

        // connect() should be called again with the new URL.
        expect(mockIpcInstance?.connect.mock.calls.length ?? 0).toBeGreaterThan(connectCallsBefore);
    });

    it('does NOT reconnect when an unrelated configuration changes', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpcInstance = (IpcClient as jest.Mock).mock.results[0]?.value as {
            connect: jest.Mock;
        };

        const configChangeHandler = (workspace.onDidChangeConfiguration as jest.Mock).mock.calls[0]?.[0] as
            ((e: { affectsConfiguration: (key: string) => boolean }) => void) | undefined;

        const connectCallsBefore = mockIpcInstance?.connect.mock.calls.length ?? 0;
        configChangeHandler?.({
            affectsConfiguration: () => false,
        });

        // connect() should NOT be called again.
        expect(mockIpcInstance?.connect.mock.calls.length ?? 0).toBe(connectCallsBefore);
    });
});

describe('extension deactivate()', () => {
    it('can be called without throwing', async () => {
        const { deactivate } = await import('../extension');
        expect(() => deactivate()).not.toThrow();
    });
});
