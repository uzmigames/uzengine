/**
 * stream-panel.test.ts — Unit tests for StreamPanel.
 *
 * Coverage targets (≥95% line + branch):
 *  1.  createOrShow() when existing panel is open and not disposed → reveal
 *  2.  createOrShow() with active text editor (non-null viewColumn) → uses it
 *  3.  createOrShow() when currentPanel is undefined → creates new
 *  4.  createOrShow() when currentPanel is disposed → creates new
 *  5.  createOrShow() ViewportManager.getInstance() throws → error message
 *  6.  createOrShow() manager.createViewport succeeds → sets currentPanel
 *  7.  createOrShow() disposeSub fires → clears currentPanel
 *  8.  createOrShow() manager.createViewport throws Error → error message
 *  9.  createOrShow() manager.createViewport throws non-Error → error message
 *  10. dispose() with currentPanel defined → calls panel.dispose()
 *  11. dispose() with currentPanel undefined → no-op
 */

import { jest } from '@jest/globals';

// ---------------------------------------------------------------------------
// Mock ViewportManager before importing StreamPanel
// ---------------------------------------------------------------------------

jest.mock('../viewport-manager', () => ({
    ViewportManager: {
        getInstance: jest.fn(),
    },
}));

// ---------------------------------------------------------------------------
// Imports
// ---------------------------------------------------------------------------

import {
    window as vscodeWindow,
    workspace,
    ViewColumn,
    makeMockContext,
    makeMockWebviewPanel,
} from '../__mocks__/vscode';

import { ViewportManager } from '../viewport-manager';
import { StreamPanel } from '../stream-panel';

// ---------------------------------------------------------------------------
// Helper: strongly typed mock of ViewportManager getInstance return value
// ---------------------------------------------------------------------------

/** Returns the getInstance mock with unknown-based return type to avoid strict overload issues. */
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AnyMock = jest.Mock<(...args: any[]) => any>;

function getGetInstanceMock(): AnyMock {
    return ViewportManager.getInstance as unknown as AnyMock;
}

/** Creates a mock ViewportPanel-like object (what createViewport resolves to). */
function makeMockViewportPanel() {
    const webviewPanel = makeMockWebviewPanel();
    return {
        ...webviewPanel,
        isDisposed: false,
        viewportId: 1,
        reveal: jest.fn(),
        dispose: jest.fn(),
    };
}

/** Creates a mock ViewportManager with a controllable createViewport. */
function makeManager(createViewportImpl?: AnyMock) {
    return {
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        createViewport: (createViewportImpl ?? jest.fn()) as AnyMock,
    };
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    workspace.__clearConfig();
    StreamPanel.currentPanel = undefined;
    vscodeWindow.activeTextEditor = undefined;
});

afterEach(() => {
    StreamPanel.currentPanel = undefined;
    vscodeWindow.activeTextEditor = undefined;
});

// Convenience: run createOrShow with dummy Uri and context
async function callCreateOrShow(ctx: ReturnType<typeof makeMockContext>) {
    return StreamPanel.createOrShow(
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        { fsPath: '/mock/ext', scheme: 'file', path: '/mock/ext' } as any,
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        ctx as any
    );
}

