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
import { EngineClient, IpcError } from '../engine-client';
import { window } from '../__mocks__/vscode';

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

/** Creates an EngineClient whose IPC directory is overridden via workspace config. */
function makeClient(ipcDir: string): EngineClient {
    // Patch workspace.getConfiguration to return ipcDir for 'ipcDirectory' key.
    const { workspace } = jest.requireMock<typeof import('../__mocks__/vscode')>('../__mocks__/vscode');
    workspace.__setConfig('uzengine', 'ipcDirectory', ipcDir);

    const outputChannel = window.createOutputChannel('test');
    return new EngineClient(outputChannel);
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
    const { workspace } = jest.requireMock<typeof import('../__mocks__/vscode')>('../__mocks__/vscode');
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
});
