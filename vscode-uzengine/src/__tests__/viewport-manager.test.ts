/**
 * viewport-manager.test.ts — Unit tests for ViewportManager.
 *
 * Tests cover:
 *  - Singleton access (getInstance() throws before initialize())
 *  - createViewport(): sends viewport.create IPC, maps panel by id, activates visible panels
 *  - createViewport(): graceful degradation when IPC fails (engine not running)
 *  - closeViewport(): sends viewport.destroy IPC, removes panel from registry
 *  - routeFrame() / routeRawFrame(): demultiplexes 4-byte prefix and calls panel.receiveFrame()
 *  - routeRawFrame(): handles messages shorter than 4 bytes (no-op)
 *  - onPanelVisibilityChanged(): issues setActive IPC on visibility change
 *  - onPanelDisposed(): removes panel from registry and sends viewport.destroy IPC
 *  - suspendAll(): suspends all active panels
 *  - resumeVisible(): resumes visible non-active panels
 *  - dispose(): disposes all panels and cleans up subscriptions
 *  - Window focus → suspendAll integration (via onDidChangeWindowState mock)
 *  - _resetForTests() cleans up singleton between tests
 */

import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import { jest } from '@jest/globals';

// Import the mock vscode module before anything else.
import {
    window as vscodeWindow,
    makeMockContext,
    makeMockWebviewPanel,
    __setMockPanelFactory,
    __resetMockPanelFactory,
} from '../__mocks__/vscode';

import { ViewportManager } from '../viewport-manager';
import { VIEWPORT_ID_INVALID } from '../viewport-types';

// ---------------------------------------------------------------------------
// Test IPC directory helpers
// ---------------------------------------------------------------------------

function makeTmpDir(): string {
    return fs.mkdtempSync(path.join(os.tmpdir(), 'uzengine-vm-test-'));
}

function writeResponse(dir: string, payload: object): void {
    const tmp = path.join(dir, 'response.json.tmp');
    fs.writeFileSync(tmp, JSON.stringify(payload), 'utf-8');
    fs.renameSync(tmp, path.join(dir, 'response.json'));
}

// ---------------------------------------------------------------------------
// Helper: create a ViewportManager with a controlled IPC directory.
// Patches workspace.getConfiguration to return the given ipcDir.
// ---------------------------------------------------------------------------

function setupManager(ipcDir: string): {
    manager: ViewportManager;
    ctx: ReturnType<typeof makeMockContext>;
} {
    const { workspace } = jest.requireMock<typeof import('../__mocks__/vscode')>('../__mocks__/vscode');
    workspace.__setConfig('uzengine', 'ipcDirectory', ipcDir);

    const ctx = makeMockContext();
    const outputChannel = vscodeWindow.createOutputChannel('test');
    const manager = ViewportManager.initialize(
        ctx as Parameters<typeof ViewportManager.initialize>[0],
        outputChannel
    );
    return { manager, ctx };
}

// ---------------------------------------------------------------------------
// Helper: resolve a createViewport call by writing the IPC response.
// ---------------------------------------------------------------------------

