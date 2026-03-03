/**
 * vscode.ts — Jest mock for the `vscode` module.
 *
 * This mock replaces the real VSCode extension API (which is only available
 * inside the VSCode Extension Host process) with Jest-compatible stubs that
 * allow unit tests to run in a plain Node.js environment.
 *
 * Stubs are intentionally minimal — only what the source files actually use.
 * Add new stubs as needed, keeping them as simple as possible.
 */

import { jest } from '@jest/globals';

// ---------------------------------------------------------------------------
// Types that appear in source but are hard to stub structurally
// ---------------------------------------------------------------------------

/** Minimal Disposable interface matching vscode.Disposable. */
export interface Disposable {
    dispose(): void;
}

// ---------------------------------------------------------------------------
// Uri
// ---------------------------------------------------------------------------

export const Uri = {
    file: (fsPath: string) => ({ fsPath, scheme: 'file', path: fsPath }),
    parse: (uriString: string) => ({ fsPath: uriString, scheme: 'file', path: uriString }),
};

// ---------------------------------------------------------------------------
// ViewColumn enum
// ---------------------------------------------------------------------------

export enum ViewColumn {
    Active = -1,
    Beside = -2,
    One = 1,
    Two = 2,
    Three = 3,
}

// ---------------------------------------------------------------------------
// WebviewPanel factory helpers
// ---------------------------------------------------------------------------

/** Creates a fresh mock WebviewPanel. */
export function makeMockWebviewPanel() {
    const listeners: {
        viewState: Array<(e: { webviewPanel: { visible: boolean; active: boolean } }) => void>;
        dispose: Array<() => void>;
    } = { viewState: [], dispose: [] };

    const webview = {
        html: '',
        postMessage: jest.fn().mockResolvedValue(true),
        onDidReceiveMessage: jest.fn(),
        options: {},
        cspSource: 'vscode-webview:',
    };

    const panel = {
        webview,
        visible: true,
        active: true,
        viewColumn: ViewColumn.One,
        title: '',
        dispose: jest.fn(() => {
            listeners.dispose.forEach(cb => cb());
        }),
        reveal: jest.fn(),
        onDidChangeViewState: jest.fn((cb: (e: { webviewPanel: { visible: boolean; active: boolean } }) => void) => {
            listeners.viewState.push(cb);
            return { dispose: jest.fn() };
        }),
        onDidDispose: jest.fn((cb: () => void) => {
            listeners.dispose.push(cb);
            return { dispose: jest.fn() };
        }),
        // Test helpers for simulating events
        _simulateVisibilityChange(visible: boolean) {
            panel.visible = visible;
            listeners.viewState.forEach(cb => cb({ webviewPanel: { visible, active: visible } }));
        },
        _simulateDispose() {
            listeners.dispose.forEach(cb => cb());
        },
    };

    return panel;
}

let _mockPanelFactory: (() => ReturnType<typeof makeMockWebviewPanel>) | null = null;

/** Install a custom panel factory for tests that need specific panel instances. */
export function __setMockPanelFactory(factory: () => ReturnType<typeof makeMockWebviewPanel>): void {
    _mockPanelFactory = factory;
}

/** Reset the panel factory to the default. */
export function __resetMockPanelFactory(): void {
    _mockPanelFactory = null;
}

// ---------------------------------------------------------------------------
// window namespace
// ---------------------------------------------------------------------------

const _windowStateListeners: Array<(state: { focused: boolean }) => void> = [];

export const window = {
    createWebviewPanel: jest.fn((_viewType: string, title: string) => {
        const panel = _mockPanelFactory ? _mockPanelFactory() : makeMockWebviewPanel();
        panel.title = title;
        return panel;
    }),
    createOutputChannel: jest.fn((_name: string) => ({
        appendLine: jest.fn(),
        append: jest.fn(),
        clear: jest.fn(),
        show: jest.fn(),
        dispose: jest.fn(),
    })),
    showErrorMessage: jest.fn().mockResolvedValue(undefined),
    showWarningMessage: jest.fn().mockResolvedValue(undefined),
    showInformationMessage: jest.fn().mockResolvedValue(undefined),
    createStatusBarItem: jest.fn(() => ({
        text: '',
        tooltip: '',
        command: '',
        backgroundColor: undefined,
        show: jest.fn(),
        hide: jest.fn(),
        dispose: jest.fn(),
    })),
    onDidChangeWindowState: jest.fn((cb: (state: { focused: boolean }) => void) => {
        _windowStateListeners.push(cb);
        return {
            dispose: jest.fn(() => {
                const idx = _windowStateListeners.indexOf(cb);
                if (idx !== -1) { _windowStateListeners.splice(idx, 1); }
            }),
        };
    }),
    // Test helper
    _simulateWindowFocus(focused: boolean): void {
        _windowStateListeners.forEach(cb => cb({ focused }));
    },
    activeTextEditor: undefined as undefined | { viewColumn: ViewColumn },
};

