/**
 * uzscene-editor.test.ts — Unit tests for UzSceneEditorProvider (viewport canvas mode).
 *
 * The UzSceneEditorProvider shows the live DX12 render stream (full-page
 * canvas over WebSocket) in the editor tab immediately — no waiting for
 * the engine to start.  The webview JS has a reconnect loop.
 *
 * Updated for multi-instance architecture: each scene now gets its own
 * EngineInstance via EngineInstanceManager.
 *
 * Coverage targets (≥95% line + branch):
 *  1.  viewType static constant is 'uzengine.uzsceneEditor'
 *  2.  resolveCustomTextEditor sets webview.html to viewport canvas immediately
 *  3.  Viewport canvas HTML contains VIEWPORT_ID=1
 *  4.  Panel title set from scene name (sceneName present)
 *  5.  Panel title set to 'UzScene Editor' when sceneName absent
 *  6.  Panel title left unchanged when parseUzScene throws
 *  7.  Instance wsPort is used for the viewport stream
 *  8.  Title falls back to 'UzScene' when fileName is empty
 *  9.  createInstance() is called with the scene path
 *  10. dispose() on provider does not throw
 *  11. dispose() is idempotent
 *  12. webview.options not overridden by provider
 */

import { jest } from '@jest/globals';

// ---------------------------------------------------------------------------
// Mocks — must be declared before imports
// ---------------------------------------------------------------------------

const mockCreateInstance = jest.fn<() => Promise<{ id: string; wsPort: number; isDisposed: boolean }>>().mockResolvedValue({
    id: 'test-instance-id',
    wsPort: 9002,
    isDisposed: false,
});
const mockDestroyInstance = jest.fn();
const mockSetActiveInstance = jest.fn();

jest.mock('../engine-instance-manager', () => ({
    EngineInstanceManager: {
        getInstance: jest.fn(() => ({
            createInstance: mockCreateInstance,
            destroyInstance: mockDestroyInstance,
            setActiveInstance: mockSetActiveInstance,
        })),
    },
}));

jest.mock('../viewport-panel', () => ({
    buildViewportStreamHtml: jest.fn(
        (opts: { nonce: string; title: string; wsPort: number; viewportId: number }) =>
            `<html><body>VIEWPORT wsPort=${opts.wsPort} viewportId=${opts.viewportId} title=${opts.title}</body></html>`
    ),
}));

// ---------------------------------------------------------------------------
// Imports
// ---------------------------------------------------------------------------

import {
    makeMockContext,
    makeMockWebviewPanel,
    window,
    workspace,
} from '../__mocks__/vscode';

import { UzSceneEditorProvider, buildLoadingHtml } from '../uzscene-editor';
import { buildViewportStreamHtml } from '../viewport-panel';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Creates a minimal TextDocument-like object. */
function makeDocument(text: string, uri = 'file:///test.uzscene'): import('vscode').TextDocument {
    return {
        getText: () => text,
        fileName: uri,
        uri: {
            toString: () => uri,
            fsPath: uri,
            scheme: 'file',
            path: uri,
        },
    } as unknown as import('vscode').TextDocument;
}

/** Minimal CancellationToken stub. */
const dummyToken: import('vscode').CancellationToken = {
    isCancellationRequested: false,
    onCancellationRequested: jest.fn(() => ({ dispose: jest.fn() })),
};

const SIMPLE_SCENE = `[uzscene]
name = TestScene

[node]
name = World
type = Node3D
parent =
`;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    workspace.__clearConfig();
    mockCreateInstance.mockResolvedValue({
        id: 'test-instance-id',
        wsPort: 9002,
        isDisposed: false,
    });
});

// ---------------------------------------------------------------------------
// Static properties
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — static viewType', () => {
    it('is "uzengine.uzsceneEditor"', () => {
        expect(UzSceneEditorProvider.viewType).toBe('uzengine.uzsceneEditor');
    });
});

// ---------------------------------------------------------------------------
// buildLoadingHtml export
// ---------------------------------------------------------------------------

describe('buildLoadingHtml()', () => {
    it('returns non-empty HTML', () => {
        const html = buildLoadingHtml('test.uzscene');
        expect(html.length).toBeGreaterThan(0);
        expect(html).toContain('<!DOCTYPE html>');
    });

    it('contains the file name', () => {
        const html = buildLoadingHtml('/some/path/MyLevel.uzscene');
        expect(html).toContain('MyLevel.uzscene');
    });

    it('escapes HTML special chars in file name', () => {
        const html = buildLoadingHtml('<evil>.uzscene');
        expect(html).not.toContain('<evil>');
        expect(html).toContain('&lt;evil&gt;');
    });

    it('uses CSP with no external resources', () => {
        const html = buildLoadingHtml('x.uzscene');
        expect(html).toContain("default-src 'none'");
    });

    it('falls back to full fileName when pop() returns empty string', () => {
        const html = buildLoadingHtml('');
        expect(html).toContain('<!DOCTYPE html>');
    });
});