// ---------------------------------------------------------------------------
// createOrShow() — reveal existing non-disposed panel
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — existing panel, not disposed', () => {
    it('reveals the existing panel when currentPanel is set and not disposed', async () => {
        const mockPanel = makeMockViewportPanel();
        mockPanel.isDisposed = false;
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        StreamPanel.currentPanel = mockPanel as any;

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mockPanel.reveal).toHaveBeenCalledTimes(1);
        // ViewportManager.getInstance should NOT be called
        expect(getGetInstanceMock()).not.toHaveBeenCalled();
    });

    it('uses ViewColumn.One when activeTextEditor is undefined', async () => {
        const mockPanel = makeMockViewportPanel();
        mockPanel.isDisposed = false;
        vscodeWindow.activeTextEditor = undefined;
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        StreamPanel.currentPanel = mockPanel as any;

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mockPanel.reveal).toHaveBeenCalledWith(ViewColumn.One);
    });

    it('uses activeTextEditor.viewColumn when editor is active', async () => {
        const mockPanel = makeMockViewportPanel();
        mockPanel.isDisposed = false;
        vscodeWindow.activeTextEditor = { viewColumn: ViewColumn.Two };
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        StreamPanel.currentPanel = mockPanel as any;

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mockPanel.reveal).toHaveBeenCalledWith(ViewColumn.Two);
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — current panel is disposed → creates new
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — disposed panel creates new', () => {
    it('creates a new panel when currentPanel.isDisposed is true', async () => {
        const disposedPanel = makeMockViewportPanel();
        disposedPanel.isDisposed = true;
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        StreamPanel.currentPanel = disposedPanel as any;

        const newPanel = makeMockViewportPanel();
        newPanel.isDisposed = false;

        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        // Should NOT have called reveal on the disposed panel
        expect(disposedPanel.reveal).not.toHaveBeenCalled();
        // Should have created a new viewport
        expect(mgr.createViewport).toHaveBeenCalledTimes(1);
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(newPanel as any);
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — no current panel → creates new
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — no current panel', () => {
    it('calls ViewportManager.getInstance() and createViewport()', async () => {
        StreamPanel.currentPanel = undefined;

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(getGetInstanceMock()).toHaveBeenCalledTimes(1);
        expect(mgr.createViewport).toHaveBeenCalledTimes(1);
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(newPanel as any);
    });

    it('passes correct config to createViewport (custom fps + quality)', async () => {
        StreamPanel.currentPanel = undefined;
        workspace.__setConfig('uzengine', 'viewport.sceneTargetFps', 30);
        workspace.__setConfig('uzengine', 'viewport.sceneJpegQuality', 75);

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mgr.createViewport).toHaveBeenCalledWith(
            expect.objectContaining({
                type: 'scene',
                title: 'UzEngine Scene',
                width: 1280,
                height: 720,
                targetFps: 30,
                jpegQuality: 75,
            }),
            expect.anything()
        );
    });

    it('uses default targetFps=60 and jpegQuality=85 when not configured', async () => {
        StreamPanel.currentPanel = undefined;
        workspace.__clearConfig();

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mgr.createViewport).toHaveBeenCalledWith(
            expect.objectContaining({
                targetFps: 60,
                jpegQuality: 85,
            }),
            expect.anything()
        );
    });

    it('passes debugName: "Scene Viewport" to createViewport', async () => {
        StreamPanel.currentPanel = undefined;

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(mgr.createViewport).toHaveBeenCalledWith(
            expect.objectContaining({ debugName: 'Scene Viewport' }),
            expect.anything()
        );
    });

    it('pushes a disposeSub to context.subscriptions', async () => {
        StreamPanel.currentPanel = undefined;

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(ctx.subscriptions.length).toBeGreaterThan(0);
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — disposeSub fires → clears currentPanel
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — disposeSub cleanup', () => {
    it('sets currentPanel to undefined when disposeSub.dispose() is called', async () => {
        StreamPanel.currentPanel = undefined;

        const newPanel = makeMockViewportPanel();
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(newPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(newPanel as any);

        // The disposeSub is the last item pushed to context.subscriptions
        const disposeSub = ctx.subscriptions[ctx.subscriptions.length - 1];
        expect(disposeSub).toBeDefined();
        expect(typeof disposeSub.dispose).toBe('function');

        disposeSub.dispose();
        expect(StreamPanel.currentPanel).toBeUndefined();
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — ViewportManager.getInstance() throws
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — getInstance throws', () => {
    it('shows an error message and returns early', async () => {
        StreamPanel.currentPanel = undefined;
        getGetInstanceMock().mockImplementation(() => {
            throw new Error('ViewportManager has not been initialised');
        });

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('ViewportManager is not initialised')
        );
        expect(StreamPanel.currentPanel).toBeUndefined();
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — createViewport throws Error
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — createViewport throws Error', () => {
    it('shows error message with err.message when Error is thrown', async () => {
        StreamPanel.currentPanel = undefined;

        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockRejectedValue(new Error('IPC timeout'))
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('IPC timeout')
        );
        expect(StreamPanel.currentPanel).toBeUndefined();
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — createViewport throws non-Error
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — createViewport throws non-Error', () => {
    it('shows error message with String(err) for string throw', async () => {
        StreamPanel.currentPanel = undefined;

        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockRejectedValue('something went wrong')
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('something went wrong')
        );
        expect(StreamPanel.currentPanel).toBeUndefined();
    });

    it('shows error message with String(err) for numeric throw', async () => {
        StreamPanel.currentPanel = undefined;

        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockRejectedValue(42)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('42')
        );
    });
});

// ---------------------------------------------------------------------------
// dispose() — with currentPanel defined
// ---------------------------------------------------------------------------

describe('StreamPanel.dispose() — with currentPanel', () => {
    it('calls dispose() on the current panel and clears the reference', () => {
        const mockPanel = makeMockViewportPanel();
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        StreamPanel.currentPanel = mockPanel as any;

        StreamPanel.dispose();

        expect(mockPanel.dispose).toHaveBeenCalledTimes(1);
        expect(StreamPanel.currentPanel).toBeUndefined();
    });
});

// ---------------------------------------------------------------------------
// dispose() — with currentPanel undefined
// ---------------------------------------------------------------------------

describe('StreamPanel.dispose() — no current panel', () => {
    it('does not throw when currentPanel is undefined', () => {
        StreamPanel.currentPanel = undefined;
        expect(() => StreamPanel.dispose()).not.toThrow();
        expect(StreamPanel.currentPanel).toBeUndefined();
    });
});

// ---------------------------------------------------------------------------
// createOrShow() — legacy direct-connect (viewport.create IPC timed out)
// ---------------------------------------------------------------------------

describe('StreamPanel.createOrShow() — legacy mode (viewportId === 0)', () => {
    it('calls panel.initialize(0, 9002) when viewportId is INVALID (0)', async () => {
        StreamPanel.currentPanel = undefined;
        workspace.__clearConfig();

        const legacyPanel = { ...makeMockViewportPanel(), viewportId: 0, initialize: jest.fn() };
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(legacyPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(legacyPanel.initialize).toHaveBeenCalledWith(0, 9002);
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(legacyPanel as any);
    });

    it('uses custom server.wsPort from config in legacy mode', async () => {
        StreamPanel.currentPanel = undefined;
        workspace.__setConfig('uzengine', 'server.wsPort', 8765);

        const legacyPanel = { ...makeMockViewportPanel(), viewportId: 0, initialize: jest.fn() };
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(legacyPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(legacyPanel.initialize).toHaveBeenCalledWith(0, 8765);
    });

    it('does NOT call initialize when viewportId is valid (non-zero)', async () => {
        StreamPanel.currentPanel = undefined;

        const normalPanel = { ...makeMockViewportPanel(), viewportId: 1, initialize: jest.fn() };
        const mgr = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(normalPanel)
        );
        getGetInstanceMock().mockReturnValue(mgr);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        expect(normalPanel.initialize).not.toHaveBeenCalled();
    });
});

// ---------------------------------------------------------------------------
// Round-trip: create → dispose → create again
// ---------------------------------------------------------------------------

describe('StreamPanel — round-trip: create, dispose, create again', () => {
    it('creates a fresh panel after disposing the previous one via StreamPanel.dispose()', async () => {
        const panel1 = makeMockViewportPanel();
        const mgr1 = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(panel1)
        );
        getGetInstanceMock().mockReturnValue(mgr1);

        const ctx = makeMockContext();
        await callCreateOrShow(ctx);

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(panel1 as any);

        StreamPanel.dispose();
        expect(StreamPanel.currentPanel).toBeUndefined();

        const panel2 = makeMockViewportPanel();
        const mgr2 = makeManager(
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            jest.fn<any>().mockResolvedValue(panel2)
        );
        getGetInstanceMock().mockReturnValue(mgr2);

        await callCreateOrShow(ctx);

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        expect(StreamPanel.currentPanel).toBe(panel2 as any);
        expect(mgr2.createViewport).toHaveBeenCalledTimes(1);
    });
});
