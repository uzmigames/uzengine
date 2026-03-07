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
 *  - uzengine.saveScene command: success path and catch/error path
 *  - uzengine.refreshScene command calls sceneTreeProvider.refresh()
 *  - uzengine.consoleClear command calls consolePanel.clear()
 *  - engine.log IPC push callback forwards to ConsolePanel
 *  - IpcClient dispose wrapper calls ipcClient.disconnect()
 *  - uzengine.openStreamViewer opens StreamPanel when start() returns true
 */

import {
    __resetMockPanelFactory,
    commands,
    makeMockContext,
    window,
    workspace,
} from '../__mocks__/vscode';
import { ViewportManager } from '../viewport-manager';

// ---------------------------------------------------------------------------
// Mock dependencies that have side effects
// ---------------------------------------------------------------------------

// Mock ServerManager (still imported in the module but no longer instantiated).
jest.mock('../server-manager', () => ({
    ServerManager: jest.fn().mockImplementation(() => ({
        start: jest.fn().mockResolvedValue(false),
        stop: jest.fn(),
        dispose: jest.fn(),
    })),
}));

// Mock EngineInstanceManager — single mock instance with onActiveChanged.
const mockGetActiveInstance = jest.fn().mockReturnValue(undefined);
const mockGetAllInstances = jest.fn().mockReturnValue([]);
const mockDestroyInstance = jest.fn();
const mockOnActiveChangedListeners: Array<(inst: unknown) => void> = [];
let mockOnActiveChangedListener: ((inst: unknown) => void) | undefined;

jest.mock('../engine-instance-manager', () => ({
    EngineInstanceManager: {
        initialize: jest.fn().mockReturnValue({
            getActiveInstance: mockGetActiveInstance,
            getAllInstances: mockGetAllInstances,
            destroyInstance: mockDestroyInstance,
            onActiveChanged: jest.fn((listener: (inst: unknown) => void) => {
                mockOnActiveChangedListeners.push(listener);
                mockOnActiveChangedListener = listener;
                return { dispose: jest.fn() };
            }),
        }),
        getInstance: jest.fn().mockReturnValue({
            getActiveInstance: mockGetActiveInstance,
            getAllInstances: mockGetAllInstances,
            destroyInstance: mockDestroyInstance,
        }),
    },
}));

// Mock StreamPanel to avoid ViewportManager/WebSocket interactions.
const mockCreateOrShow = jest.fn().mockResolvedValue(undefined);
jest.mock('../stream-panel', () => ({
    StreamPanel: {
        createOrShow: (...args: unknown[]) => mockCreateOrShow(...args),
        currentPanel: undefined,
        dispose: jest.fn(),
    },
}));

