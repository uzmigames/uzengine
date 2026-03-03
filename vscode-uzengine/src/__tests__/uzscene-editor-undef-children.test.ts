/**
 * uzscene-editor-undef-children.test.ts
 *
 * Tests for _updateTitle when parseUzScene returns edge-case data.
 * parseUzScene is mocked at module level to return various shapes.
 */

import { jest } from '@jest/globals';

jest.mock('../uzscene-parser', () => ({
    parseUzScene: jest.fn(),
}));

jest.mock('../server-manager', () => ({ ServerManager: jest.fn() }));

jest.mock('../viewport-panel', () => ({
    buildViewportStreamHtml: jest.fn(() => '<html>VIEWPORT</html>'),
}));

import { parseUzScene } from '../uzscene-parser';
import { makeMockWebviewPanel, workspace } from '../__mocks__/vscode';
import { UzSceneEditorProvider } from '../uzscene-editor';

const mockParseUzScene = parseUzScene as jest.MockedFunction<typeof parseUzScene>;

function makeMockServerManager(): import('../server-manager').ServerManager {
    return {
        start: jest.fn().mockImplementation(() => Promise.resolve(true)),
        isRunning: false,
        dispose: jest.fn(),
    } as unknown as import('../server-manager').ServerManager;
}

const dummyContext = {
    extensionUri: { fsPath: '/ext', scheme: 'file', path: '/ext' },
    subscriptions: [],
} as unknown as import('vscode').ExtensionContext;

const dummyToken: import('vscode').CancellationToken = {
    isCancellationRequested: false,
    onCancellationRequested: jest.fn(() => ({ dispose: jest.fn() })),
};

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

beforeEach(() => {
    jest.clearAllMocks();
    workspace.__clearConfig();
});

describe('UzSceneEditorProvider — _updateTitle edge cases', () => {

    it('sets title when sceneName is present', async () => {
        mockParseUzScene.mockReturnValueOnce({
            sceneName: 'MyScene',
            roots: [],
        });

        const provider = new UzSceneEditorProvider(makeMockServerManager(), dummyContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument('dummy');

        await provider.resolveCustomTextEditor(
            doc,
            panel as unknown as import('vscode').WebviewPanel,
            dummyToken,
        );

        expect(panel.title).toBe('MyScene — UzScene');
    });

    it('sets title to "UzScene Editor" when sceneName is empty string', async () => {
        mockParseUzScene.mockReturnValueOnce({
            sceneName: '',
            roots: [],
        });

        const provider = new UzSceneEditorProvider(makeMockServerManager(), dummyContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument('dummy');

        await provider.resolveCustomTextEditor(
            doc,
            panel as unknown as import('vscode').WebviewPanel,
            dummyToken,
        );

        expect(panel.title).toBe('UzScene Editor');
    });

    it('does not crash when parseUzScene returns undefined sceneName', async () => {
        mockParseUzScene.mockReturnValueOnce({
            sceneName: undefined as unknown as string,
            roots: [],
        });

        const provider = new UzSceneEditorProvider(makeMockServerManager(), dummyContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument('dummy');

        await expect(
            provider.resolveCustomTextEditor(
                doc,
                panel as unknown as import('vscode').WebviewPanel,
                dummyToken,
            )
        ).resolves.not.toThrow();

        expect(panel.title).toBe('UzScene Editor');
    });

    it('does not crash when parseUzScene throws', async () => {
        mockParseUzScene.mockImplementationOnce(() => {
            throw new Error('parse error');
        });

        const provider = new UzSceneEditorProvider(makeMockServerManager(), dummyContext);
        const panel    = makeMockWebviewPanel();
        const doc      = makeDocument('dummy');

        await expect(
            provider.resolveCustomTextEditor(
                doc,
                panel as unknown as import('vscode').WebviewPanel,
                dummyToken,
            )
        ).resolves.not.toThrow();
    });
});
