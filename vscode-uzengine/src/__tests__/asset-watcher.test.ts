/**
 * asset-watcher.test.ts — Tests for AssetWatcher (auto-import on file creation).
 */

import { AssetWatcher } from '../asset-watcher';
import * as vscode from 'vscode';

// ---------------------------------------------------------------------------
// Mocks
// ---------------------------------------------------------------------------

const mockIpc = {
    request: jest.fn().mockResolvedValue({ ok: true }),
    dispose: jest.fn(),
} as any;

// Track watcher callbacks registered by the AssetWatcher constructor.
let onDidCreateCallback: ((uri: vscode.Uri) => void) | null = null;

const mockWatcherDispose = jest.fn();
jest.mock('vscode', () => {
    const original = jest.requireActual('../__mocks__/vscode');
    return {
        ...original,
        workspace: {
            ...original.workspace,
            createFileSystemWatcher: jest.fn(() => ({
                onDidCreate: jest.fn((cb: (uri: vscode.Uri) => void) => {
                    onDidCreateCallback = cb;
                    return { dispose: jest.fn() };
                }),
                onDidChange: jest.fn(() => ({ dispose: jest.fn() })),
                onDidDelete: jest.fn(() => ({ dispose: jest.fn() })),
                dispose: mockWatcherDispose,
            })),
        },
    };
});

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeUri(path: string): vscode.Uri {
    return { fsPath: path, scheme: 'file', path } as any;
}

