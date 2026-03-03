/**
 * engine-client.ts — File-based IPC client for the UzEngine process.
 *
 * The engine exposes a file-based IPC protocol:
 *   - Commands written to   <baseDir>/command.json
 *   - Responses read from   <baseDir>/response.json
 *   - Status polled from    <baseDir>/status.json
 *
 * The poller thread inside the engine watches command.json via mtime.
 * On any file change it parses the JSON, dispatches to the IpcRegistry, and
 * atomically writes response.json. Command deduplication is by the `id` field.
 *
 * This client serialises concurrent calls through a promise queue so that
 * only one command.json write is in-flight at a time, preventing lost
 * responses due to rapid file overwrites.
 */

import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import { randomBytes } from 'crypto';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** The JSON payload written to command.json. */
interface IpcCommand {
    readonly id: string;
    readonly function: string;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly args: Record<string, any>;
}

/** The JSON payload read from response.json after a command completes. */
interface IpcResponse {
    readonly id: string;
    readonly ok: boolean;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly result: Record<string, any>;
    readonly error: string | null;
    readonly timestamp_ms: number;
}

/** Engine status read from status.json. */
export interface EngineStatus {
    readonly alive: boolean;
    readonly fps: number;
    readonly frame: number;
    readonly uptime_s: number;
    readonly encoder: string;
    readonly resolution: string;
}

/**
 * Structured error thrown when the engine returns `ok: false` or the IPC
 * mechanism itself fails (timeout, file I/O error, engine not running).
 */
export class IpcError extends Error {
    constructor(
        message: string,
        public readonly functionName: string,
        public readonly commandId: string
    ) {
        super(message);
        this.name = 'IpcError';
    }
}

// ---------------------------------------------------------------------------
// EngineClient
// ---------------------------------------------------------------------------

/**
 * Type-safe client for the UzEngine file-based IPC protocol.
 *
 * Usage:
 * ```typescript
 * const client = new EngineClient(outputChannel);
 * const result = await client.call('viewport.create', { type: 'scene', ... });
 * ```
 *
 * Concurrent calls are serialised via an internal queue so the command.json
 * file is never overwritten before the previous response is read.
 */
export class EngineClient implements vscode.Disposable {
    /** Timeout in milliseconds to wait for a response after writing command.json. */
    private static readonly RESPONSE_TIMEOUT_MS = 5000;

    /** Polling interval in milliseconds when waiting for response.json. */
    private static readonly POLL_INTERVAL_MS = 25;

    /** Absolute path to the directory containing command/response/status JSON files. */
    private readonly baseDir: string;

    /** Serialisation queue: the in-flight promise (or null when idle). */
    private callQueue: Promise<unknown> = Promise.resolve();

    /** Whether this client has been disposed. */
    private disposed = false;

    private readonly outputChannel: vscode.OutputChannel;

    constructor(outputChannel: vscode.OutputChannel) {
        this.outputChannel = outputChannel;
        this.baseDir = this.resolveBaseDir();
    }

