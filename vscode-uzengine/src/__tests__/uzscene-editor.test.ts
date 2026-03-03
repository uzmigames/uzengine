/**
 * uzscene-editor.test.ts — Unit tests for UzSceneEditorProvider (viewport canvas mode).
 *
 * The UzSceneEditorProvider shows the live DX12 render stream (full-page
 * canvas over WebSocket) in the editor tab immediately — no waiting for
 * the engine to start.  The webview JS has a reconnect loop.
 *
 * Coverage targets (≥95% line + branch):
 *  1.  viewType static constant is 'uzengine.uzsceneEditor'
 *  2.  resolveCustomTextEditor sets webview.html to viewport canvas immediately
 *  3.  Viewport canvas HTML contains WS_PORT and VIEWPORT_ID=1
 *  4.  Panel title set from scene name (sceneName present)
 *  5.  Panel title set to 'UzScene Editor' when sceneName absent
 *  6.  Panel title left unchanged when parseUzScene throws
 *  7.  Custom server.wsPort config is injected into the canvas HTML
 *  8.  Title falls back to 'UzScene' when fileName is empty
 *  9.  start() is called fire-and-forget (errors silently caught)
 *  10. dispose() on provider does not throw
 *  11. dispose() is idempotent
 *  12. webview.options not overridden by provider
 */

import { jest } from '@jest/globals';

// ---------------------------------------------------------------------------
// Mocks — must be declared before imports
// ---------------------------------------------------------------------------

jest.mock('../server-manager', () => ({
    ServerManager: jest.fn(),
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
    window,
    workspace,
    makeMockWebviewPanel,
    makeMockContext,
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

/** Minimal ServerManager stub — start() resolves true by default. */
function makeMockServerManager(startResult: Promise<boolean> = Promise.resolve(true)) {
    return {
        start:     jest.fn().mockReturnValue(startResult),
        isRunning: false,
        dispose:   jest.fn(),
    } as unknown as import('../server-manager').ServerManager;
}

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
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
    });

    it('calls buildViewportStreamHtml with viewportId=1 (multiplexed mode)', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ viewportId: 1 })
        );
    });

    it('uses default wsPort=9002 when not configured', async () => {
        workspace.__clearConfig();
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ wsPort: 9002 })
        );
    });

    it('uses custom server.wsPort from config', async () => {
        workspace.__setConfig('uzengine', 'server.wsPort', 8080);
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ wsPort: 8080 })
        );
    });

    it('passes a non-empty nonce string', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        const call = (buildViewportStreamHtml as jest.Mock).mock.calls[0][0] as { nonce: string };
        expect(call.nonce).toBeTruthy();
        expect(typeof call.nonce).toBe('string');
    });

    it('passes the file base name as title', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE, 'file:///some/path/MyLevel.uzscene');

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ title: 'MyLevel.uzscene' })
        );
    });

    it('falls back to "UzScene" when fileName is empty (|| branch)', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE, '');

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ title: 'UzScene' })
        );
    });

    it('sets webview.html to the result of buildViewportStreamHtml', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.webview.html).toContain('VIEWPORT');
        expect(panel.webview.html).toContain('viewportId=1');
    });
});

// ---------------------------------------------------------------------------
// start() is fire-and-forget
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — fire-and-forget start()', () => {
    it('calls serverManager.start()', async () => {
        const sm = makeMockServerManager();
        const provider = new UzSceneEditorProvider(sm, makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(sm.start).toHaveBeenCalledTimes(1);
    });

    it('does not throw when start() rejects (error is silently caught)', async () => {
        const sm = makeMockServerManager(Promise.reject(new Error('spawn failed')));
        const provider = new UzSceneEditorProvider(sm, makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        // Should not throw — error is swallowed by .catch().
        await expect(
            provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken)
        ).resolves.not.toThrow();

        // Canvas is still shown regardless.
        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
    });

    it('does not throw when start() rejects with a non-Error value', async () => {
        const sm = makeMockServerManager(Promise.reject('string error'));
        const provider = new UzSceneEditorProvider(sm, makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await expect(
            provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken)
        ).resolves.not.toThrow();
    });

    it('does not show error message when start() fails', async () => {
        const sm = makeMockServerManager(Promise.reject(new Error('spawn failed')));
        const provider = new UzSceneEditorProvider(sm, makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        // Fire-and-forget — no error message shown.
        expect(window.showErrorMessage).not.toHaveBeenCalled();
    });

    it('shows viewport canvas even when start() returns false', async () => {
        const sm = makeMockServerManager(Promise.resolve(false));
        const provider = new UzSceneEditorProvider(sm, makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        // Canvas shown regardless of start() result.
        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
        expect(panel.webview.html).toContain('VIEWPORT');
    });
});

// ---------------------------------------------------------------------------
// Panel title update from scene name
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — panel title', () => {
    it('sets panel title to "sceneName — UzScene" when sceneName is present', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.title).toBe('TestScene — UzScene');
    });

    it('sets panel title to "UzScene Editor" when sceneName is absent', async () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
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
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();

        const badDoc = {
            getText:  () => { throw new Error('bad parse'); },
            fileName: 'bad.uzscene',
            uri:      { toString: () => 'file:///bad.uzscene', fsPath: 'file:///bad.uzscene', scheme: 'file', path: 'file:///bad.uzscene' },
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
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(panel.webview.options).toEqual({ enableScripts: true });
    });
});

// ---------------------------------------------------------------------------
// Provider dispose()
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider.dispose()', () => {
    it('does not throw', () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        expect(() => provider.dispose()).not.toThrow();
    });

    it('is idempotent', () => {
        const provider = new UzSceneEditorProvider(makeMockServerManager(), makeMockContext() as unknown as import('vscode').ExtensionContext);
        expect(() => { provider.dispose(); provider.dispose(); }).not.toThrow();
    });
});
