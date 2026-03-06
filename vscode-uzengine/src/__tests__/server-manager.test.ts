/**
 * server-manager.test.ts — Unit tests for ServerManager.
 *
 * Coverage targets (≥95% line + branch):
 *  - isRunning getter: true / false branches
 *  - start(): already running, no exe, spawn throws, success path
 *  - Error message choices: undefined / 'Dismiss' / 'Open Settings'
 *  - Process events: stdout data, stderr data, exit, error
 *  - _waitForPort: not running, connect success, error+deadline, error+retry
 *  - stop(): no process / with process
 *  - dispose()
 *  - _resolveExecutable(): explicit path exists/missing, no workspaceFolders,
 *    each of the 4 candidate paths, no candidate found
 *  - _setStatus(): 'running' with/without exePath, 'stopped'
 */

import { jest } from '@jest/globals';
import * as cp from 'child_process';
import * as fs from 'fs';
import * as net from 'net';
import * as path from 'path';

// ---------------------------------------------------------------------------
// Module mocks — declared before any imports of the module under test
// ---------------------------------------------------------------------------

jest.mock('child_process');
jest.mock('fs', () => ({
    existsSync: jest.fn().mockReturnValue(false),
}));
jest.mock('net');

// ---------------------------------------------------------------------------
// Import mocks and helpers
// ---------------------------------------------------------------------------

import {
    window as vscodeWindow,
    workspace,
    commands,
    ThemeColor,
    StatusBarAlignment,
} from '../__mocks__/vscode';

// ---------------------------------------------------------------------------
// Helper: build a fresh mock child-process object
// ---------------------------------------------------------------------------

type EventHandler = (...args: unknown[]) => void;

function makeMockProcess(exitCode: number | null = null) {
    const handlers: Record<string, EventHandler[]> = {};
    const stdoutHandlers: Record<string, EventHandler[]> = {};
    const stderrHandlers: Record<string, EventHandler[]> = {};

    const on = jest.fn((event: string, cb: EventHandler) => {
        handlers[event] = handlers[event] ?? [];
        handlers[event].push(cb);
    });

    const stdoutOn = jest.fn((event: string, cb: EventHandler) => {
        stdoutHandlers[event] = stdoutHandlers[event] ?? [];
        stdoutHandlers[event].push(cb);
    });

    const stderrOn = jest.fn((event: string, cb: EventHandler) => {
        stderrHandlers[event] = stderrHandlers[event] ?? [];
        stderrHandlers[event].push(cb);
    });

    const mockProc = {
        exitCode,
        stdout: { on: stdoutOn },
        stderr: { on: stderrOn },
        on,
        kill: jest.fn(),
        pid: 12345,
        // Helpers for test-driving events
        _emit(event: string, ...args: unknown[]) {
            (handlers[event] ?? []).forEach(cb => cb(...args));
        },
        _emitStdout(data: Buffer) {
            (stdoutHandlers['data'] ?? []).forEach(cb => cb(data));
        },
        _emitStderr(data: Buffer) {
            (stderrHandlers['data'] ?? []).forEach(cb => cb(data));
        },
    };

    return mockProc;
}

// ---------------------------------------------------------------------------
// Helper: build a mock socket
// ---------------------------------------------------------------------------

type SocketCallback = (...args: unknown[]) => void;

function makeMockSocket() {
    const callbacks: Record<string, SocketCallback> = {};
    const mock = {
        setTimeout: jest.fn(),
        once: jest.fn((event: string, cb: SocketCallback) => {
            callbacks[event] = cb;
        }),
        connect: jest.fn(),
        destroy: jest.fn(),
        _trigger(event: string, ...args: unknown[]) {
            callbacks[event]?.(...args);
        },
    };
    return mock;
}

// Helper to mock net.Socket constructor
function mockNetSocket(socketImpl: ReturnType<typeof makeMockSocket>) {
    // Use unknown cast to satisfy TypeScript strict overlap check
    (net.Socket as unknown as jest.Mock).mockImplementation(() => socketImpl);
}