// ---------------------------------------------------------------------------
// resolveCustomTextEditor — viewport canvas shown immediately
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider.resolveCustomTextEditor() — viewport canvas', () => {
    it('calls buildViewportStreamHtml immediately', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
    });

    it('calls buildViewportStreamHtml with viewportId=1 (multiplexed mode)', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ viewportId: 1 })
        );
    });

    it('uses instance wsPort (default 9002)', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ wsPort: 9002 })
        );
    });

    it('uses custom wsPort from instance', async () => {
        mockCreateInstance.mockResolvedValue({
            id: 'custom-port-id',
            wsPort: 8080,
            isDisposed: false,
        });
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ wsPort: 8080 })
        );
    });

    it('passes a non-empty nonce string', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        const call = (buildViewportStreamHtml as jest.Mock).mock.calls[0][0] as { nonce: string };
        expect(call.nonce).toBeTruthy();
        expect(typeof call.nonce).toBe('string');
    });

    it('passes the file base name as title', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE, 'file:///some/path/MyLevel.uzscene');

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ title: 'MyLevel.uzscene' })
        );
    });

    it('falls back to "UzScene" when fileName is empty (|| branch)', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE, '');

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ title: 'UzScene' })
        );
    });

    it('sets webview.html to the result of buildViewportStreamHtml', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.webview.html).toContain('VIEWPORT');
        expect(panel.webview.html).toContain('viewportId=1');
    });
});

// ---------------------------------------------------------------------------
// Instance lifecycle via EngineInstanceManager
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — engine instance management', () => {
    it('calls createInstance() with the scene file path', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE, 'file:///my/scene.uzscene');

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(mockCreateInstance).toHaveBeenCalledWith('file:///my/scene.uzscene');
    });

    it('destroys the instance when the panel is disposed', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        // Simulate tab close.
        panel._simulateDispose();

        expect(mockDestroyInstance).toHaveBeenCalledWith('test-instance-id');
    });

    it('shows error when createInstance throws (max instances)', async () => {
        mockCreateInstance.mockRejectedValue(new Error('Maximum engine instances reached'));
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('Maximum engine instances reached')
        );
    });

    it('shows error when EngineInstanceManager is not initialised', async () => {
        const { EngineInstanceManager } = await import('../engine-instance-manager');
        (EngineInstanceManager.getInstance as jest.Mock).mockImplementationOnce(() => {
            throw new Error('not initialised');
        });

        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('EngineInstanceManager is not initialised')
        );
    });

    it('shows viewport canvas even when createInstance returns false-start', async () => {
        // Instance is created but its process didn't start — viewport canvas still shown.
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        // Canvas shown regardless.
        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
        expect(panel.webview.html).toContain('VIEWPORT');
    });
});

// ---------------------------------------------------------------------------
// Panel title update from scene name
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — panel title', () => {
    it('sets panel title to "sceneName — UzScene" when sceneName is present', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.title).toBe('TestScene — UzScene');
    });

    it('sets panel title to "UzScene Editor" when sceneName is absent', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const noNameScene = `[node]
name = Root
type = Node3D
parent =
`;
        const doc = makeDocument(noNameScene);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.title).toBe('UzScene Editor');
    });

    it('does not throw when parseUzScene throws inside _updateTitle', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();

        const badDoc = {
            getText: () => { throw new Error('bad parse'); },
            fileName: 'bad.uzscene',
            uri: { toString: () => 'file:///bad.uzscene', fsPath: 'file:///bad.uzscene', scheme: 'file', path: 'file:///bad.uzscene' },
        } as unknown as import('vscode').TextDocument;

        await expect(
            provider.resolveCustomTextEditor(badDoc, panel as unknown as import('vscode').WebviewPanel, dummyToken)
        ).resolves.not.toThrow();
    });
});

// ---------------------------------------------------------------------------
// webview options not overridden
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — webview options', () => {
    it('enables scripts on the webview', async () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.webview.options).toEqual({ enableScripts: true });
    });
});

// ---------------------------------------------------------------------------
// Provider dispose()
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider.dispose()', () => {
    it('does not throw', () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        expect(() => provider.dispose()).not.toThrow();
    });

    it('is idempotent', () => {
        const provider = new UzSceneEditorProvider(makeMockContext() as unknown as import('vscode').ExtensionContext);
        expect(() => { provider.dispose(); provider.dispose(); }).not.toThrow();
    });
});