function advanceTimers(ms: number): void {
    jest.advanceTimersByTime(ms);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('AssetWatcher', () => {
    let watcher: AssetWatcher;
    let refreshCb: jest.Mock;

    beforeEach(() => {
        jest.useFakeTimers();
        jest.clearAllMocks();
        onDidCreateCallback = null;
        refreshCb = jest.fn();
        watcher = new AssetWatcher(mockIpc, refreshCb);
    });

    afterEach(() => {
        watcher.dispose();
        jest.useRealTimers();
    });

    it('should create a file system watcher on assets/**/*', () => {
        expect(vscode.workspace.createFileSystemWatcher).toHaveBeenCalledWith('**/assets/**/*');
    });

    it('should register onDidCreate callback', () => {
        expect(onDidCreateCallback).not.toBeNull();
    });

    it('should import .glb files after 200ms debounce', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));

        // Before debounce — not called yet.
        expect(mockIpc.request).not.toHaveBeenCalled();

        // Advance past debounce.
        advanceTimers(200);
        await Promise.resolve(); // flush microtasks

        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/meshes/cube.glb',
        });
    });

    it('should import .png texture files', async () => {
        onDidCreateCallback!(makeUri('/project/assets/textures/albedo.png'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/textures/albedo.png',
        });
    });

    it('should import .wav audio files', async () => {
        onDidCreateCallback!(makeUri('/project/assets/audio/hit.wav'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/audio/hit.wav',
        });
    });

    it('should import .hlsl shader files', async () => {
        onDidCreateCallback!(makeUri('/project/assets/shaders/my.hlsl'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/shaders/my.hlsl',
        });
    });

    it('should ignore non-importable extensions like .txt', async () => {
        onDidCreateCallback!(makeUri('/project/assets/notes.txt'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).not.toHaveBeenCalled();
    });

    it('should ignore files with no extension', async () => {
        onDidCreateCallback!(makeUri('/project/assets/Makefile'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).not.toHaveBeenCalled();
    });

    it('should debounce multiple rapid file creations to one batch', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/a.glb'));
        advanceTimers(100); // 100ms — debounce resets
        onDidCreateCallback!(makeUri('/project/assets/meshes/b.glb'));
        advanceTimers(200);
        await Promise.resolve();

        // Both imported in one batch.
        expect(mockIpc.request).toHaveBeenCalledTimes(2);
        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/meshes/a.glb',
        });
        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/meshes/b.glb',
        });
    });

    it('should call refresh callback after importing', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        expect(refreshCb).toHaveBeenCalledTimes(1);
    });

    it('should not call refresh if no importable files detected', async () => {
        onDidCreateCallback!(makeUri('/project/assets/readme.md'));
        advanceTimers(200);
        await Promise.resolve();

        expect(refreshCb).not.toHaveBeenCalled();
    });

    it('should show information message on successful import', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        expect(vscode.window.showInformationMessage).toHaveBeenCalledWith(
            'Imported asset: cube.glb'
        );
    });

    it('should fire onDidImport event', async () => {
        const listener = jest.fn();
        watcher.onDidImport(listener);

        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        expect(listener).toHaveBeenCalledWith('/project/assets/meshes/cube.glb');
    });

    it('should not crash on import IPC failure', async () => {
        mockIpc.request.mockRejectedValueOnce(new Error('IPC error'));

        onDidCreateCallback!(makeUri('/project/assets/meshes/broken.glb'));
        advanceTimers(200);
        await Promise.resolve();

        // Should not throw, refresh still called.
        expect(refreshCb).toHaveBeenCalledTimes(1);
    });

    it('should deduplicate same file path', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        // Only one import call for the same path.
        expect(mockIpc.request).toHaveBeenCalledTimes(1);
    });

    it('should handle case-insensitive extensions (.PNG)', async () => {
        onDidCreateCallback!(makeUri('/project/assets/textures/photo.PNG'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/textures/photo.PNG',
        });
    });

    it('should not import after dispose', async () => {
        watcher.dispose();

        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).not.toHaveBeenCalled();
    });

    it('should work without refresh callback', async () => {
        const w2 = new AssetWatcher(mockIpc);
        // Get the callback registered by the second watcher.
        const cb = onDidCreateCallback!;
        cb(makeUri('/project/assets/meshes/cube.glb'));
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).toHaveBeenCalled();
        w2.dispose();
    });

    it('should handle all supported mesh extensions', async () => {
        for (const ext of ['.glb', '.gltf', '.fbx', '.obj']) {
            onDidCreateCallback!(makeUri(`/project/assets/mesh${ext}`));
        }
        advanceTimers(200);
        // Flush all microtasks (one per await inside _importPending loop).
        for (let i = 0; i < 10; i++) { await Promise.resolve(); }
        expect(mockIpc.request).toHaveBeenCalledTimes(4);
    });

    it('should handle all supported texture extensions', async () => {
        for (const ext of ['.png', '.jpg', '.jpeg', '.tga', '.bmp', '.hdr']) {
            onDidCreateCallback!(makeUri(`/project/assets/tex${ext}`));
        }
        advanceTimers(200);
        for (let i = 0; i < 15; i++) { await Promise.resolve(); }
        expect(mockIpc.request).toHaveBeenCalledTimes(6);
    });

    it('should handle all supported audio extensions', async () => {
        for (const ext of ['.wav', '.ogg', '.mp3']) {
            onDidCreateCallback!(makeUri(`/project/assets/sfx${ext}`));
        }
        advanceTimers(200);
        for (let i = 0; i < 10; i++) { await Promise.resolve(); }
        expect(mockIpc.request).toHaveBeenCalledTimes(3);
    });

    it('should cancel pending debounce on dispose', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        // Dispose before the 200ms debounce fires.
        watcher.dispose();
        advanceTimers(200);
        await Promise.resolve();

        expect(mockIpc.request).not.toHaveBeenCalled();
    });

    it('should handle double dispose gracefully', () => {
        watcher.dispose();
        // Second dispose should not throw.
        expect(() => watcher.dispose()).not.toThrow();
    });

    it('should use full path as display name when path has no filename segment', async () => {
        // Directly inject a path that ends with a separator into _pendingPaths,
        // which would make .split(/[\\/]/).pop() return '' (falsy).
        // This exercises the || fallback in the info message.
        const w = watcher as any;
        w._pendingPaths.add('/');
        await w._importPending();

        expect(vscode.window.showInformationMessage).toHaveBeenCalledWith(
            'Imported asset: /'
        );
    });

    it('should skip import if _importPending is called after dispose', async () => {
        // Add a pending path manually, then dispose, then call _importPending directly.
        onDidCreateCallback!(makeUri('/project/assets/meshes/cube.glb'));
        watcher.dispose();

        // Call _importPending directly on the disposed watcher to exercise the guard.
        await (watcher as any)._importPending();

        expect(mockIpc.request).not.toHaveBeenCalled();
    });

    // 5.6 Integration test: file create → asset.import IPC fired within 300ms.
    it('should fire asset.import within 300ms of file creation', async () => {
        onDidCreateCallback!(makeUri('/project/assets/meshes/house.glb'));

        // At 199ms — debounce hasn't fired yet.
        advanceTimers(199);
        await Promise.resolve();
        expect(mockIpc.request).not.toHaveBeenCalled();

        // At 201ms — debounce has fired, import should be called.
        advanceTimers(2);
        await Promise.resolve();
        expect(mockIpc.request).toHaveBeenCalledWith('asset.import', {
            path: '/project/assets/meshes/house.glb',
        });
        expect(refreshCb).toHaveBeenCalled();
    });
});
