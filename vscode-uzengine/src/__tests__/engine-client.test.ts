/**
 * engine-client.test.ts — Unit tests for EngineClient.
 *
 * Tests cover:
 *  - Command serialisation and file writing
 *  - Response polling and parsing
 *  - Timeout / missing response error handling
 *  - ok=false engine error propagation
 *  - Serialised call queue (concurrent calls execute sequentially)
 *  - Disposal guard
 *  - readStatus() and isEngineAlive()
 */

import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import * as vscode from 'vscode';
import { EngineClient, IpcError } from '../engine-client';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** Creates a temporary directory for IPC files and returns its path. */
function makeTmpDir(): string {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'uzengine-test-'));
    return dir;
}

/** Writes response.json into the given directory with the provided payload. */
function writeResponse(dir: string, payload: object): void {
    const content = JSON.stringify(payload);
    const tmp = path.join(dir, 'response.json.tmp');
    fs.writeFileSync(tmp, content, 'utf-8');
    fs.renameSync(tmp, path.join(dir, 'response.json'));
}

/** Reads and parses the most recently written command.json from the directory. */
function readCommand(dir: string): { id: string; function: string; args: Record<string, unknown> } {
    const raw = fs.readFileSync(path.join(dir, 'command.json'), 'utf-8');
    return JSON.parse(raw) as { id: string; function: string; args: Record<string, unknown> };
}

/** Creates a minimal mock OutputChannel for EngineClient. */
function makeMockOutputChannel() {
    return {
        appendLine: jest.fn(),
        append: jest.fn(),
        clear: jest.fn(),
        show: jest.fn(),
        hide: jest.fn(),
        dispose: jest.fn(),
        replace: jest.fn(),
        name: 'test',
    };
}

/** Creates an EngineClient whose IPC directory is overridden directly.
 *
 * We bypass workspace.getConfiguration entirely and patch the private
 * `baseDir` property directly on the instance after construction to avoid
 * the Jest module caching issue where jest.requireMock('vscode') and
 * the vscode module loaded via moduleNameMapper are different instances.
 */
function makeClient(ipcDir: string): EngineClient {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const client = new EngineClient(makeMockOutputChannel() as any);
    // Override the private baseDir to point to our temp directory.
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (client as any).baseDir = ipcDir;
    return client;
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const { workspace } = jest.requireMock<any>('vscode');
    workspace.__clearConfig();
    jest.clearAllMocks();
});

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

