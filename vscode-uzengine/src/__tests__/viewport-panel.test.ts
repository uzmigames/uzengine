/**
 * viewport-panel.test.ts — Unit tests for ViewportPanel.
 *
 * Tests cover:
 *  - Construction creates a WebviewPanel with correct options
 *  - Loading HTML is set immediately on construction
 *  - initialize() updates the webview HTML with the stream canvas
 *  - Tab visibility changes notify the host
 *  - Tab disposal notifies the host and marks state as destroyed
 *  - receiveFrame() posts the frame to the webview (when visible)
 *  - receiveFrame() is a no-op when panel is disposed or not visible
 *  - markActive() / markSuspended() update state correctly
 *  - dispose() is idempotent (double-dispose is safe)
 *  - reveal() delegates to the underlying panel
 */

import {
    ViewColumn,
    __resetMockPanelFactory,
    __setMockPanelFactory,
    makeMockContext,
    makeMockWebviewPanel,
    window as vscodeWindow,
} from '../__mocks__/vscode';
import { ViewportPanel, ViewportPanelHost } from '../viewport-panel';
import { VIEWPORT_ID_INVALID, ViewportConfig } from '../viewport-types';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const defaultConfig: ViewportConfig = {
    type: 'scene',
    title: 'Test Scene',
    width: 1280,
    height: 720,
    targetFps: 60,
    jpegQuality: 85,
    debugName: 'Test Viewport',
};