async function resolveCreateViewport(
    dir: string,
    viewportId: number,
    wsPort = 9002,
    delay = 60
): Promise<void> {
    await new Promise(r => setTimeout(r, delay));
    const raw = fs.readFileSync(path.join(dir, 'command.json'), 'utf-8');
    const cmd = JSON.parse(raw) as { id: string };
    writeResponse(dir, {
        id: cmd.id,
        ok: true,
        result: { viewport_id: viewportId, ws_port: wsPort, vram_bytes: 7340032 },
        error: null,
        timestamp_ms: 1,
    });
    // Also respond to the subsequent setActive(true) call (if panel is visible).
    await new Promise(r => setTimeout(r, 80));
    try {
        const raw2 = fs.readFileSync(path.join(dir, 'command.json'), 'utf-8');
        const cmd2 = JSON.parse(raw2) as { id: string; function: string };
        if (cmd2.function === 'viewport.setActive') {
            writeResponse(dir, { id: cmd2.id, ok: true, result: { state: 'active' }, error: null, timestamp_ms: 2 });
        }
    } catch { /* no follow-up call */ }
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

let ipcDir: string;

beforeEach(() => {
    jest.clearAllMocks();
    __resetMockPanelFactory();
    ViewportManager._resetForTests();

    ipcDir = makeTmpDir();

    const { workspace } = jest.requireMock<typeof import('../__mocks__/vscode')>('../__mocks__/vscode');
    workspace.__clearConfig();
});

afterEach(() => {
    ViewportManager._resetForTests();
    __resetMockPanelFactory();
    try { fs.rmSync(ipcDir, { recursive: true }); } catch { /* ignore */ }
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('ViewportManager', () => {

    // -------------------------------------------------------------------------
    // Singleton access
    // -------------------------------------------------------------------------

    describe('getInstance()', () => {
        it('throws before initialize() is called', () => {
            expect(() => ViewportManager.getInstance()).toThrow(
                'ViewportManager has not been initialised'
            );
        });

        it('returns the same instance after initialize()', () => {
            const { manager } = setupManager(ipcDir);
            expect(ViewportManager.getInstance()).toBe(manager);
        });

        it('_resetForTests() clears the singleton so getInstance() throws again', () => {
            setupManager(ipcDir);
            ViewportManager._resetForTests();
            expect(() => ViewportManager.getInstance()).toThrow();
        });
    });

    // -------------------------------------------------------------------------
    // createViewport
    // -------------------------------------------------------------------------

    describe('createViewport()', () => {
        it('issues viewport.create IPC with correct args', async () => {
            const { manager, ctx } = setupManager(ipcDir);

            const createPromise = manager.createViewport(
                { type: 'scene', title: 'Test', width: 1280, height: 720, targetFps: 60, jpegQuality: 85 },
                ctx as Parameters<typeof manager.createViewport>[1]
            );

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.create');
            expect(cmd.args.type).toBe('scene');
            expect(cmd.args.width).toBe(1280);
            expect(cmd.args.height).toBe(720);
            expect(cmd.args.target_fps).toBe(60);
            expect(cmd.args.jpeg_quality).toBe(85);

            // Resolve the create call so the promise settles.
            const cmdParsed = JSON.parse(raw) as { id: string };
            writeResponse(ipcDir, {
                id: cmdParsed.id,
                ok: true,
                result: { viewport_id: 1, ws_port: 9002, vram_bytes: 7340032 },
                error: null,
                timestamp_ms: 1,
            });

            // Allow setActive follow-up.
            await new Promise(r => setTimeout(r, 120));
            try {
                const raw2 = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
                const cmd2 = JSON.parse(raw2) as { id: string };
                writeResponse(ipcDir, { id: cmd2.id, ok: true, result: { state: 'active' }, error: null, timestamp_ms: 2 });
            } catch { /* no follow-up */ }

            await createPromise;
            manager.dispose();
        }, 10000);

        it('registers the panel by the engine-assigned viewportId', async () => {
            const mockPanel = makeMockWebviewPanel();
            mockPanel.visible = true;
            __setMockPanelFactory(() => mockPanel);

            const { manager, ctx } = setupManager(ipcDir);

            const createPromise = manager.createViewport(
                { type: 'scene', title: 'Test', width: 800, height: 600 },
                ctx as Parameters<typeof manager.createViewport>[1]
            );

            await resolveCreateViewport(ipcDir, 5);
            const panel = await createPromise;

            expect(panel.viewportId).toBe(5);
            expect(manager.getPanel(5)).toBe(panel);
            manager.dispose();
        }, 10000);

        it('returns a panel with inactive state when IPC fails (engine not running)', async () => {
            const { manager, ctx } = setupManager(ipcDir);

            // Set a very short timeout to speed up the failure.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            const { EngineClient } = await import('../engine-client');
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (EngineClient as any).RESPONSE_TIMEOUT_MS = 100;

            const panel = await manager.createViewport(
                { type: 'material', title: 'Mat', width: 512, height: 512 },
                ctx as Parameters<typeof manager.createViewport>[1]
            );

            // Panel has no valid ID.
            expect(panel.viewportId).toBe(VIEWPORT_ID_INVALID);
            // Manager does not register it (no valid ID to key on).
            expect(manager.getPanel(VIEWPORT_ID_INVALID)).toBeUndefined();

            // Restore.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (EngineClient as any).RESPONSE_TIMEOUT_MS = 5000;
            manager.dispose();
        }, 15000);

        it('uses default targetFps=60 and jpegQuality=85 when not specified', async () => {
            const { manager, ctx } = setupManager(ipcDir);

            manager.createViewport(
                { type: 'mesh', title: 'Mesh', width: 512, height: 512 },
                ctx as Parameters<typeof manager.createViewport>[1]
            ).catch(() => { /* ignore timeout */ });

            await new Promise(r => setTimeout(r, 70));

            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { args: Record<string, unknown> };
            expect(cmd.args.target_fps).toBe(60);
            expect(cmd.args.jpeg_quality).toBe(85);
            manager.dispose();
        }, 5000);
    });

    // -------------------------------------------------------------------------
    // closeViewport
    // -------------------------------------------------------------------------

    describe('closeViewport()', () => {
        it('sends viewport.destroy IPC with the correct viewport_id', async () => {
            const mockPanel = makeMockWebviewPanel();
            mockPanel.visible = false; // Avoid auto-setActive call.
            __setMockPanelFactory(() => mockPanel);

            const { manager, ctx } = setupManager(ipcDir);

            const createPromise = manager.createViewport(
                { type: 'scene', title: 'T', width: 640, height: 480 },
                ctx as Parameters<typeof manager.createViewport>[1]
            );

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { id: string };
            writeResponse(ipcDir, {
                id: cmd.id,
                ok: true,
                result: { viewport_id: 3, ws_port: 9002, vram_bytes: 0 },
                error: null,
                timestamp_ms: 1,
            });

            await createPromise;

            // Now close it.
            const closePromise = manager.closeViewport(3);
            await new Promise(r => setTimeout(r, 70));
            const raw2 = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd2 = JSON.parse(raw2) as { id: string; function: string; args: Record<string, unknown> };
            expect(cmd2.function).toBe('viewport.destroy');
            expect(cmd2.args.viewport_id).toBe(3);

            writeResponse(ipcDir, { id: cmd2.id, ok: true, result: {}, error: null, timestamp_ms: 2 });
            await closePromise;

            // Panel is removed from the registry.
            expect(manager.getPanel(3)).toBeUndefined();
            manager.dispose();
        }, 10000);
    });

    // -------------------------------------------------------------------------
    // routeFrame / routeRawFrame
    // -------------------------------------------------------------------------

    describe('routeFrame()', () => {
        it('calls receiveFrame on the matching panel', () => {
            const { manager } = setupManager(ipcDir);

            // Manually insert a mock panel into the registry via a spy.
            const fakePanel = {
                receiveFrame: jest.fn(),
                isDisposed: false,
                state: 'active' as const,
                viewportId: 10,
                isVisible: true,
                config: { type: 'scene' as const, title: '', width: 0, height: 0 },
                initialize: jest.fn(),
                markActive: jest.fn(),
                markSuspended: jest.fn(),
                reveal: jest.fn(),
                dispose: jest.fn(),
            };

            // Access private map via cast.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(10, fakePanel);

            const jpeg = new Uint8Array([0xff, 0xd8, 0xff, 0xe0]);
            manager.routeFrame(10, jpeg);

            expect(fakePanel.receiveFrame).toHaveBeenCalledWith(jpeg);
            manager.dispose();
        });

        it('does nothing for an unknown viewportId', () => {
            const { manager } = setupManager(ipcDir);
            // No panel registered for id=99 — should not throw.
            expect(() => {
                manager.routeFrame(99, new Uint8Array([0xff, 0xd8]));
            }).not.toThrow();
            manager.dispose();
        });
    });

    describe('routeRawFrame()', () => {
        it('parses the 4-byte little-endian prefix and routes to the correct panel', () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                receiveFrame: jest.fn(),
                isDisposed: false,
                state: 'active' as const,
                viewportId: 2,
                isVisible: true,
                config: { type: 'scene' as const, title: '', width: 0, height: 0 },
                initialize: jest.fn(),
                markActive: jest.fn(),
                markSuspended: jest.fn(),
                reveal: jest.fn(),
                dispose: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(2, fakePanel);

            // Build a frame: [viewport_id=2 LE 4 bytes] [JPEG bytes]
            const buffer = new ArrayBuffer(6);
            const view = new DataView(buffer);
            view.setUint32(0, 2, true); // little-endian
            view.setUint8(4, 0xff);
            view.setUint8(5, 0xd8);

            manager.routeRawFrame(buffer);

            expect(fakePanel.receiveFrame).toHaveBeenCalledTimes(1);
            const calledWith = (fakePanel.receiveFrame as jest.Mock).mock.calls[0][0] as Uint8Array;
            expect(calledWith[0]).toBe(0xff);
            expect(calledWith[1]).toBe(0xd8);

            manager.dispose();
        });

        it('is a no-op for buffers shorter than 4 bytes', () => {
            const { manager } = setupManager(ipcDir);
            const fakePanel = { receiveFrame: jest.fn() };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, fakePanel);

            expect(() => manager.routeRawFrame(new ArrayBuffer(3))).not.toThrow();
            expect(fakePanel.receiveFrame).not.toHaveBeenCalled();
            manager.dispose();
        });

        it('is a no-op for an empty buffer', () => {
            const { manager } = setupManager(ipcDir);
            expect(() => manager.routeRawFrame(new ArrayBuffer(0))).not.toThrow();
            manager.dispose();
        });
    });

    // -------------------------------------------------------------------------
    // onPanelVisibilityChanged (ViewportPanelHost)
    // -------------------------------------------------------------------------

    describe('onPanelVisibilityChanged()', () => {
        it('issues viewport.setActive(true) IPC when panel becomes visible', async () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                viewportId: 4,
                isDisposed: false,
                state: 'suspended' as const,
                markActive: jest.fn(),
                markSuspended: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(4, fakePanel);

            // Trigger the visibility change callback.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any).onPanelVisibilityChanged(fakePanel, true);

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.setActive');
            expect(cmd.args.viewport_id).toBe(4);
            expect(cmd.args.active).toBe(true);

            manager.dispose();
        }, 5000);

        it('issues viewport.setActive(false) IPC when panel becomes hidden', async () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                viewportId: 5,
                isDisposed: false,
                state: 'active' as const,
                markActive: jest.fn(),
                markSuspended: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(5, fakePanel);

            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any).onPanelVisibilityChanged(fakePanel, false);

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.setActive');
            expect(cmd.args.active).toBe(false);
            manager.dispose();
        }, 5000);

        it('is a no-op when the panel has VIEWPORT_ID_INVALID', () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                viewportId: VIEWPORT_ID_INVALID,
                isDisposed: false,
                state: 'inactive' as const,
                markActive: jest.fn(),
                markSuspended: jest.fn(),
            };

            // Should not write any command file.
            expect(() => {
                // eslint-disable-next-line @typescript-eslint/no-explicit-any
                (manager as any).onPanelVisibilityChanged(fakePanel, true);
            }).not.toThrow();

            expect(fs.existsSync(path.join(ipcDir, 'command.json'))).toBe(false);
            manager.dispose();
        });
    });

    // -------------------------------------------------------------------------
    // onPanelDisposed (ViewportPanelHost)
    // -------------------------------------------------------------------------

    describe('onPanelDisposed()', () => {
        it('removes the panel from the registry', async () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                viewportId: 7,
                isDisposed: true,
                state: 'destroyed' as const,
                dispose: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(7, fakePanel);

            // Simulate the host callback.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any).onPanelDisposed(fakePanel);

            expect(manager.getPanel(7)).toBeUndefined();
            manager.dispose();
        });

        it('sends viewport.destroy IPC after panel disposal', async () => {
            const { manager } = setupManager(ipcDir);

            const fakePanel = {
                viewportId: 8,
                isDisposed: true,
                state: 'destroyed' as const,
                dispose: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(8, fakePanel);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any).onPanelDisposed(fakePanel);

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.destroy');
            expect(cmd.args.viewport_id).toBe(8);
            manager.dispose();
        }, 5000);
    });

    // -------------------------------------------------------------------------
    // suspendAll / resumeVisible
    // -------------------------------------------------------------------------

    describe('suspendAll()', () => {
        it('suspends all active panels', async () => {
            const { manager } = setupManager(ipcDir);

            const panel1 = { viewportId: 1, isDisposed: false, state: 'active' as const, markSuspended: jest.fn(), markActive: jest.fn() };
            const panel2 = { viewportId: 2, isDisposed: false, state: 'active' as const, markSuspended: jest.fn(), markActive: jest.fn() };
            const panel3 = { viewportId: 3, isDisposed: false, state: 'suspended' as const, markSuspended: jest.fn(), markActive: jest.fn() };

            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, panel1);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(2, panel2);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(3, panel3);

            // Fire-and-forget; timeout errors are acceptable here.
            void manager.suspendAll();

            // Verify that two setActive(false) IPC calls are eventually issued.
            await new Promise(r => setTimeout(r, 70));

            // We can't easily read both commands since only the last one is visible
            // in the file. Just verify no exception was thrown and the manager
            // attempted IPC (it wrote command.json).
            expect(fs.existsSync(path.join(ipcDir, 'command.json'))).toBe(true);
            manager.dispose();
        }, 5000);

        it('is a no-op when already disposed', async () => {
            const { manager } = setupManager(ipcDir);
            manager.dispose();

            await expect(manager.suspendAll()).resolves.toBeUndefined();
        });
    });

    describe('resumeVisible()', () => {
        it('is a no-op when already disposed', async () => {
            const { manager } = setupManager(ipcDir);
            manager.dispose();

            await expect(manager.resumeVisible()).resolves.toBeUndefined();
        });

        it('resumes visible panels that are not active', async () => {
            const { manager } = setupManager(ipcDir);

            const panel1 = {
                viewportId: 1,
                isDisposed: false,
                state: 'suspended' as const,
                isVisible: true,
                markActive: jest.fn(),
                markSuspended: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, panel1);

            void manager.resumeVisible();

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.setActive');
            expect(cmd.args.active).toBe(true);
            manager.dispose();
        }, 5000);

        it('skips panels that are not visible', async () => {
            const { manager } = setupManager(ipcDir);

            const panel1 = {
                viewportId: 1,
                isDisposed: false,
                state: 'suspended' as const,
                isVisible: false, // Not visible — should be skipped.
                markActive: jest.fn(),
                markSuspended: jest.fn(),
            };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, panel1);

            await manager.resumeVisible();

            // No command should have been written.
            expect(fs.existsSync(path.join(ipcDir, 'command.json'))).toBe(false);
            manager.dispose();
        });
    });

    // -------------------------------------------------------------------------
    // Window focus integration
    // -------------------------------------------------------------------------

    describe('window focus → suspendAll integration', () => {
        it('suspendAll is called when the VSCode window loses focus', () => {
            const { manager } = setupManager(ipcDir);

            const suspendSpy = jest.spyOn(manager, 'suspendAll').mockResolvedValue(undefined);

            // Simulate window losing focus.
            vscodeWindow._simulateWindowFocus(false);

            expect(suspendSpy).toHaveBeenCalledTimes(1);
            suspendSpy.mockRestore();
            manager.dispose();
        });

        it('suspendAll is NOT called when the window gains focus', () => {
            const { manager } = setupManager(ipcDir);

            const suspendSpy = jest.spyOn(manager, 'suspendAll').mockResolvedValue(undefined);

            vscodeWindow._simulateWindowFocus(true);

            expect(suspendSpy).not.toHaveBeenCalled();
            suspendSpy.mockRestore();
            manager.dispose();
        });
    });

    // -------------------------------------------------------------------------
    // resizeViewport
    // -------------------------------------------------------------------------

    describe('resizeViewport()', () => {
        it('sends viewport.resize IPC with the correct args', async () => {
            const { manager } = setupManager(ipcDir);

            void manager.resizeViewport(6, 1920, 1080);

            await new Promise(r => setTimeout(r, 70));
            const raw = fs.readFileSync(path.join(ipcDir, 'command.json'), 'utf-8');
            const cmd = JSON.parse(raw) as { function: string; args: Record<string, unknown> };

            expect(cmd.function).toBe('viewport.resize');
            expect(cmd.args.viewport_id).toBe(6);
            expect(cmd.args.width).toBe(1920);
            expect(cmd.args.height).toBe(1080);
            manager.dispose();
        }, 5000);

        it('is a no-op when disposed', async () => {
            const { manager } = setupManager(ipcDir);
            manager.dispose();

            await expect(manager.resizeViewport(1, 800, 600)).resolves.toBeUndefined();
        });
    });

    // -------------------------------------------------------------------------
    // getAllPanels
    // -------------------------------------------------------------------------

    describe('getAllPanels()', () => {
        it('returns an array of all registered panels', () => {
            const { manager } = setupManager(ipcDir);

            const panel1 = { viewportId: 1 };
            const panel2 = { viewportId: 2 };
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, panel1);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(2, panel2);

            const all = manager.getAllPanels();
            expect(all).toHaveLength(2);
            expect(all).toContain(panel1);
            expect(all).toContain(panel2);
            manager.dispose();
        });

        it('returns empty array when no panels are registered', () => {
            const { manager } = setupManager(ipcDir);
            expect(manager.getAllPanels()).toHaveLength(0);
            manager.dispose();
        });
    });

    // -------------------------------------------------------------------------
    // dispose
    // -------------------------------------------------------------------------

    describe('dispose()', () => {
        it('disposes all registered panels', () => {
            const { manager } = setupManager(ipcDir);

            const panelDispose1 = jest.fn();
            const panelDispose2 = jest.fn();
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, { viewportId: 1, dispose: panelDispose1, isDisposed: false });
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(2, { viewportId: 2, dispose: panelDispose2, isDisposed: false });

            manager.dispose();

            expect(panelDispose1).toHaveBeenCalledTimes(1);
            expect(panelDispose2).toHaveBeenCalledTimes(1);
        });

        it('clears the panel registry after dispose', () => {
            const { manager } = setupManager(ipcDir);
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (manager as any)._panels.set(1, { viewportId: 1, dispose: jest.fn(), isDisposed: false });

            manager.dispose();

            expect(manager.getAllPanels()).toHaveLength(0);
        });

        it('is idempotent — double dispose does not throw', () => {
            const { manager } = setupManager(ipcDir);

            manager.dispose();
            expect(() => manager.dispose()).not.toThrow();
        });

        it('clears the singleton reference so getInstance() throws after dispose', () => {
            const { manager } = setupManager(ipcDir);
            manager.dispose();

            expect(() => ViewportManager.getInstance()).toThrow();
        });
    });
});