// ---------------------------------------------------------------------------
// workspace namespace
// ---------------------------------------------------------------------------

const _configValues: Map<string, unknown> = new Map();

export const workspace = {
    workspaceFolders: [{ uri: { fsPath: '/mock/workspace' }, name: 'mock', index: 0 }] as
        Array<{ uri: { fsPath: string }; name: string; index: number }>,

    getConfiguration: jest.fn((_section?: string) => ({
        get: jest.fn(<T>(key: string, defaultValue?: T): T => {
            const fullKey = `${_section ?? ''}.${key}`;
            return (_configValues.has(fullKey) ? _configValues.get(fullKey) : defaultValue) as T;
        }),
        update: jest.fn(),
        has: jest.fn(() => false),
        inspect: jest.fn(() => undefined),
    })),

    onDidChangeConfiguration: jest.fn(() => ({ dispose: jest.fn() })),

    // Test helper: set a configuration value for use in tests.
    __setConfig(section: string, key: string, value: unknown): void {
        _configValues.set(`${section}.${key}`, value);
    },

    __clearConfig(): void {
        _configValues.clear();
    },
};

// ---------------------------------------------------------------------------
// commands namespace
// ---------------------------------------------------------------------------

const _commandHandlers: Map<string, (...args: unknown[]) => unknown> = new Map();

export const commands = {
    registerCommand: jest.fn((id: string, handler: (...args: unknown[]) => unknown) => {
        _commandHandlers.set(id, handler);
        return { dispose: jest.fn() };
    }),
    executeCommand: jest.fn(async (id: string, ...args: unknown[]) => {
        const handler = _commandHandlers.get(id);
        if (handler) { return handler(...args); }
    }),
    // Test helper
    __executeRegisteredCommand(id: string, ...args: unknown[]): unknown {
        const handler = _commandHandlers.get(id);
        if (!handler) { throw new Error(`Command not registered: ${id}`); }
        return handler(...args);
    },
    __clearHandlers(): void {
        _commandHandlers.clear();
    },
};

// ---------------------------------------------------------------------------
// StatusBarAlignment enum
// ---------------------------------------------------------------------------

export enum StatusBarAlignment {
    Left = 1,
    Right = 2,
}

// ---------------------------------------------------------------------------
// ThemeColor
// ---------------------------------------------------------------------------

export class ThemeColor {
    constructor(public readonly id: string) {}
}

// ---------------------------------------------------------------------------
// Miscellaneous stubs
// ---------------------------------------------------------------------------

export const extensions = {
    getExtension: jest.fn(() => undefined),
};

export const env = {
    openExternal: jest.fn(),
    clipboard: {
        writeText: jest.fn(),
        readText: jest.fn().mockResolvedValue(''),
    },
};

// ---------------------------------------------------------------------------
// ExtensionContext mock factory
// ---------------------------------------------------------------------------

/** Creates a mock vscode.ExtensionContext for use in tests. */
export function makeMockContext(): {
    subscriptions: Disposable[];
    extensionUri: { fsPath: string; scheme: string; path: string };
    workspaceState: { get: ReturnType<typeof jest.fn>; update: ReturnType<typeof jest.fn> };
    globalState: { get: ReturnType<typeof jest.fn>; update: ReturnType<typeof jest.fn> };
    extensionPath: string;
} {
    return {
        subscriptions: [],
        extensionUri: Uri.file('/mock/extension'),
        extensionPath: '/mock/extension',
        workspaceState: {
            get: jest.fn(),
            update: jest.fn().mockResolvedValue(undefined),
        },
        globalState: {
            get: jest.fn(),
            update: jest.fn().mockResolvedValue(undefined),
        },
    };
}