function makeHost(): ViewportPanelHost & {
    visibilityChanges: Array<{ panel: ViewportPanel; visible: boolean }>;
    disposals: ViewportPanel[];
} {
    const visibilityChanges: Array<{ panel: ViewportPanel; visible: boolean }> = [];
    const disposals: ViewportPanel[] = [];

    return {
        visibilityChanges,
        disposals,
        onPanelVisibilityChanged(panel: ViewportPanel, visible: boolean): void {
            visibilityChanges.push({ panel, visible });
        },
        onPanelDisposed(panel: ViewportPanel): void {
            disposals.push(panel);
        },
    };
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

let mockPanel: ReturnType<typeof makeMockWebviewPanel>;

beforeEach(() => {
    jest.clearAllMocks();
    __resetMockPanelFactory();
    mockPanel = makeMockWebviewPanel();
    __setMockPanelFactory(() => mockPanel);
});

afterEach(() => {
    __resetMockPanelFactory();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('ViewportPanel', () => {

    describe('construction', () => {
        it('creates a WebviewPanel with correct viewType containing the viewport type', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(vscodeWindow.createWebviewPanel).toHaveBeenCalledWith(
                'uzengine.viewport.scene',
                'Test Scene',
                ViewColumn.One,
                expect.objectContaining({
                    enableScripts: true,
                    retainContextWhenHidden: true,
                })
            );
        });

        it('sets loading HTML immediately (before initialize())', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(mockPanel.webview.html).toContain('Initialising scene viewport');
            expect(mockPanel.webview.html).toContain('Test Scene');
        });

        it('registers onDidChangeViewState listener', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(mockPanel.onDidChangeViewState).toHaveBeenCalledTimes(1);
        });

        it('registers onDidDispose listener', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(mockPanel.onDidDispose).toHaveBeenCalledTimes(1);
        });

        it('starts with viewportId === VIEWPORT_ID_INVALID', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(panel.viewportId).toBe(VIEWPORT_ID_INVALID);
        });

        it('starts with state === inactive', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(panel.state).toBe('inactive');
        });

        it('adds itself to context.subscriptions', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            expect(ctx.subscriptions).toContain(panel);
        });
    });

    describe('initialize()', () => {
        it('sets the viewportId to the provided value', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.initialize(42, 9002);
            expect(panel.viewportId).toBe(42);
        });

        it('updates webview HTML with stream canvas content', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.initialize(1, 9002);

            expect(mockPanel.webview.html).toContain('<canvas');
            expect(mockPanel.webview.html).toContain('viewport-canvas');
        });

        it('embeds the viewport_id in the stream HTML', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.initialize(7, 9002);

            expect(mockPanel.webview.html).toContain('VIEWPORT_ID = 7');
        });

        it('embeds the ws_port in the stream HTML', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.initialize(1, 9050);

            expect(mockPanel.webview.html).toContain('9050');
        });

        it('is a no-op when the panel is already disposed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.dispose();

            // Should not throw or change state.
            expect(() => panel.initialize(1, 9002)).not.toThrow();
            expect(panel.viewportId).toBe(VIEWPORT_ID_INVALID);
        });
    });

    describe('markActive() / markSuspended()', () => {
        it('markActive() sets state to active', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);

            panel.markActive();
            expect(panel.state).toBe('active');
        });

        it('markSuspended() sets state to suspended', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);
            panel.markActive();

            panel.markSuspended();
            expect(panel.state).toBe('suspended');
        });

        it('markActive() is a no-op when disposed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.dispose();

            expect(() => panel.markActive()).not.toThrow();
            expect(panel.state).toBe('destroyed');
        });

        it('markSuspended() is a no-op when disposed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.dispose();

            expect(() => panel.markSuspended()).not.toThrow();
            expect(panel.state).toBe('destroyed');
        });
    });

    describe('tab visibility changes → host notification', () => {
        it('notifies host with visible=false when panel becomes hidden', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel._simulateVisibilityChange(false);

            expect(host.visibilityChanges).toHaveLength(1);
            expect(host.visibilityChanges[0]).toEqual({ panel, visible: false });
        });

        it('notifies host with visible=true when panel becomes visible', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel._simulateVisibilityChange(true);

            expect(host.visibilityChanges).toHaveLength(1);
            expect(host.visibilityChanges[0]).toEqual({ panel, visible: true });
        });

        it('fires multiple notifications for multiple visibility changes', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel._simulateVisibilityChange(false);
            mockPanel._simulateVisibilityChange(true);
            mockPanel._simulateVisibilityChange(false);

            expect(host.visibilityChanges).toHaveLength(3);
            expect(host.visibilityChanges.map(e => e.visible)).toEqual([false, true, false]);
            host.visibilityChanges.forEach(e => expect(e.panel).toBe(panel));
        });
    });

    describe('tab dispose → host notification', () => {
        it('notifies host when the underlying panel is disposed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel._simulateDispose();

            expect(host.disposals).toHaveLength(1);
            expect(host.disposals[0]).toBe(panel);
        });

        it('sets state to destroyed when the underlying panel fires onDidDispose', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel._simulateDispose();

            expect(panel.state).toBe('destroyed');
        });
    });

    describe('receiveFrame()', () => {
        it('posts a frame message to the webview when panel is visible', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);

            // Panel starts visible (mock default).
            mockPanel.visible = true;

            const jpeg = new Uint8Array([0xff, 0xd8, 0xff, 0xe0]);
            panel.receiveFrame(jpeg);

            expect(mockPanel.webview.postMessage).toHaveBeenCalledWith({
                type: 'frame',
                data: expect.any(Uint8Array),
            });
        });

        it('includes all bytes from the jpeg in the posted data array', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);
            mockPanel.visible = true;

            const jpeg = new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10]);
            panel.receiveFrame(jpeg);

            const call = (mockPanel.webview.postMessage as jest.Mock).mock.calls[0][0] as {
                type: string;
                data: Uint8Array;
            };
            expect(Array.from(call.data)).toEqual([0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10]);
        });

        it('does not post when panel is not visible', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);
            mockPanel.visible = false;

            panel.receiveFrame(new Uint8Array([0xff, 0xd8]));

            expect(mockPanel.webview.postMessage).not.toHaveBeenCalled();
        });

        it('does not post when panel is disposed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);
            panel.initialize(1, 9002);
            panel.dispose();

            panel.receiveFrame(new Uint8Array([0xff, 0xd8]));

            expect(mockPanel.webview.postMessage).not.toHaveBeenCalled();
        });
    });

    describe('dispose()', () => {
        it('sets isDisposed to true', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.dispose();
            expect(panel.isDisposed).toBe(true);
        });

        it('sets state to destroyed', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.dispose();
            expect(panel.state).toBe('destroyed');
        });

        it('disposes the underlying VSCode panel', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.dispose();
            expect(mockPanel.dispose).toHaveBeenCalledTimes(1);
        });

        it('is idempotent — calling dispose() twice does not throw or double-dispose', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.dispose();
            expect(() => panel.dispose()).not.toThrow();
            // The underlying panel is disposed only once.
            expect(mockPanel.dispose).toHaveBeenCalledTimes(1);
        });
    });

    describe('reveal()', () => {
        it('delegates to the underlying panel reveal method', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.reveal(ViewColumn.Two);
            expect(mockPanel.reveal).toHaveBeenCalledWith(ViewColumn.Two);
        });

        it('reveals with undefined column when none specified', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            panel.reveal();
            expect(mockPanel.reveal).toHaveBeenCalledWith(undefined);
        });
    });

    describe('isVisible accessor', () => {
        it('reflects the underlying panel visible property', () => {
            const host = makeHost();
            const ctx = makeMockContext();
            const panel = new ViewportPanel(defaultConfig, host, ctx as unknown as ConstructorParameters<typeof ViewportPanel>[2]);

            mockPanel.visible = true;
            expect(panel.isVisible).toBe(true);

            mockPanel.visible = false;
            expect(panel.isVisible).toBe(false);
        });
    });
});

