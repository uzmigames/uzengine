/**
 * play-mode-controller.test.ts — Unit tests for PlayModeController.
 *
 * Tests cover:
 *  - launchPlay spawns the engine with correct arguments
 *  - stopPlay sends IPC then kills if no response
 *  - pausePlay/resumePlay send correct IPC
 *  - Process crash (exit code !== 0) triggers warning notification
 *  - State machine transitions (idle -> launching -> running -> stopping -> idle)
 *  - Disposal guard
 *  - Executable resolution
 *  - Error paths
 */

import { EventEmitter } from 'events';
import { jest } from '@jest/globals';
import { PlayModeController } from '../play-mode-controller';
import type { PlayModeControllerOptions } from '../play-mode-controller';
import { window, workspace } from '../__mocks__/vscode';

// ---------------------------------------------------------------------------
// Mock types
// ---------------------------------------------------------------------------

/** Minimal mock for child_process.ChildProcess. */
interface MockProcess extends EventEmitter {
    exitCode: number | null;
    kill: jest.Mock;
    stdout: EventEmitter | null;
    stderr: EventEmitter | null;
    pid: number;
}

/** Creates a fresh mock process. */
function makeMockProcess(): MockProcess {
    const proc = new EventEmitter() as MockProcess;
    proc.exitCode = null;
    proc.kill = jest.fn(() => {
        proc.exitCode = 0;
        proc.emit('exit', 0, null);
    });
    proc.stdout = new EventEmitter();
    proc.stderr = new EventEmitter();
    proc.pid = 12345;
    return proc;
}