describe('EngineClient', () => {

    describe('call() — basic round-trip', () => {
        it('writes command.json with correct id, function, and args fields', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            // Simulate the engine writing a response after we write the command.
            const callPromise = client.call('engine.ping', { foo: 'bar' });

            // Give the client time to write command.json before we read it.
            await new Promise(r => setTimeout(r, 60));

            const cmd = readCommand(dir);
            expect(cmd.function).toBe('engine.ping');
            expect(cmd.args).toEqual({ foo: 'bar' });
            expect(typeof cmd.id).toBe('string');
            expect(cmd.id).toMatch(/^[0-9a-f-]{36}$/);

            // Now write the response so the call can resolve.
            writeResponse(dir, { id: cmd.id, ok: true, result: { pong: true }, error: null, timestamp_ms: 1 });

            const result = await callPromise;
            expect(result).toEqual({ pong: true });
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('resolves with the result object from the response', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('viewport.create', {
                type: 'scene', width: 1280, height: 720,
            });

            await new Promise(r => setTimeout(r, 60));
            const cmd = readCommand(dir);

            writeResponse(dir, {
                id: cmd.id,
                ok: true,
                result: { viewport_id: 1, ws_port: 9002, vram_bytes: 7340032 },
                error: null,
                timestamp_ms: 1,
            });

            const result = await callPromise;
            expect(result).toEqual({ viewport_id: 1, ws_port: 9002, vram_bytes: 7340032 });
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('call() — error handling', () => {
        it('throws IpcError when the engine returns ok=false', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('viewport.create', { type: 'scene', width: 0, height: 0 });

            await new Promise(r => setTimeout(r, 60));
            const cmd = readCommand(dir);

            writeResponse(dir, {
                id: cmd.id,
                ok: false,
                result: {},
                error: 'Invalid dimensions',
                timestamp_ms: 1,
            });

            await expect(callPromise).rejects.toBeInstanceOf(IpcError);
            await expect(callPromise).rejects.toMatchObject({
                message: expect.stringContaining('Invalid dimensions'),
                functionName: 'viewport.create',
            });

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('uses "unknown error" message when error field is null in ok=false response', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('engine.ping', {});
            await new Promise(r => setTimeout(r, 60));
            const cmd = readCommand(dir);

            // error: null — triggers the `?? 'unknown error'` branch.
            writeResponse(dir, { id: cmd.id, ok: false, result: {}, error: null, timestamp_ms: 1 });

            await expect(callPromise).rejects.toBeInstanceOf(IpcError);
            await expect(callPromise).rejects.toMatchObject({
                message: expect.stringContaining('unknown error'),
            });

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('throws IpcError when response does not appear within RESPONSE_TIMEOUT_MS', async () => {
            // Use a very short timeout by patching the private constant via a
            // subclass approach — instead we create a real dir but never write
            // the response.  The default 5s timeout is too slow for a unit test,
            // so we override via a private accessor workaround.
            //
            // Real strategy: we use a short-timeout client by writing NO response.
            // We accept that this test takes up to 5s in CI. To make it faster
            // we override the static via Object.defineProperty.
            const dir = makeTmpDir();
            const client = makeClient(dir);

            // Patch timeout to 100ms for speed.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (EngineClient as any).RESPONSE_TIMEOUT_MS = 100;

            const callPromise = client.call('engine.ping', {});

            await expect(callPromise).rejects.toBeInstanceOf(IpcError);
            await expect(callPromise).rejects.toMatchObject({
                message: expect.stringContaining('timed out'),
            });

            // Restore timeout.
            // eslint-disable-next-line @typescript-eslint/no-explicit-any
            (EngineClient as any).RESPONSE_TIMEOUT_MS = 5000;

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        }, 10000);

        it('throws IpcError if called after dispose()', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);
            client.dispose();

            await expect(client.call('engine.ping', {})).rejects.toBeInstanceOf(IpcError);
            await expect(client.call('engine.ping', {})).rejects.toMatchObject({
                message: expect.stringContaining('disposed'),
            });

            fs.rmSync(dir, { recursive: true });
        });

        it('handles malformed response.json (partial write) by continuing to poll', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('engine.ping', {});
            await new Promise(r => setTimeout(r, 60));
            const cmd = readCommand(dir);

            // Write malformed JSON first.
            fs.writeFileSync(path.join(dir, 'response.json'), '{ "id": "', 'utf-8');

            // Then write a valid response after a short delay.
            setTimeout(() => {
                writeResponse(dir, { id: cmd.id, ok: true, result: { alive: true }, error: null, timestamp_ms: 1 });
            }, 80);

            const result = await callPromise;
            expect(result).toEqual({ alive: true });

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('ignores response.json with a different command id and keeps polling', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('engine.ping', {});
            await new Promise(r => setTimeout(r, 60));
            const cmd = readCommand(dir);

            // Write a response for a different command id first.
            writeResponse(dir, { id: 'different-id', ok: true, result: {}, error: null, timestamp_ms: 1 });

            // Then write the correct response.
            setTimeout(() => {
                writeResponse(dir, { id: cmd.id, ok: true, result: { pong: 1 }, error: null, timestamp_ms: 2 });
            }, 80);

            const result = await callPromise;
            expect(result).toEqual({ pong: 1 });

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('call() — concurrency / serialisation', () => {
        it('serialises concurrent calls so they execute sequentially', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const order: string[] = [];

            // Start two calls concurrently.
            const p1 = client.call('cmd.one', {}).then(r => { order.push('one'); return r; });
            const p2 = client.call('cmd.two', {}).then(r => { order.push('two'); return r; });

            // Respond to each command as it appears (sequential).
            const respond = async () => {
                // Wait for cmd.one to appear.
                await new Promise(r => setTimeout(r, 60));
                const cmd1 = readCommand(dir);
                writeResponse(dir, { id: cmd1.id, ok: true, result: { n: 1 }, error: null, timestamp_ms: 1 });

                // Wait for cmd.two to appear.
                await new Promise(r => setTimeout(r, 80));
                const cmd2 = readCommand(dir);
                writeResponse(dir, { id: cmd2.id, ok: true, result: { n: 2 }, error: null, timestamp_ms: 2 });
            };

            await Promise.all([p1, p2, respond()]);

            // cmd.one must be sent before cmd.two.
            expect(order).toEqual(['one', 'two']);

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('readStatus()', () => {
        it('returns parsed status when status.json is valid', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const statusPayload = {
                alive: true,
                fps: 59.8,
                frame: 1000,
                uptime_s: 120,
                encoder: 'JPEG',
                resolution: '1280x720',
            };
            fs.writeFileSync(path.join(dir, 'status.json'), JSON.stringify(statusPayload), 'utf-8');

            const status = await client.readStatus();
            expect(status).not.toBeNull();
            expect(status?.alive).toBe(true);
            expect(status?.fps).toBeCloseTo(59.8, 1);
            expect(status?.encoder).toBe('JPEG');

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns null when status.json does not exist', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const status = await client.readStatus();
            expect(status).toBeNull();

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns null when status.json contains malformed JSON', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            fs.writeFileSync(path.join(dir, 'status.json'), '{ broken json', 'utf-8');
            const status = await client.readStatus();
            expect(status).toBeNull();

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('isEngineAlive()', () => {
        it('returns true when status.json reports alive=true', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            fs.writeFileSync(
                path.join(dir, 'status.json'),
                JSON.stringify({ alive: true, fps: 60, frame: 1, uptime_s: 1, encoder: 'JPEG', resolution: '1280x720' }),
                'utf-8'
            );

            expect(await client.isEngineAlive()).toBe(true);
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns false when status.json does not exist', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            expect(await client.isEngineAlive()).toBe(false);
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns false when status.json reports alive=false', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            fs.writeFileSync(
                path.join(dir, 'status.json'),
                JSON.stringify({ alive: false, fps: 0, frame: 0, uptime_s: 0, encoder: 'JPEG', resolution: '' }),
                'utf-8'
            );

            expect(await client.isEngineAlive()).toBe(false);
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('IpcError class', () => {
        it('has correct name, message, functionName, and commandId properties', () => {
            const err = new IpcError('test message', 'viewport.create', 'cmd-123');
            expect(err.name).toBe('IpcError');
            expect(err.message).toBe('test message');
            expect(err.functionName).toBe('viewport.create');
            expect(err.commandId).toBe('cmd-123');
            expect(err).toBeInstanceOf(Error);
            expect(err).toBeInstanceOf(IpcError);
        });
    });

    describe('resolveBaseDir()', () => {
        it('returns the explicit ipcDirectory config value when set', () => {
            const dir = makeTmpDir();
            const client = new EngineClient(makeMockOutputChannel() as any);
            // Set the config on the same vscode instance used by engine-client.ts.
            (vscode.workspace as any).__setConfig('uzengine', 'ipcDirectory', '/custom/ipc/dir');

            const resolved = (client as any).resolveBaseDir();
            expect(resolved).toBe('/custom/ipc/dir');

            (vscode.workspace as any).__clearConfig();
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns the first existing candidate path when no explicit config is set', () => {
            const dir = makeTmpDir();
            const debugPath = path.join(dir, 'build', 'Debug', 'preview');
            fs.mkdirSync(debugPath, { recursive: true });

            const client = new EngineClient(makeMockOutputChannel() as any);
            // Override workspace folder to our temp dir.
            const origFolders = (vscode.workspace as any).workspaceFolders;
            (vscode.workspace as any).workspaceFolders = [{ uri: { fsPath: dir }, name: 'test', index: 0 }];
            (vscode.workspace as any).__clearConfig();

            const resolved = (client as any).resolveBaseDir();
            // The Release path does not exist, so it should find Debug.
            expect(resolved).toBe(path.join(dir, 'build', 'Debug', 'preview'));

            (vscode.workspace as any).workspaceFolders = origFolders;
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns the Release fallback when no candidate path exists', () => {
            const dir = makeTmpDir();
            const client = new EngineClient(makeMockOutputChannel() as any);
            // Override workspace folder to our temp dir (no build dirs created).
            const origFolders = (vscode.workspace as any).workspaceFolders;
            (vscode.workspace as any).workspaceFolders = [{ uri: { fsPath: dir }, name: 'test', index: 0 }];
            (vscode.workspace as any).__clearConfig();

            const resolved = (client as any).resolveBaseDir();
            expect(resolved).toBe(path.join(dir, 'build', 'Release', 'preview'));

            (vscode.workspace as any).workspaceFolders = origFolders;
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });

        it('returns a relative default path when no workspace folders are open', () => {
            const dir = makeTmpDir();
            const client = new EngineClient(makeMockOutputChannel() as any);
            const origFolders = (vscode.workspace as any).workspaceFolders;
            (vscode.workspace as any).workspaceFolders = undefined;
            (vscode.workspace as any).__clearConfig();

            const resolved = (client as any).resolveBaseDir();
            expect(resolved).toBe(path.join('build', 'Release', 'preview'));

            (vscode.workspace as any).workspaceFolders = origFolders;
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('call() — mkdir failure', () => {
        it('throws IpcError when the IPC directory cannot be created', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            // Replace the internal baseDir with a path that will cause mkdir to fail:
            // use a path where a file exists at the parent location.
            const blockingFile = path.join(dir, 'blocker');
            fs.writeFileSync(blockingFile, 'x', 'utf-8');
            // Point baseDir to a subdirectory of the file — mkdir will fail.
            (client as any).baseDir = path.join(blockingFile, 'subdir');

            await expect(client.call('engine.ping', {})).rejects.toBeInstanceOf(IpcError);
            await expect(client.call('engine.ping', {})).rejects.toMatchObject({
                message: expect.stringContaining('Failed to create IPC directory'),
            });

            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('call() — disposed while polling', () => {
        it('throws IpcError when the client is disposed while waiting for a response', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            const callPromise = client.call('engine.ping', {});

            // Wait for the command to be written, then dispose mid-poll.
            await new Promise(r => setTimeout(r, 60));
            client.dispose();

            await expect(callPromise).rejects.toBeInstanceOf(IpcError);
            await expect(callPromise).rejects.toMatchObject({
                message: expect.stringContaining('disposed'),
            });

            fs.rmSync(dir, { recursive: true });
        });
    });

    describe('call() — write command.json failure', () => {
        it('throws IpcError when writeFile fails', async () => {
            const dir = makeTmpDir();
            const client = makeClient(dir);

            // Spy on fs.promises.writeFile to throw.
            const writeError = new Error('ENOSPC: no space left on device');
            const writeFileSpy = jest.spyOn(fs.promises, 'writeFile').mockRejectedValueOnce(writeError);

            const callPromise = client.call('engine.ping', {});

            await expect(callPromise).rejects.toBeInstanceOf(IpcError);
            await expect(callPromise).rejects.toMatchObject({
                message: expect.stringContaining('Failed to write command.json'),
            });

            writeFileSpy.mockRestore();
            client.dispose();
            fs.rmSync(dir, { recursive: true });
        });
    });
});