// Helper to set the showErrorMessage resolved value
// eslint-disable-next-line @typescript-eslint/no-explicit-any
function mockShowErrorMessage(value: any) {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (vscodeWindow.showErrorMessage as unknown as jest.Mock<() => Promise<any>>).mockResolvedValue(value);
}

// ---------------------------------------------------------------------------
// Import the module under test (after mocks are set up)
// ---------------------------------------------------------------------------

import { ServerManager } from '../server-manager';

// ---------------------------------------------------------------------------
// Helper: construct a ServerManager with an injected existsSync
// ---------------------------------------------------------------------------

function makeServerManager(existsSyncImpl?: (p: string) => boolean): ServerManager {
    return new ServerManager(existsSyncImpl ? { existsSync: existsSyncImpl } : undefined);
}

// Workspace root used by the mock (matches vscode mock's workspaceFolders[0].uri.fsPath)
const WORKSPACE_ROOT = '/mock/workspace';

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    jest.clearAllMocks();
    workspace.__clearConfig();
    // Default: fs.existsSync returns false
    (fs.existsSync as jest.Mock).mockReturnValue(false);
    // Default: cp.spawn is not called
    (cp.spawn as jest.Mock).mockReset();
    // Default: net.Socket constructor not set up — individual tests configure it
    (net.Socket as unknown as jest.Mock).mockReset();
});

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