/** Mock EngineClient. */
function makeMockClient() {
    return {
        call: jest.fn<(fn: string, args: Record<string, unknown>) => Promise<Record<string, unknown>>>()
            .mockResolvedValue({ ok: true }),
        dispose: jest.fn(),
        readStatus: jest.fn<() => Promise<null>>().mockResolvedValue(null),
        isEngineAlive: jest.fn<() => Promise<boolean>>().mockResolvedValue(false),
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeController(
    overrides?: {
        options?: PlayModeControllerOptions;
        mockProcess?: MockProcess;
        mockClient?: ReturnType<typeof makeMockClient>;
        existsSync?: (p: string) => boolean;
    }
) {
    const outputChannel = window.createOutputChannel('UzEngine Play');
    const mockProc = overrides?.mockProcess ?? makeMockProcess();
    const mockClient = overrides?.mockClient ?? makeMockClient();

    const spawnFn = jest.fn().mockReturnValue(mockProc);
    const clientFactory = jest.fn().mockReturnValue(mockClient);
    const existsSync = overrides?.existsSync ?? (() => true);

    const controller = new PlayModeController(
        outputChannel,
        {
            executablePath: '/mock/engine/UzEngine.exe',
            port: 9003,
            stopTimeoutMs: 100,
            ...overrides?.options,
        },
        {
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            spawn: spawnFn as any,
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            clientFactory: clientFactory as any,
            existsSync,
        }
    );

    return { controller, spawnFn, clientFactory, mockProc, mockClient, outputChannel };
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    const vscMock = jest.requireMock<typeof import('../__mocks__/vscode')>('../__mocks__/vscode');
    vscMock.workspace.__clearConfig();
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('PlayModeController', () => {

    // =======================================================================
    // Construction and state
    // =======================================================================

    describe('initial state', () => {
        it('starts in idle state', () => {
            const { controller } = makeController();
            expect(controller.state).toBe('idle');
            expect(controller.isRunning).toBe(false);
            controller.dispose();
        });
    });

    // =======================================================================
    // launchPlay
    // =======================================================================

    describe('launchPlay', () => {
        it('calls spawn with correct executable and arguments', () => {
            const { controller, spawnFn } = makeController();

            controller.launchPlay('scenes/level1.uzscene');

            expect(spawnFn).toHaveBeenCalledTimes(1);
            const [exe, args, opts] = spawnFn.mock.calls[0] as [string, string[], Record<string, unknown>];
            expect(exe).toBe('/mock/engine/UzEngine.exe');
            expect(args).toContain('--play');
            expect(args).toContain('--scene=scenes/level1.uzscene');
            expect(args).toContain('--port=9003');
            expect(opts.windowsHide).toBe(true);

            controller.dispose();
        });

        it('transitions state to running after spawn', () => {
            const { controller } = makeController();
            controller.launchPlay('test.uzscene');
            expect(controller.state).toBe('running');
            expect(controller.isRunning).toBe(true);
            controller.dispose();
        });

        it('creates an IPC client for the play process', () => {
            const { controller, clientFactory } = makeController();
            controller.launchPlay('test.uzscene');
            expect(clientFactory).toHaveBeenCalledTimes(1);
            controller.dispose();
        });

        it('throws if already running', () => {
            const { controller } = makeController();
            controller.launchPlay('test.uzscene');
            expect(() => controller.launchPlay('test2.uzscene')).toThrow('Cannot launch');
            controller.dispose();
        });

        it('throws if disposed', () => {
            const { controller } = makeController();
            controller.dispose();
            expect(() => controller.launchPlay('test.uzscene')).toThrow('disposed');
        });

        it('throws and shows error if executable not found', () => {
            const { controller } = makeController({
                options: { executablePath: '' },
                existsSync: () => false,
            });

            expect(() => controller.launchPlay('test.uzscene')).toThrow('not found');
            expect(window.showErrorMessage).toHaveBeenCalledTimes(1);
            controller.dispose();
        });

        it('pipes stdout to output channel', () => {
            const { controller, mockProc, outputChannel } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.stdout!.emit('data', Buffer.from('Hello from engine'));
            expect(outputChannel.appendLine).toHaveBeenCalledWith('Hello from engine');

            controller.dispose();
        });

        it('pipes stderr to output channel with [stderr] prefix', () => {
            const { controller, mockProc, outputChannel } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.stderr!.emit('data', Buffer.from('Warning: something'));
            expect(outputChannel.appendLine).toHaveBeenCalledWith('[stderr] Warning: something');

            controller.dispose();
        });

        it('uses cwd as dirname of executable', () => {
            const { controller, spawnFn } = makeController();
            controller.launchPlay('test.uzscene');

            const [, , opts] = spawnFn.mock.calls[0] as [string, string[], Record<string, unknown>];
            expect(opts.cwd).toBe('/mock/engine');

            controller.dispose();
        });

        it('uses custom port from options', () => {
            const { controller, spawnFn } = makeController({ options: { port: 9005 } });
            controller.launchPlay('test.uzscene');

            const [, args] = spawnFn.mock.calls[0] as [string, string[]];
            expect(args).toContain('--port=9005');

            controller.dispose();
        });
    });

    // =======================================================================
    // Process exit handling
    // =======================================================================

    describe('process exit', () => {
        it('transitions to idle on normal exit (code 0)', () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 0;
            mockProc.emit('exit', 0, null);

            expect(controller.state).toBe('idle');
            expect(controller.isRunning).toBe(false);
        });

        it('calls onExit callback with exit code and signal', () => {
            const { controller, mockProc } = makeController();
            const onExit = jest.fn();
            controller.onExit(onExit);
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 0;
            mockProc.emit('exit', 0, null);

            expect(onExit).toHaveBeenCalledWith(0, null);
        });

        it('shows warning notification on crash (exit code !== 0)', () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 1;
            mockProc.emit('exit', 1, null);

            expect(window.showWarningMessage).toHaveBeenCalledTimes(1);
            expect(window.showWarningMessage).toHaveBeenCalledWith(
                expect.stringContaining('crashed')
            );
        });

        it('does not show crash warning during intentional stop', async () => {
            const { controller, mockProc, mockClient } = makeController();
            mockClient.call.mockImplementation(async () => {
                // Simulate the process exiting immediately after stop IPC.
                setTimeout(() => {
                    mockProc.exitCode = 0;
                    mockProc.emit('exit', 0, null);
                }, 10);
                return { ok: true };
            });
            controller.launchPlay('test.uzscene');

            await controller.stopPlay();

            expect(window.showWarningMessage).not.toHaveBeenCalled();
        });

        it('calls onExit callback on process error', () => {
            const { controller, mockProc } = makeController();
            const onExit = jest.fn();
            controller.onExit(onExit);
            controller.launchPlay('test.uzscene');

            mockProc.emit('error', new Error('ENOENT'));

            expect(controller.state).toBe('idle');
            expect(onExit).toHaveBeenCalledWith(null, null);
        });

        it('disposes IPC client on exit', () => {
            const { controller, mockProc, mockClient } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 0;
            mockProc.emit('exit', 0, null);

            expect(mockClient.dispose).toHaveBeenCalledTimes(1);
        });
    });

    // =======================================================================
    // stopPlay
    // =======================================================================

    describe('stopPlay', () => {
        it('sends playmode.stop IPC to the play process', async () => {
            const { controller, mockProc, mockClient } = makeController();
            mockClient.call.mockImplementation(async () => {
                setTimeout(() => {
                    mockProc.exitCode = 0;
                    mockProc.emit('exit', 0, null);
                }, 10);
                return { ok: true };
            });

            controller.launchPlay('test.uzscene');
            await controller.stopPlay();

            expect(mockClient.call).toHaveBeenCalledWith('playmode.stop', {});
        });

        it('kills process if IPC stop fails', async () => {
            const { controller, mockProc, mockClient } = makeController();
            mockClient.call.mockRejectedValue(new Error('IPC timeout'));

            controller.launchPlay('test.uzscene');
            await controller.stopPlay();

            expect(mockProc.kill).toHaveBeenCalled();
            expect(controller.state).toBe('idle');
        });

        it('kills process if IPC stop succeeds but process does not exit in time', async () => {
            const { controller, mockProc, mockClient } = makeController({
                options: { stopTimeoutMs: 50 },
            });
            mockClient.call.mockResolvedValue({ ok: true });
            // Process never exits naturally.

            controller.launchPlay('test.uzscene');
            await controller.stopPlay();

            expect(mockProc.kill).toHaveBeenCalled();
        });

        it('no-op if not running', async () => {
            const { controller } = makeController();
            // Should not throw.
            await controller.stopPlay();
            expect(controller.state).toBe('idle');
        });

        it('sets state to stopping during stop', async () => {
            const { controller, mockProc, mockClient } = makeController();
            let capturedState: string = '';
            mockClient.call.mockImplementation(async () => {
                capturedState = controller.state;
                setTimeout(() => {
                    mockProc.exitCode = 0;
                    mockProc.emit('exit', 0, null);
                }, 10);
                return { ok: true };
            });

            controller.launchPlay('test.uzscene');
            await controller.stopPlay();

            expect(capturedState).toBe('stopping');
        });
    });

    // =======================================================================
    // pausePlay / resumePlay
    // =======================================================================

    describe('pausePlay', () => {
        it('sends engine.pause IPC to the play process', async () => {
            const { controller, mockClient } = makeController();
            mockClient.call.mockResolvedValue({ ok: true });

            controller.launchPlay('test.uzscene');
            await controller.pausePlay();

            expect(mockClient.call).toHaveBeenCalledWith('engine.pause', {});
        });

        it('throws if not running', async () => {
            const { controller } = makeController();
            await expect(controller.pausePlay()).rejects.toThrow('not running');
        });
    });

    describe('resumePlay', () => {
        it('sends engine.resume IPC to the play process', async () => {
            const { controller, mockClient } = makeController();
            mockClient.call.mockResolvedValue({ ok: true });

            controller.launchPlay('test.uzscene');
            await controller.resumePlay();

            expect(mockClient.call).toHaveBeenCalledWith('engine.resume', {});
        });

        it('throws if not running', async () => {
            const { controller } = makeController();
            await expect(controller.resumePlay()).rejects.toThrow('not running');
        });
    });

    // =======================================================================
    // dispose
    // =======================================================================

    describe('dispose', () => {
        it('kills running process', () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');
            controller.dispose();
            expect(mockProc.kill).toHaveBeenCalled();
        });

        it('sets state to idle', () => {
            const { controller } = makeController();
            controller.launchPlay('test.uzscene');
            controller.dispose();
            expect(controller.state).toBe('idle');
        });

        it('disposes IPC client', () => {
            const { controller, mockClient } = makeController();
            controller.launchPlay('test.uzscene');
            controller.dispose();
            expect(mockClient.dispose).toHaveBeenCalled();
        });

        it('is safe to call multiple times', () => {
            const { controller } = makeController();
            controller.dispose();
            controller.dispose();
            expect(controller.state).toBe('idle');
        });
    });

    // =======================================================================
    // Executable resolution
    // =======================================================================

    describe('executable resolution', () => {
        it('uses explicit executablePath if file exists', () => {
            const existsSync = jest.fn((p: string) => p === '/custom/UzEngine.exe');
            const { controller, spawnFn } = makeController({
                options: { executablePath: '/custom/UzEngine.exe' },
                existsSync,
            });

            controller.launchPlay('test.uzscene');
            const [exe] = spawnFn.mock.calls[0] as [string];
            expect(exe).toBe('/custom/UzEngine.exe');
            controller.dispose();
        });

        it('falls back to workspace candidates if explicit path does not exist', () => {
            const existsSync = jest.fn((p: string) =>
                p === '/mock/workspace/build/Debug/UzEngine.exe'
            );
            const { controller, spawnFn } = makeController({
                options: { executablePath: '/nonexistent/UzEngine.exe' },
                existsSync,
            });

            controller.launchPlay('test.uzscene');
            const [exe] = spawnFn.mock.calls[0] as [string];
            expect(exe).toBe('/mock/workspace/build/Debug/UzEngine.exe');
            controller.dispose();
        });

        it('returns undefined if no workspace folders', () => {
            const savedFolders = workspace.workspaceFolders;
            workspace.workspaceFolders = [];

            const { controller } = makeController({
                options: { executablePath: '' },
                existsSync: () => false,
            });

            expect(() => controller.launchPlay('test.uzscene')).toThrow('not found');

            workspace.workspaceFolders = savedFolders;
            controller.dispose();
        });

        it('resolves build/Release candidate first', () => {
            const existsSync = jest.fn((p: string) =>
                p === '/mock/workspace/build/Release/UzEngine.exe' ||
                p === '/mock/workspace/build/Debug/UzEngine.exe'
            );
            const { controller, spawnFn } = makeController({
                options: { executablePath: '' },
                existsSync,
            });

            controller.launchPlay('test.uzscene');
            const [exe] = spawnFn.mock.calls[0] as [string];
            expect(exe).toBe('/mock/workspace/build/Release/UzEngine.exe');
            controller.dispose();
        });

        it('tries all candidates in order', () => {
            const existsSync = jest.fn((p: string) =>
                p === '/mock/workspace/UzEngine.exe'
            );
            const { controller, spawnFn } = makeController({
                options: { executablePath: '' },
                existsSync,
            });

            controller.launchPlay('test.uzscene');
            const [exe] = spawnFn.mock.calls[0] as [string];
            expect(exe).toBe('/mock/workspace/UzEngine.exe');
            controller.dispose();
        });

        it('ignores empty explicit path', () => {
            const existsSync = jest.fn((p: string) =>
                p === '/mock/workspace/build/Release/UzEngine.exe'
            );
            const { controller, spawnFn } = makeController({
                options: { executablePath: '  ' },
                existsSync,
            });

            controller.launchPlay('test.uzscene');
            const [exe] = spawnFn.mock.calls[0] as [string];
            expect(exe).toBe('/mock/workspace/build/Release/UzEngine.exe');
            controller.dispose();
        });
    });

    // =======================================================================
    // onExit callback
    // =======================================================================

    describe('onExit callback', () => {
        it('receives signal on SIGTERM exit', () => {
            const { controller, mockProc } = makeController();
            const onExit = jest.fn();
            controller.onExit(onExit);
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = null;
            mockProc.emit('exit', null, 'SIGTERM');

            expect(onExit).toHaveBeenCalledWith(null, 'SIGTERM');
        });
    });

    // =======================================================================
    // Default options
    // =======================================================================

    describe('default options', () => {
        it('uses port 9003 by default', () => {
            const { controller, spawnFn } = makeController({ options: {} });
            controller.launchPlay('test.uzscene');

            const [, args] = spawnFn.mock.calls[0] as [string, string[]];
            expect(args).toContain('--port=9003');
            controller.dispose();
        });
    });

    // =======================================================================
    // Edge cases: null stdout/stderr
    // =======================================================================

    describe('null stdout/stderr', () => {
        it('handles process with null stdout and stderr', () => {
            const mockProc = makeMockProcess();
            mockProc.stdout = null;
            mockProc.stderr = null;

            const { controller } = makeController({ mockProcess: mockProc });
            // Should not throw even with null stdout/stderr.
            controller.launchPlay('test.uzscene');
            expect(controller.state).toBe('running');
            controller.dispose();
        });
    });

    // =======================================================================
    // Edge cases: exit without onExit callback set
    // =======================================================================

    describe('exit without onExit callback', () => {
        it('exits cleanly without onExit callback', () => {
            const { controller, mockProc } = makeController();
            // Do NOT register an onExit callback.
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 0;
            mockProc.emit('exit', 0, null);

            expect(controller.state).toBe('idle');
        });

        it('exits cleanly on error without onExit callback', () => {
            const { controller, mockProc } = makeController();
            // Do NOT register an onExit callback.
            controller.launchPlay('test.uzscene');

            mockProc.emit('error', new Error('spawn ENOENT'));

            expect(controller.state).toBe('idle');
        });
    });

    // =======================================================================
    // Edge cases: crash with code = null (signal kill) should not show warning
    // =======================================================================

    describe('signal-killed process', () => {
        it('does not show crash warning when exit code is null (signal kill)', () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');

            // Process killed by signal: code is null.
            mockProc.exitCode = null;
            mockProc.emit('exit', null, 'SIGKILL');

            expect(window.showWarningMessage).not.toHaveBeenCalled();
        });

        it('does not show crash warning when exit code is 0', () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');

            mockProc.exitCode = 0;
            mockProc.emit('exit', 0, null);

            expect(window.showWarningMessage).not.toHaveBeenCalled();
        });
    });

    // =======================================================================
    // Edge cases: stopPlay when process exits before timeout
    // =======================================================================

    describe('stopPlay edge cases', () => {
        it('returns immediately if state is idle', async () => {
            const { controller, mockClient } = makeController();
            await controller.stopPlay();
            // Should not have called IPC.
            expect(mockClient.call).not.toHaveBeenCalled();
        });

        it('handles stopPlay when ipcClient is null', async () => {
            const { controller, mockProc } = makeController();
            controller.launchPlay('test.uzscene');

            // Simulate client being disposed (e.g., by a previous exit event).
            // Forcibly null out the client via dispose.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (controller as any)._ipcClient = null;

            await controller.stopPlay();

            // Should fall through to kill.
            expect(mockProc.kill).toHaveBeenCalled();
        });
    });

    // =======================================================================
    // Edge cases: launching state transitions
    // =======================================================================

    describe('state during launch', () => {
        it('cannot launch while in stopping state', async () => {
            const { controller, mockClient, mockProc } = makeController({
                options: { stopTimeoutMs: 200 },
            });
            mockClient.call.mockImplementation(async () => {
                // Process exits after IPC.
                setTimeout(() => {
                    mockProc.exitCode = 0;
                    mockProc.emit('exit', 0, null);
                }, 10);
                return { ok: true };
            });
            controller.launchPlay('test.uzscene');

            const stopPromise = controller.stopPlay();
            // While stopping, cannot launch.
            expect(() => controller.launchPlay('test2.uzscene')).toThrow('Cannot launch');

            await stopPromise;
        });
    });

    // =======================================================================
    // Edge cases: _ensureRunning with null ipcClient
    // =======================================================================

    describe('_ensureRunning with null ipcClient', () => {
        it('pausePlay throws when ipcClient is null but process is running', async () => {
            const { controller } = makeController();
            controller.launchPlay('test.uzscene');

            // Forcibly null out the client.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (controller as any)._ipcClient = null;

            await expect(controller.pausePlay()).rejects.toThrow('no IPC client');
            controller.dispose();
        });

        it('resumePlay throws when ipcClient is null but process is running', async () => {
            const { controller } = makeController();
            controller.launchPlay('test.uzscene');

            // Forcibly null out the client.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (controller as any)._ipcClient = null;

            await expect(controller.resumePlay()).rejects.toThrow('no IPC client');
            controller.dispose();
        });
    });

    // =======================================================================
    // Edge cases: _waitForExit when process already null
    // =======================================================================

    describe('_waitForExit when process already null', () => {
        it('stopPlay resolves immediately when process becomes null during IPC', async () => {
            const { controller, mockProc, mockClient } = makeController();
            controller.launchPlay('test.uzscene');

            mockClient.call.mockImplementation(async () => {
                // Simulate process becoming null before _waitForExit runs.
                // This happens if the exit event fires synchronously during the
                // IPC call.
                // eslint-disable-next-line @typescript-eslint/no-explicit-any
                (controller as any)._process = null;
                // Also reset state as exit handler would.
                // eslint-disable-next-line @typescript-eslint/no-explicit-any
                (controller as any)._state = 'idle';
                return { ok: true };
            });

            await controller.stopPlay();
            // _waitForExit returns true immediately because _process is null.
            // State is already idle from mock.
            expect(controller.state).toBe('idle');
        });
    });

    // =======================================================================
    // Edge cases: default constructor options (no options argument)
    // =======================================================================

    describe('default constructor options', () => {
        it('handles construction with no options at all', () => {
            const outputChannel = window.createOutputChannel('UzEngine Play');
            const mockProc = makeMockProcess();
            const mockClient = makeMockClient();

            const controller = new PlayModeController(
                outputChannel,
                undefined,   // No options
                {
                    // eslint-disable-next-line @typescript-eslint/no-explicit-any
                    spawn: jest.fn().mockReturnValue(mockProc) as any,
                    // eslint-disable-next-line @typescript-eslint/no-explicit-any
                    clientFactory: jest.fn().mockReturnValue(mockClient) as any,
                    existsSync: (p: string) => p.includes('UzEngine.exe'),
                }
            );

            expect(controller.state).toBe('idle');
            controller.dispose();
        });

        it('handles construction with empty options object', () => {
            const outputChannel = window.createOutputChannel('UzEngine Play');
            const mockProc = makeMockProcess();
            const mockClient = makeMockClient();

            const controller = new PlayModeController(
                outputChannel,
                {},   // Empty options
                {
                    // eslint-disable-next-line @typescript-eslint/no-explicit-any
                    spawn: jest.fn().mockReturnValue(mockProc) as any,
                    // eslint-disable-next-line @typescript-eslint/no-explicit-any
                    clientFactory: jest.fn().mockReturnValue(mockClient) as any,
                    existsSync: (p: string) => p.includes('UzEngine.exe'),
                }
            );

            expect(controller.state).toBe('idle');
            controller.dispose();
        });
    });
});
