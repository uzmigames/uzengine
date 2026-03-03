/**
 * uzscene-editor-engine.test.ts — Integration tests for UzSceneEditorProvider
 * engine-launch behaviour.
 *
 * Tests cover:
 *  - resolveCustomTextEditor shows viewport canvas HTML immediately
 *  - serverManager.start() is called (fire-and-forget)
 *  - start() rejects: no error message shown, canvas still displayed
 *  - buildLoadingHtml exports the file name in the HTML
 */

import { jest } from '@jest/globals';

const mockStart = jest.fn<() => Promise<boolean>>();

jest.mock('../server-manager', () => ({
    ServerManager: jest.fn(),
}));

jest.mock('../viewport-panel', () => ({
    buildViewportStreamHtml: jest.fn(
        (opts: { nonce: string; title: string; wsPort: number; viewportId: number }) =>
            `<html><body>VIEWPORT wsPort=${opts.wsPort} viewportId=${opts.viewportId} title=${opts.title}</body></html>`
    ),
}));

import {
    window,
    workspace,
    makeMockWebviewPanel,
} from '../__mocks__/vscode';

import { UzSceneEditorProvider, buildLoadingHtml } from '../uzscene-editor';
import { buildViewportStreamHtml } from '../viewport-panel';

beforeEach(() => {
    jest.clearAllMocks();
    workspace.__clearConfig();
});

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeServerManager(startResult: boolean | Error = true): import('../server-manager').ServerManager {
    if (startResult instanceof Error) {
        mockStart.mockRejectedValue(startResult);
    } else {
        mockStart.mockResolvedValue(startResult);
    }
    return { start: mockStart, isRunning: false, dispose: jest.fn() } as unknown as import('../server-manager').ServerManager;
}

const dummyContext = {
    extensionUri: { fsPath: '/ext', scheme: 'file', path: '/ext' },
    subscriptions: [],
} as unknown as import('vscode').ExtensionContext;

function makeDocument(text: string, uri = 'file:///test.uzscene'): import('vscode').TextDocument {
    return {
        getText:  () => text,
        fileName: uri,
        uri: { toString: () => uri, fsPath: uri, scheme: 'file', path: uri },
    } as unknown as import('vscode').TextDocument;
}

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
// buildLoadingHtml helper
// ---------------------------------------------------------------------------

describe('buildLoadingHtml()', () => {
    it('includes the file name in the output', () => {
        const html = buildLoadingHtml('/path/to/level.uzscene');
        expect(html).toContain('level.uzscene');
    });

    it('includes "Starting UzEngine" text', () => {
        const html = buildLoadingHtml('scene.uzscene');
        expect(html).toContain('Starting UzEngine');
    });

    it('escapes HTML in the file name', () => {
        const html = buildLoadingHtml('<evil>.uzscene');
        expect(html).not.toContain('<evil>');
        expect(html).toContain('&lt;evil&gt;');
    });

    it('returns a complete HTML document', () => {
        const html = buildLoadingHtml('x.uzscene');
        expect(html).toContain('<!DOCTYPE html>');
        expect(html).toContain('</html>');
    });
});

// ---------------------------------------------------------------------------
// Engine-launch flow
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — engine launch', () => {
    it('shows viewport canvas HTML immediately', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(true), dummyContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
        expect(panel.webview.html).toContain('VIEWPORT');
    });

    it('calls serverManager.start()', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(true), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(mockStart).toHaveBeenCalledTimes(1);
    });

    it('shows canvas even when start() returns false', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(false), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
        expect(panel.webview.html).toContain('VIEWPORT');
    });

    it('passes viewportId=1 for multiplexed mode', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(true), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ viewportId: 1 })
        );
    });
});

// ---------------------------------------------------------------------------
// Engine start failure (fire-and-forget)
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — engine start failure', () => {
    it('does not show error message when start() throws (fire-and-forget)', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(new Error('exe not found')), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(window.showErrorMessage).not.toHaveBeenCalled();
    });

    it('still shows viewport canvas when start() throws', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(new Error('fail')), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
    });

    it('handles non-Error thrown value from start()', async () => {
        mockStart.mockRejectedValue('string error');
        const sm = { start: mockStart, isRunning: false, dispose: jest.fn() } as unknown as import('../server-manager').ServerManager;
        const provider = new UzSceneEditorProvider(sm, dummyContext);
        const panel    = makeMockWebviewPanel();
        await expect(
            provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken)
        ).resolves.not.toThrow();
    });
});

// ---------------------------------------------------------------------------
// Panel title from scene name
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — panel title (engine test)', () => {
    it('sets panel title from scene name', async () => {
        const provider = new UzSceneEditorProvider(makeServerManager(true), dummyContext);
        const panel    = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(panel.title).toBe('TestScene — UzScene');
    });
});