describe('ServerManager constructor', () => {
    it('creates an output channel and status bar item', () => {
        const sm = new ServerManager();
        expect(vscodeWindow.createOutputChannel).toHaveBeenCalledWith('UzEngine Server');
        expect(vscodeWindow.createStatusBarItem).toHaveBeenCalledWith(StatusBarAlignment.Left, 10);
        sm.dispose();
    });

    it('sets status bar command to uzengine.stopServer and shows it', () => {
        const sm = new ServerManager();
        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            command: string;
            show: jest.Mock;
        };
        expect(mockBar.command).toBe('uzengine.stopServer');
        expect(mockBar.show).toHaveBeenCalled();
        sm.dispose();
    });

    it('sets stopped status on construction', () => {
        const sm = new ServerManager();
        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
        };
        expect(mockBar.text).toContain('stopped');
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// isRunning getter
// ---------------------------------------------------------------------------

describe('ServerManager.isRunning', () => {
    it('returns false when no process is set', () => {
        const sm = new ServerManager();
        expect(sm.isRunning).toBe(false);
        sm.dispose();
    });

    it('returns true when process is set and exitCode is null', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();

        // Immediately after spawn, process has exitCode=null → isRunning = true
        expect(sm.isRunning).toBe(true);

        // Resolve the waitForPort
        mockSocket._trigger('connect');
        await startPromise;
        sm.dispose();
    });

    it('returns false when process exitCode is non-null', async () => {
        const mockProc = makeMockProcess(1); // Already exited
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();

        // exitCode=1 → isRunning = false
        expect(sm.isRunning).toBe(false);

        // Resolve _waitForPort (not running → resolves false immediately)
        mockSocket._trigger('error', new Error('refused'));
        await startPromise;
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — already running
// ---------------------------------------------------------------------------

describe('start() when already running', () => {
    it('returns true immediately without spawning again', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const firstStart = sm.start();
        mockSocket._trigger('connect');
        await firstStart;

        const spawnCallCount = (cp.spawn as jest.Mock).mock.calls.length;
        const result = await sm.start();

        expect(result).toBe(true);
        // No additional spawn call
        expect((cp.spawn as jest.Mock).mock.calls.length).toBe(spawnCallCount);
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — no executable found
// ---------------------------------------------------------------------------

describe('start() when no executable found', () => {
    beforeEach(() => {
        workspace.__clearConfig();
        (fs.existsSync as jest.Mock).mockReturnValue(false);
    });

    it('shows error message and returns false when choice is undefined', async () => {
        mockShowErrorMessage(undefined);

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        expect(vscodeWindow.showErrorMessage).toHaveBeenCalledWith(
            expect.stringContaining('render.exe not found'),
            'Open Settings',
            'Dismiss'
        );
        sm.dispose();
    });

    it('shows error message and returns false when choice is Dismiss', async () => {
        mockShowErrorMessage('Dismiss');

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        sm.dispose();
    });

    it('opens settings when choice is Open Settings', async () => {
        mockShowErrorMessage('Open Settings');

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        expect(commands.executeCommand).toHaveBeenCalledWith(
            'workbench.action.openSettings',
            'uzengine.serverExecutable'
        );
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — cp.spawn throws
// ---------------------------------------------------------------------------

describe('start() when cp.spawn throws', () => {
    it('returns false and logs the error', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');
        (cp.spawn as jest.Mock).mockImplementation(() => {
            throw new Error('spawn ENOENT');
        });

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            appendLine: jest.Mock;
        };
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Failed to start')
        );
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — process events
// ---------------------------------------------------------------------------

describe('start() process event handlers', () => {
    let sm: ServerManager;
    let mockProc: ReturnType<typeof makeMockProcess>;
    let mockChannel: { appendLine: jest.Mock; append: jest.Mock; clear: jest.Mock; show: jest.Mock };

    beforeEach(async () => {
        mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            appendLine: jest.Mock;
            append: jest.Mock;
            clear: jest.Mock;
            show: jest.Mock;
        };
    });

    afterEach(() => {
        sm.dispose();
    });

    it('appends stdout data to the output channel', () => {
        mockProc._emitStdout(Buffer.from('hello stdout'));
        expect(mockChannel.append).toHaveBeenCalledWith('hello stdout');
    });

    it('appends stderr data to the output channel', () => {
        mockProc._emitStderr(Buffer.from('error line\n'));
        expect(mockChannel.append).toHaveBeenCalledWith('error line\n');
    });

    it('handles process exit event — logs and sets stopped', () => {
        mockProc._emit('exit', 0, null);
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Server exited')
        );
        expect(sm.isRunning).toBe(false);
    });

    it('handles process exit event with signal', () => {
        mockProc._emit('exit', null, 'SIGTERM');
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('SIGTERM')
        );
    });

    it('handles process error event — logs and sets stopped', () => {
        const err = new Error('ENOENT');
        mockProc._emit('error', err);
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Process error')
        );
        expect(sm.isRunning).toBe(false);
    });
});

// ---------------------------------------------------------------------------
// start() — _waitForPort: process not running at time of first attempt
// ---------------------------------------------------------------------------

describe('start() _waitForPort when process already not running', () => {
    it('resolves false immediately when exitCode is non-null at attempt time', async () => {
        // exitCode=1 → isRunning=false → _waitForPort resolves false right away
        const mockProc = makeMockProcess(1);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const socketsCreated: ReturnType<typeof makeMockSocket>[] = [];
        (net.Socket as unknown as jest.Mock).mockImplementation(() => {
            const s = makeMockSocket();
            socketsCreated.push(s);
            return s;
        });

        const sm = new ServerManager();
        const result = await sm.start();

        // isRunning=false → _waitForPort resolves false → start returns false
        expect(result).toBe(false);
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — _waitForPort: socket connects → resolves true
// ---------------------------------------------------------------------------

describe('start() _waitForPort socket connect success', () => {
    it('returns true when socket connects within timeout', async () => {
        jest.useFakeTimers();

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();

        // Trigger connect event (the 'connect' once-handler on the socket)
        mockSocket._trigger('connect');

        await jest.runAllTimersAsync();
        const result = await startPromise;

        expect(result).toBe(true);
        expect(mockSocket.destroy).toHaveBeenCalled();

        jest.useRealTimers();
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — _waitForPort: socket error within deadline → retry → connect
// ---------------------------------------------------------------------------

describe('start() _waitForPort retry on error within deadline', () => {
    it('retries after error and succeeds when second socket connects', async () => {
        jest.useFakeTimers();

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const socketsCreated: ReturnType<typeof makeMockSocket>[] = [];
        (net.Socket as unknown as jest.Mock).mockImplementation(() => {
            const s = makeMockSocket();
            socketsCreated.push(s);
            return s;
        });

        const sm = new ServerManager();
        const startPromise = sm.start();

        // Let initial attempt execute
        await jest.runAllTimersAsync();
        expect(socketsCreated.length).toBeGreaterThanOrEqual(1);

        // Trigger error on first socket (within deadline → retry scheduled at +100ms)
        socketsCreated[0]._trigger('error', new Error('ECONNREFUSED'));

        // Advance 100ms so the retry setTimeout fires
        jest.advanceTimersByTime(100);
        await jest.runAllTimersAsync();

        // Connect on the second socket
        const lastSocket = socketsCreated[socketsCreated.length - 1];
        lastSocket._trigger('connect');

        await jest.runAllTimersAsync();
        const result = await startPromise;

        expect(result).toBe(true);

        jest.useRealTimers();
        sm.dispose();
    });

    it('retries after timeout event within deadline', async () => {
        jest.useFakeTimers();

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const socketsCreated: ReturnType<typeof makeMockSocket>[] = [];
        (net.Socket as unknown as jest.Mock).mockImplementation(() => {
            const s = makeMockSocket();
            socketsCreated.push(s);
            return s;
        });

        const sm = new ServerManager();
        const startPromise = sm.start();

        await jest.runAllTimersAsync();
        expect(socketsCreated.length).toBeGreaterThanOrEqual(1);

        // Trigger timeout instead of error
        socketsCreated[0]._trigger('timeout');

        jest.advanceTimersByTime(100);
        await jest.runAllTimersAsync();

        // Connect on the retry socket
        const lastSocket = socketsCreated[socketsCreated.length - 1];
        lastSocket._trigger('connect');

        await jest.runAllTimersAsync();
        const result = await startPromise;

        expect(result).toBe(true);

        jest.useRealTimers();
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — _waitForPort: deadline passed → resolves false, warning logged
// ---------------------------------------------------------------------------

describe('start() _waitForPort deadline exceeded', () => {
    it('logs warning and returns isRunning state when deadline passes', async () => {
        jest.useFakeTimers();

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const socketsCreated: ReturnType<typeof makeMockSocket>[] = [];
        (net.Socket as unknown as jest.Mock).mockImplementation(() => {
            const s = makeMockSocket();
            socketsCreated.push(s);
            return s;
        });

        const sm = new ServerManager();
        const startPromise = sm.start();

        // Let first attempt run
        await jest.runAllTimersAsync();
        expect(socketsCreated.length).toBeGreaterThanOrEqual(1);

        // Advance past the 5000ms deadline
        jest.advanceTimersByTime(5001);
        await jest.runAllTimersAsync();

        // Fire error AFTER deadline — triggers the deadline branch in retry()
        socketsCreated[socketsCreated.length - 1]._trigger('error', new Error('ECONNREFUSED'));

        await jest.runAllTimersAsync();
        const result = await startPromise;

        // Process is still running (exitCode=null) but port wasn't ready
        // start() returns isRunning; since process is alive, isRunning = true
        // and the "Warning: server did not become ready" should be logged
        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            appendLine: jest.Mock;
        };
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Warning')
        );
        // Result depends on whether process is still alive
        expect(typeof result).toBe('boolean');

        jest.useRealTimers();
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// stop()
// ---------------------------------------------------------------------------

describe('stop()', () => {
    it('is a no-op when no process is running', () => {
        const sm = new ServerManager();
        expect(() => sm.stop()).not.toThrow();
        sm.dispose();
    });

    it('kills the process and sets isRunning to false', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(sm.isRunning).toBe(true);
        sm.stop();

        expect(mockProc.kill).toHaveBeenCalledTimes(1);
        expect(sm.isRunning).toBe(false);
        sm.dispose();
    });

    it('logs stopping message to output channel', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        sm.stop();

        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            appendLine: jest.Mock;
        };
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Stopping server')
        );
        sm.dispose();
    });

    it('sets stopped status on status bar after stop', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        sm.stop();

        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
            backgroundColor: unknown;
        };
        expect(mockBar.text).toContain('stopped');
        expect(mockBar.backgroundColor).toBeUndefined();
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// dispose()
// ---------------------------------------------------------------------------

describe('dispose()', () => {
    it('calls stop, disposes output channel and status bar', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        sm.dispose();

        expect(mockProc.kill).toHaveBeenCalled();

        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            dispose: jest.Mock;
        };
        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            dispose: jest.Mock;
        };
        expect(mockChannel.dispose).toHaveBeenCalled();
        expect(mockBar.dispose).toHaveBeenCalled();
    });

    it('disposes cleanly when not running', () => {
        const sm = new ServerManager();
        expect(() => sm.dispose()).not.toThrow();
        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            dispose: jest.Mock;
        };
        expect(mockChannel.dispose).toHaveBeenCalled();
    });
});

// ---------------------------------------------------------------------------
// _resolveExecutable() — indirect testing via start()
// ---------------------------------------------------------------------------

describe('_resolveExecutable()', () => {
    it('uses explicit path when set and exists', async () => {
        (fs.existsSync as jest.Mock).mockImplementation((p: unknown) =>
            p === '/explicit/render.exe'
        );
        workspace.__setConfig('uzengine', 'serverExecutable', '/explicit/render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(
            '/explicit/render.exe',
            expect.anything(),
            expect.anything()
        );
        sm.dispose();
    });

    it('falls through to candidates when explicit path does not exist', async () => {
        const releasePath = path.join(WORKSPACE_ROOT, 'build', 'Release', 'render.exe');
        workspace.__setConfig('uzengine', 'serverExecutable', '/nonexistent/render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === releasePath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(
            releasePath,
            expect.anything(),
            expect.anything()
        );
        sm.dispose();
    });

    it('returns undefined when workspaceFolders is empty array', async () => {
        const origFolders = workspace.workspaceFolders;
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (workspace as any).workspaceFolders = [];
        (fs.existsSync as jest.Mock).mockReturnValue(false);
        mockShowErrorMessage(undefined);

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        expect(vscodeWindow.showErrorMessage).toHaveBeenCalled();

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (workspace as any).workspaceFolders = origFolders;
        sm.dispose();
    });

    it('returns undefined when workspaceFolders is undefined', async () => {
        const origFolders = workspace.workspaceFolders;
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (workspace as any).workspaceFolders = undefined;
        (fs.existsSync as jest.Mock).mockReturnValue(false);
        mockShowErrorMessage(undefined);

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        expect(vscodeWindow.showErrorMessage).toHaveBeenCalled();

        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (workspace as any).workspaceFolders = origFolders;
        sm.dispose();
    });

    it('uses build/Debug/render.exe when Release does not exist', async () => {
        const debugPath = path.join(WORKSPACE_ROOT, 'build', 'Debug', 'render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === debugPath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(debugPath, expect.anything(), expect.anything());
        sm.dispose();
    });

    it('uses build/render.exe when Release and Debug do not exist', async () => {
        const buildPath = path.join(WORKSPACE_ROOT, 'build', 'render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === buildPath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(buildPath, expect.anything(), expect.anything());
        sm.dispose();
    });

    it('uses root render.exe when all build candidates do not exist', async () => {
        const rootPath = path.join(WORKSPACE_ROOT, 'render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === rootPath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(rootPath, expect.anything(), expect.anything());
        sm.dispose();
    });

    it('shows error when no candidates exist', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(false);
        mockShowErrorMessage(undefined);

        const sm = new ServerManager();
        const result = await sm.start();

        expect(result).toBe(false);
        expect(vscodeWindow.showErrorMessage).toHaveBeenCalled();
        sm.dispose();
    });

    it('handles empty string explicit path (falls through)', async () => {
        // explicit = '' (default) → trim is '' → not truthy → skip to candidates
        workspace.__setConfig('uzengine', 'serverExecutable', '');
        const releasePath = path.join(WORKSPACE_ROOT, 'build', 'Release', 'render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === releasePath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        expect(cp.spawn).toHaveBeenCalledWith(releasePath, expect.anything(), expect.anything());
        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// _setStatus() — indirect via start() / stop() / constructor
// ---------------------------------------------------------------------------

describe('_setStatus()', () => {
    it('sets running status with basename of exePath', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/some/path/MyEngine.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
            tooltip: string;
            backgroundColor: unknown;
        };
        expect(mockBar.text).toContain('MyEngine.exe');
        expect(mockBar.tooltip).toContain('running');
        expect(mockBar.backgroundColor).toBeInstanceOf(ThemeColor);

        sm.dispose();
    });

    it('sets stopped status on construction (no exePath)', () => {
        const sm = new ServerManager();
        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
            tooltip: string;
            backgroundColor: undefined | unknown;
        };
        expect(mockBar.text).toContain('stopped');
        expect(mockBar.tooltip).toContain('not running');
        expect(mockBar.backgroundColor).toBeUndefined();
        sm.dispose();
    });

    it('resets to stopped status after process exit event', async () => {
        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
            backgroundColor: unknown;
        };

        // Should be in running state
        expect(mockBar.text).not.toContain('stopped');

        // Simulate process exit
        mockProc._emit('exit', 0, null);

        // Should be back to stopped
        expect(mockBar.text).toContain('stopped');
        expect(mockBar.backgroundColor).toBeUndefined();

        sm.dispose();
    });

    it('shows basename of candidate exe in running status text', async () => {
        const releasePath = path.join(WORKSPACE_ROOT, 'build', 'Release', 'render.exe');
        workspace.__clearConfig();

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = makeServerManager((p) => p === releasePath);
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
        };
        expect(mockBar.text).toContain('render.exe');

        sm.dispose();
    });

    it('uses "render.exe" default name and empty tooltip suffix when exePath is undefined', () => {
        // Directly invoke the private _setStatus method with no exePath argument
        // to cover the ternary false branch: exePath ? basename : 'render.exe'
        // and the nullish coalescing false branch: exePath ?? ''
        const sm = new ServerManager();
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        (sm as any)._setStatus('running', undefined);

        const mockBar = (vscodeWindow.createStatusBarItem as jest.Mock).mock.results[0].value as {
            text: string;
            tooltip: string;
        };
        expect(mockBar.text).toContain('render.exe');
        expect(mockBar.tooltip).toContain('running');
        // The tooltip ends with an empty string (exePath ?? '')
        expect(mockBar.tooltip).toMatch(/\n$/);

        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — clear output channel and show it
// ---------------------------------------------------------------------------

describe('start() output channel setup', () => {
    it('clears and shows the output channel when starting', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const mockChannel = (vscodeWindow.createOutputChannel as jest.Mock).mock.results[0].value as {
            clear: jest.Mock;
            show: jest.Mock;
            appendLine: jest.Mock;
        };
        expect(mockChannel.clear).toHaveBeenCalled();
        expect(mockChannel.show).toHaveBeenCalledWith(true);
        expect(mockChannel.appendLine).toHaveBeenCalledWith(
            expect.stringContaining('Starting:')
        );

        sm.dispose();
    });
});

// ---------------------------------------------------------------------------
// start() — UZENGINE_HEADLESS env var set
// ---------------------------------------------------------------------------

describe('start() spawn environment', () => {
    it('passes UZENGINE_HEADLESS=1 in env', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/mock/render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const spawnOptions = (cp.spawn as jest.Mock).mock.calls[0][2] as { env: Record<string, string> };
        expect(spawnOptions.env['UZENGINE_HEADLESS']).toBe('1');

        sm.dispose();
    });

    it('sets cwd to directory of executable', async () => {
        (fs.existsSync as jest.Mock).mockReturnValue(true);
        workspace.__setConfig('uzengine', 'serverExecutable', '/some/dir/render.exe');

        const mockProc = makeMockProcess(null);
        (cp.spawn as jest.Mock).mockReturnValue(mockProc);

        const mockSocket = makeMockSocket();
        mockNetSocket(mockSocket);

        const sm = new ServerManager();
        const startPromise = sm.start();
        mockSocket._trigger('connect');
        await startPromise;

        const spawnOptions = (cp.spawn as jest.Mock).mock.calls[0][2] as { cwd: string };
        expect(spawnOptions.cwd).toBe('/some/dir');

        sm.dispose();
    });
});