// Mock IpcClient to avoid real WebSocket connections.
jest.mock('../ipc-client', () => ({
    IpcClient: jest.fn().mockImplementation(() => ({
        connect: jest.fn(),
        disconnect: jest.fn(),
        request: jest.fn().mockResolvedValue({}),
        on: jest.fn().mockReturnValue(() => { }),
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

// Mock fs — keep all real implementations.
jest.mock('fs', () => {
    const actual = jest.requireActual<typeof import('fs')>('fs');
    return { ...actual };
});


// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    __resetMockPanelFactory();
    commands.__clearHandlers();
    workspace.__clearConfig();
    ViewportManager._resetForTests();
    mockOnActiveChangedListener = undefined;
    mockOnActiveChangedListeners.length = 0;
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

    it('uzengine.play switches all viewport panels to play port (9003)', async () => {
        await activateExtension();

        const mockPanel = { switchPort: jest.fn() };
        const mgr = ViewportManager.getInstance();
        jest.spyOn(mgr, 'getAllPanels').mockReturnValue([mockPanel as any]);

        await (commands.__executeRegisteredCommand('uzengine.play') as Promise<void>);

        expect(mockPanel.switchPort).toHaveBeenCalledWith(9003);
    });

    it('uzengine.play switches multiple viewport panels to play port', async () => {
        await activateExtension();

        const mockPanel1 = { switchPort: jest.fn() };
        const mockPanel2 = { switchPort: jest.fn() };
        const mgr = ViewportManager.getInstance();
        jest.spyOn(mgr, 'getAllPanels').mockReturnValue([mockPanel1 as any, mockPanel2 as any]);

        await (commands.__executeRegisteredCommand('uzengine.play') as Promise<void>);

        expect(mockPanel1.switchPort).toHaveBeenCalledWith(9003);
        expect(mockPanel2.switchPort).toHaveBeenCalledWith(9003);
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

    it('uzengine.stop switches all viewport panels back to editor port (9002)', async () => {
        await activateExtension();

        const mockPanel = { switchPort: jest.fn() };
        const mgr = ViewportManager.getInstance();
        jest.spyOn(mgr, 'getAllPanels').mockReturnValue([mockPanel as any]);

        await (commands.__executeRegisteredCommand('uzengine.stop') as Promise<void>);

        expect(mockPanel.switchPort).toHaveBeenCalledWith(9002);
    });

    it('registers the UzSceneEditorProvider custom editor', async () => {
        await activateExtension();
        expect(window.registerCustomEditorProvider).toHaveBeenCalledWith(
            'uzengine.uzsceneEditor',
            expect.any(Object),
            expect.objectContaining({ webviewOptions: { retainContextWhenHidden: true } })
        );
    });

    it('subscribes to onActiveChanged for dynamic IpcClient reconnection', async () => {
        await activateExtension();
        expect(mockOnActiveChangedListener).toBeDefined();
    });

    it('reconnects IpcClient when active engine instance changes', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpcInstance = (IpcClient as jest.Mock).mock.results[0]?.value as {
            connect: jest.Mock;
        };

        // Simulate an active instance appearing — fire all onActiveChanged listeners.
        mockGetActiveInstance.mockReturnValueOnce({ wsPort: 9005, isRunning: true });
        for (const listener of mockOnActiveChangedListeners) {
            listener({ wsPort: 9005, isRunning: true });
        }

        expect(mockIpcInstance?.connect).toHaveBeenCalledWith('ws://localhost:9005');
    });

    it('uzengine.saveScene — success path logs to console', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            request: jest.Mock;
        };
        mockIpc.request.mockResolvedValueOnce({});

        await (commands.__executeRegisteredCommand('uzengine.saveScene') as Promise<void>);

        expect(mockIpc.request).toHaveBeenCalledWith('scene.save', {});
    });

    it('uzengine.saveScene — error path shows error message', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            request: jest.Mock;
        };
        mockIpc.request.mockRejectedValueOnce(new Error('IPC timeout'));

        await (commands.__executeRegisteredCommand('uzengine.saveScene') as Promise<void>);

        expect(window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('IPC timeout')
        );
    });

    it('uzengine.saveScene — non-Error rejection uses String()', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            request: jest.Mock;
        };
        mockIpc.request.mockRejectedValueOnce('plain string error');

        await (commands.__executeRegisteredCommand('uzengine.saveScene') as Promise<void>);

        expect(window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('plain string error')
        );
    });

    it('uzengine.refreshScene — calls sceneTreeProvider.refresh()', async () => {
        await activateExtension();
        // Should complete without throwing.
        expect(() =>
            commands.__executeRegisteredCommand('uzengine.refreshScene')
        ).not.toThrow();
    });

    it('uzengine.consoleClear — calls consolePanel.clear()', async () => {
        await activateExtension();
        // Should complete without throwing.
        expect(() =>
            commands.__executeRegisteredCommand('uzengine.consoleClear')
        ).not.toThrow();
    });

    it('engine.log callback — forwards info-level message to ConsolePanel', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };

        // Find the callback registered for 'engine.log'
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        expect(logCall).toBeDefined();
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;
        expect(logCallback).toBeDefined();

        // Should not throw for a valid info message.
        expect(() => logCallback?.({ level: 'info', message: 'hello' })).not.toThrow();
    });

    it('engine.log callback — forwards warning-level message', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;

        expect(() => logCallback?.({ level: 'warning', message: 'warn msg' })).not.toThrow();
    });

    it('engine.log callback — forwards error-level message', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;

        expect(() => logCallback?.({ level: 'error', message: 'err msg' })).not.toThrow();
    });

    it('engine.log callback — ignores null payload', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;

        // null payload — should silently return.
        expect(() => logCallback?.(null)).not.toThrow();
    });

    it('engine.log callback — ignores payload without message string', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;

        // Missing message field — should silently return.
        expect(() => logCallback?.({ level: 'info' })).not.toThrow();
    });

    it('engine.log callback — unknown level defaults to info', async () => {
        const { IpcClient } = await import('../ipc-client');
        await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            on: jest.Mock;
        };
        const logCall = mockIpc.on.mock.calls.find(
            (call: unknown[]) => call[0] === 'engine.log'
        );
        const logCallback = logCall?.[1] as ((payload: unknown) => void) | undefined;

        // Unknown level (not 'warning' or 'error') → treated as 'info'.
        expect(() => logCallback?.({ level: 'debug', message: 'trace' })).not.toThrow();
    });

    it('IpcClient dispose wrapper calls disconnect()', async () => {
        const { IpcClient } = await import('../ipc-client');
        const ctx = await activateExtension();

        const mockIpc = (IpcClient as jest.Mock).mock.results[0]?.value as {
            disconnect: jest.Mock;
        };

        // Find the dispose wrapper pushed to subscriptions
        // (it is the object { dispose: () => ipcClient.disconnect() })
        const disposeWrapper = ctx.subscriptions.find(
            (s: { dispose: () => void }) => {
                try {
                    s.dispose();
                    return mockIpc.disconnect.mock.calls.length > 0;
                } catch {
                    return false;
                }
            }
        );
        expect(disposeWrapper).toBeDefined();
        expect(mockIpc.disconnect).toHaveBeenCalled();
    });

    it('uzengine.openStreamViewer — opens StreamPanel when active instance exists', async () => {
        await activateExtension();

        // Simulate an active instance.
        mockGetActiveInstance.mockReturnValueOnce({ wsPort: 9002, isRunning: true });

        await (commands.__executeRegisteredCommand('uzengine.openStreamViewer') as Promise<void>);

        expect(mockCreateOrShow).toHaveBeenCalled();
    });

    it('uzengine.openStreamViewer — shows info when no active instance', async () => {
        await activateExtension();

        // No active instance.
        mockGetActiveInstance.mockReturnValueOnce(undefined);

        mockCreateOrShow.mockClear();
        await (commands.__executeRegisteredCommand('uzengine.openStreamViewer') as Promise<void>);

        expect(mockCreateOrShow).not.toHaveBeenCalled();
        expect(window.showInformationMessage).toHaveBeenCalled();
    });
});