    dispose(): void {
        this.disposed = true;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Sends an IPC command to the engine and waits for its response.
     *
     * @param functionName  The registry key (e.g. `"viewport.create"`).
     * @param args          Arguments object serialised into the `args` field.
     * @returns             The `result` object from the engine response.
     * @throws IpcError     If the engine returns ok=false or the call times out.
     */
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    async call(functionName: string, args: Record<string, any>): Promise<Record<string, any>> {
        if (this.disposed) {
            throw new IpcError('EngineClient has been disposed', functionName, '');
        }

        // Serialise: chain onto the existing queue so writes never race.
        const result = this.callQueue.then(() => this.executeCall(functionName, args));
        // Keep the queue alive (suppress unhandled rejections by chaining a catch).
        this.callQueue = result.catch(() => undefined);
        return result;
    }

    /**
     * Reads the latest engine status without sending a command.
     * Returns null if the engine is not running or status.json cannot be read.
     */
    async readStatus(): Promise<EngineStatus | null> {
        const statusPath = path.join(this.baseDir, 'status.json');
        try {
            const raw = await fs.promises.readFile(statusPath, 'utf-8');
            const parsed = JSON.parse(raw) as EngineStatus;
            return parsed;
        } catch {
            return null;
        }
    }

    /**
     * Returns true if the engine appears to be running (status.json reports alive=true).
     */
    async isEngineAlive(): Promise<boolean> {
        const status = await this.readStatus();
        return status?.alive === true;
    }

    // -------------------------------------------------------------------------
    // Private implementation
    // -------------------------------------------------------------------------

    /**
     * Resolves the IPC base directory.
     *
     * Priority:
     *   1. uzengine.ipcDirectory setting (absolute path)
     *   2. <workspace>/build/Release/preview
     *   3. <workspace>/build/Debug/preview
     *   4. <workspace>/build/preview
     */
    private resolveBaseDir(): string {
        const config = vscode.workspace.getConfiguration('uzengine');
        const explicit = config.get<string>('ipcDirectory', '').trim();
        if (explicit) {
            return explicit;
        }

        const folders = vscode.workspace.workspaceFolders;
        if (folders && folders.length > 0) {
            const root = folders[0].uri.fsPath;
            const candidates = [
                path.join(root, 'build', 'Release', 'preview'),
                path.join(root, 'build', 'Debug', 'preview'),
                path.join(root, 'build', 'preview'),
            ];
            for (const candidate of candidates) {
                if (fs.existsSync(candidate)) {
                    return candidate;
                }
            }
            // Fall through to the Release path even if it doesn't exist yet —
            // the engine will create it on startup.
            return candidates[0];
        }

        // No workspace open — use a reasonable default.
        return path.join('build', 'Release', 'preview');
    }

    /** Generates a cryptographically random command ID (UUID v4 style). */
    private generateCommandId(): string {
        const hex = randomBytes(16).toString('hex');
        return [
            hex.slice(0, 8),
            hex.slice(8, 12),
            hex.slice(12, 16),
            hex.slice(16, 20),
            hex.slice(20),
        ].join('-');
    }

    /**
     * Core call implementation: writes command.json, polls response.json until
     * the matching response appears, then parses and returns it.
     */
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    private async executeCall(functionName: string, args: Record<string, any>): Promise<Record<string, any>> {
        const commandId = this.generateCommandId();
        const commandPath = path.join(this.baseDir, 'command.json');
        const responsePath = path.join(this.baseDir, 'response.json');

        const command: IpcCommand = {
            id: commandId,
            function: functionName,
            args,
        };

        const commandJson = JSON.stringify(command);

        this.log(`→ IPC call [${commandId.slice(0, 8)}] ${functionName}(${JSON.stringify(args)})`);

        // Ensure the directory exists before writing.
        try {
            await fs.promises.mkdir(this.baseDir, { recursive: true });
        } catch (err) {
            throw new IpcError(
                `Failed to create IPC directory: ${String(err)}`,
                functionName,
                commandId
            );
        }

        // Write command.json atomically via a temp file to avoid partial reads.
        const tmpPath = commandPath + '.tmp';
        try {
            await fs.promises.writeFile(tmpPath, commandJson, 'utf-8');
            await fs.promises.rename(tmpPath, commandPath);
        } catch (err) {
            throw new IpcError(
                `Failed to write command.json: ${String(err)}`,
                functionName,
                commandId
            );
        }

        // Poll response.json until we see a response with our commandId.
        const response = await this.waitForResponse(commandId, responsePath);

        this.log(`← IPC response [${commandId.slice(0, 8)}] ok=${String(response.ok)}`);

        if (!response.ok) {
            throw new IpcError(
                `Engine returned error for ${functionName}: ${response.error ?? 'unknown error'}`,
                functionName,
                commandId
            );
        }

        return response.result;
    }

    /**
     * Polls response.json until it contains a response matching `expectedId`,
     * or until the timeout expires.
     */
    private async waitForResponse(expectedId: string, responsePath: string): Promise<IpcResponse> {
        const deadline = Date.now() + EngineClient.RESPONSE_TIMEOUT_MS;

        while (Date.now() < deadline) {
            if (this.disposed) {
                throw new IpcError('EngineClient disposed while waiting for response', '', expectedId);
            }

            let raw: string;
            try {
                raw = await fs.promises.readFile(responsePath, 'utf-8');
            } catch {
                // response.json not yet written — keep polling.
                await this.sleep(EngineClient.POLL_INTERVAL_MS);
                continue;
            }

            let parsed: IpcResponse;
            try {
                parsed = JSON.parse(raw) as IpcResponse;
            } catch {
                // Malformed JSON mid-write — keep polling.
                await this.sleep(EngineClient.POLL_INTERVAL_MS);
                continue;
            }

            if (parsed.id === expectedId) {
                return parsed;
            }

            // Different ID — the engine hasn't processed our command yet.
            await this.sleep(EngineClient.POLL_INTERVAL_MS);
        }

        throw new IpcError(
            `IPC call timed out after ${EngineClient.RESPONSE_TIMEOUT_MS}ms — engine not responding`,
            '',
            expectedId
        );
    }

    /** Promisified setTimeout helper. */
    private sleep(ms: number): Promise<void> {
        return new Promise(resolve => setTimeout(resolve, ms));
    }

    private log(message: string): void {
        this.outputChannel.appendLine(`[EngineClient] ${message}`);
    }
}
