/**
 * uzscene-editor-engine.test.ts — Integration tests for UzSceneEditorProvider
 * engine-launch behaviour.
 *
 * Tests cover:
 *  - resolveCustomTextEditor shows viewport canvas HTML immediately
 *  - EngineInstanceManager.createInstance() is called for the scene
 *  - buildLoadingHtml exports the file name in the HTML
 *  - Panel title is set from scene name
 *  - onDidDispose destroys the engine instance
 */

import { jest } from '@jest/globals';

// Mock EngineInstanceManager 
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

import {
    makeMockWebviewPanel,
    window,
    workspace,
} from '../__mocks__/vscode';

import { UzSceneEditorProvider, buildLoadingHtml } from '../uzscene-editor';
import { buildViewportStreamHtml } from '../viewport-panel';

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
// Helpers
// ---------------------------------------------------------------------------

const dummyContext = {
    extensionUri: { fsPath: '/ext', scheme: 'file', path: '/ext' },
    subscriptions: [],
} as unknown as import('vscode').ExtensionContext;

function makeDocument(text: string, uri = 'file:///test.uzscene'): import('vscode').TextDocument {
    return {
        getText: () => text,
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
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);

        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(buildViewportStreamHtml).toHaveBeenCalledTimes(1);
        expect(panel.webview.html).toContain('VIEWPORT');
    });

    it('calls EngineInstanceManager.createInstance() with the scene path', async () => {
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        const doc = makeDocument(SIMPLE_SCENE);
        await provider.resolveCustomTextEditor(doc, panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(mockCreateInstance).toHaveBeenCalledWith('file:///test.uzscene');
    });

    it('uses the instance wsPort for the viewport stream', async () => {
        mockCreateInstance.mockResolvedValue({
            id: 'test-id',
            wsPort: 9005,
            isDisposed: false,
        });
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ wsPort: 9005 })
        );
    });

    it('passes viewportId=1 for multiplexed mode', async () => {
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(buildViewportStreamHtml).toHaveBeenCalledWith(
            expect.objectContaining({ viewportId: 1 })
        );
    });
});

// ---------------------------------------------------------------------------
// Instance lifecycle
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — instance lifecycle', () => {
    it('registers onDidDispose to destroy the engine instance', async () => {
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);

        // Simulate tab close — the onDidDispose callback should destroy the instance.
        panel._simulateDispose();

        expect(mockDestroyInstance).toHaveBeenCalledWith('test-instance-id');
    });
});

// ---------------------------------------------------------------------------
// EngineInstanceManager not initialised
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — EngineInstanceManager not initialised', () => {
    it('shows error message when getInstance throws', async () => {
        // Override the mock to throw (not initialised).
        const { EngineInstanceManager } = await import('../engine-instance-manager');
        (EngineInstanceManager.getInstance as jest.Mock).mockImplementationOnce(() => {
            throw new Error('not initialised');
        });

        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);

        expect(window.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('EngineInstanceManager is not initialised')
        );
    });
});

// ---------------------------------------------------------------------------
// Panel title from scene name
// ---------------------------------------------------------------------------

describe('UzSceneEditorProvider — panel title (engine test)', () => {
    it('sets panel title from scene name', async () => {
        const provider = new UzSceneEditorProvider(dummyContext);
        const panel = makeMockWebviewPanel();
        await provider.resolveCustomTextEditor(makeDocument(SIMPLE_SCENE), panel as unknown as import('vscode').WebviewPanel, dummyToken);
        expect(panel.title).toBe('TestScene — UzScene');
    });
});