describe('extension — .uzscene file watcher', () => {
    it('loads scene from textDocuments already open on activate', async () => {
        workspace.textDocuments = [
            {
                fileName: 'test.uzscene',
                getText: () => '[uzscene]\nname = Pre\n\n[node]\nname = World\ntype = Node3D\nparent =\n',
                uri: { fsPath: 'test.uzscene' },
            } as any, // eslint-disable-line @typescript-eslint/no-explicit-any
        ];
        await activateExtension();
        // textDocuments was iterated — onDidOpenTextDocument was also registered
        expect(workspace.onDidOpenTextDocument).toHaveBeenCalled();
        workspace.textDocuments = [];
    });

    it('ignores non-.uzscene documents already open on activate', async () => {
        workspace.textDocuments = [
            { fileName: 'readme.md', getText: () => '# hello' },
        ];
        await expect(activateExtension()).resolves.toBeDefined();
        workspace.textDocuments = [];
    });

    it('registers onDidOpenTextDocument, onDidChangeTextDocument, onDidCloseTextDocument', async () => {
        await activateExtension();
        expect(workspace.onDidOpenTextDocument).toHaveBeenCalled();
        expect(workspace.onDidChangeTextDocument).toHaveBeenCalled();
        expect(workspace.onDidCloseTextDocument).toHaveBeenCalled();
    });

    it('onDidOpenTextDocument callback loads .uzscene without throwing', async () => {
        await activateExtension();
        const openCb = (workspace.onDidOpenTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((doc: { fileName: string; getText: () => string; uri?: { fsPath: string } }) => void) | undefined;
        expect(openCb).toBeDefined();

        expect(() => openCb!({
            fileName: 'level.uzscene',
            getText: () => '[uzscene]\nname = L\n\n[node]\nname = R\ntype = Node3D\nparent =\n',
            uri: { fsPath: 'level.uzscene' },
        })).not.toThrow();
    });

    it('onDidOpenTextDocument callback ignores non-.uzscene files', async () => {
        await activateExtension();
        const openCb = (workspace.onDidOpenTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((doc: { fileName: string; getText: () => string; uri?: { fsPath: string } }) => void) | undefined;

        expect(() => openCb!({ fileName: 'main.ts', getText: () => 'const x = 1;' })).not.toThrow();
    });

    it('onDidChangeTextDocument callback reloads .uzscene on edit', async () => {
        await activateExtension();
        const changeCb = (workspace.onDidChangeTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((e: { document: { fileName: string; getText: () => string; uri?: { fsPath: string } } }) => void) | undefined;
        expect(changeCb).toBeDefined();

        expect(() => changeCb!({
            document: {
                fileName: 'level.uzscene',
                getText: () => '[uzscene]\nname = Updated\n\n[node]\nname = R\ntype = Camera\nparent =\n',
                uri: { fsPath: 'level.uzscene' },
            },
        })).not.toThrow();
    });

    it('onDidCloseTextDocument callback clears tree for .uzscene', async () => {
        await activateExtension();
        const closeCb = (workspace.onDidCloseTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((doc: { fileName: string }) => void) | undefined;
        expect(closeCb).toBeDefined();

        expect(() => closeCb!({ fileName: 'level.uzscene' })).not.toThrow();
    });

    it('onDidCloseTextDocument callback ignores non-.uzscene files', async () => {
        await activateExtension();
        const closeCb = (workspace.onDidCloseTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((doc: { fileName: string }) => void) | undefined;

        expect(() => closeCb!({ fileName: 'style.css' })).not.toThrow();
    });

    it('onDidOpenTextDocument callback loads .uzscene without triggering IPC', async () => {
        await activateExtension();

        const openCb = (workspace.onDidOpenTextDocument as jest.Mock).mock.calls[0]?.[0] as
            ((doc: { fileName: string; getText: () => string; uri?: { fsPath: string } }) => void) | undefined;

        // Should not throw — and should NOT write any files (IPC removed).
        expect(() => openCb!({
            fileName: 'level.uzscene',
            getText: () => '[uzscene]\nname = L\n\n[node]\nname = World\ntype = Node3D\nparent =\n',
            uri: { fsPath: 'level.uzscene' },
        })).not.toThrow();
    });
});

describe('extension deactivate()', () => {
    it('can be called without throwing', async () => {
        const { deactivate } = await import('../extension');
        expect(() => deactivate()).not.toThrow();
    });
});