// ---------------------------------------------------------------------------
// buildViewportStreamHtml — camera input capture tests
// ---------------------------------------------------------------------------

import { buildViewportStreamHtml } from '../viewport-panel';

describe('buildViewportStreamHtml — camera input capture', () => {
    const baseOpts = {
        nonce: 'testnonce123',
        title: 'Test Viewport',
        wsPort: 9002,
        viewportId: 1,
    };

    it('contains camera_input message type for WS send', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('camera_input');
    });

    it('contains requestPointerLock for pointer lock on right-click', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('requestPointerLock');
    });

    it('contains contextmenu event listener to suppress the context menu', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('contextmenu');
    });

    it('contains pointerlockchange event listener to reset state on lock exit', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('pointerlockchange');
    });

    it('contains wheel event listener for scroll-to-zoom input', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('scrollDelta');
    });

    it('contains key declarations for WASD+QE fly keys', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('w:0, a:0, s:0, d:0, q:0, e:0');
    });

    it('sends dx/dy mouse deltas in the camera_input message', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('dx: mouseDeltaX');
        expect(html).toContain('dy: mouseDeltaY');
    });

    it('sends shift state in the camera_input message', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('shift: shiftDown');
    });

    it('sends rmb state in the camera_input message', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('rmb: rightMouseDown');
    });

    it('resets mouseDeltaX and mouseDeltaY to 0 after each send', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('mouseDeltaX = 0');
        expect(html).toContain('mouseDeltaY = 0');
    });

    it('uses setInterval at 33ms for ~30fps input polling', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('}, 33)');
    });

    it('skips sending when no input is active (early return guard)', () => {
        const html = buildViewportStreamHtml(baseOpts);
        // The guard expression checks all movement keys before sending.
        expect(html).toContain('!keys.w && !keys.s && !keys.a && !keys.d && !keys.q && !keys.e');
    });

    it('tracks canvasFocused via focus/blur events on the canvas element', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('canvasFocused');
        expect(html).toContain("canvas.addEventListener('focus'");
        expect(html).toContain("canvas.addEventListener('blur'");
    });

    it('guards gizmo hotkeys behind canvasFocused check', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('else if (canvasFocused)');
    });

    it('contains editor.pickNode message for click-to-select', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('editor.pickNode');
        expect(html).toContain('screen_x');
        expect(html).toContain('screen_y');
    });

    it('auto-focuses canvas on left-click for subsequent hotkeys', () => {
        const html = buildViewportStreamHtml(baseOpts);
        // The click handler should call canvas.focus()
        expect(html).toContain('canvas.focus()');
    });
});

// ---------------------------------------------------------------------------
// buildViewportStreamHtml — ResizeObserver / viewport resize tests
// ---------------------------------------------------------------------------

describe('buildViewportStreamHtml — viewport resize', () => {
    const baseOpts = {
        nonce: 'testnonce123',
        title: 'Test Viewport',
        wsPort: 9002,
        viewportId: 1,
    };

    it('contains viewport_resize message type for WS send', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('viewport_resize');
    });

    it('contains ResizeObserver constructor call', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('ResizeObserver');
    });

    it('contains devicePixelRatio for HiDPI support', () => {
        const html = buildViewportStreamHtml(baseOpts);
        expect(html).toContain('devicePixelRatio');
    });
});
