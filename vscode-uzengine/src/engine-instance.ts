/**
 * engine-instance.ts — Represents a single UzEngine.exe process instance.
 *
 * Each EngineInstance owns:
 *   - A child process (UzEngine.exe)
 *   - A unique WebSocket port
 *   - A unique IPC directory (temp directory)
 *   - An EngineClient pointed at that IPC directory
 *   - The scene file path it's rendering
 *
 * Multiple EngineInstances can run in parallel, each serving a different
 * .uzscene editor tab with fully independent rendering.
 */

import * as cp from 'child_process';
import { randomUUID } from 'crypto';
import * as fs from 'fs';
import * as net from 'net';
import * as os from 'os';
import * as path from 'path';
import * as vscode from 'vscode';
import { EngineClient } from './engine-client';

// ---------------------------------------------------------------------------
// EngineInstance
// ---------------------------------------------------------------------------

export class EngineInstance implements vscode.Disposable {
    /** Unique identifier for this instance. */
    public readonly id: string;

    /** WebSocket port this instance listens on. */
    public readonly wsPort: number;

    /** IPC base directory for this instance. */
    public readonly ipcDir: string;

    /** The .uzscene file this instance is rendering. */
    public readonly scenePath: string;

    /** The EngineClient for communicating with this specific instance. */
    public readonly engineClient: EngineClient;

    private _process: cp.ChildProcess | undefined;
    private _disposed = false;
    private readonly _outputChannel: vscode.OutputChannel;

    /** Injected file-system check for testability. */
    private readonly _existsSyncFn: (p: string) => boolean;

    constructor(opts: {
        id: string;
        wsPort: number;
        ipcDir: string;
        scenePath: string;
        outputChannel: vscode.OutputChannel;
        existsSync?: (p: string) => boolean;
    }) {
        this.id = opts.id;
        this.wsPort = opts.wsPort;
        this.ipcDir = opts.ipcDir;
        this.scenePath = opts.scenePath;
        this._outputChannel = opts.outputChannel;
        this._existsSyncFn = opts.existsSync ?? fs.existsSync;

        // Create the IPC directory if it doesn't exist.
        if (!fs.existsSync(this.ipcDir)) {
            fs.mkdirSync(this.ipcDir, { recursive: true });
        }

        // Create an EngineClient pointed at this instance's IPC dir.
        this.engineClient = new EngineClient(this._outputChannel, this.ipcDir);
    }

    /** Whether the engine process is currently running. */
    get isRunning(): boolean {
        return this._process !== undefined && this._process.exitCode === null;
    }

    /** Whether this instance has been disposed. */
    get isDisposed(): boolean {
        return this._disposed;
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * Starts the UzEngine.exe process for this instance.
     * @returns true if the process started successfully.
     */
    async start(): Promise<boolean> {
        if (this.isRunning) {
            return true;
        }

        const exePath = EngineInstance.resolveExecutable(this._existsSyncFn);
        if (!exePath) {
            const choice = await vscode.window.showErrorMessage(
                'UzEngine.exe not found. Build the project first or set uzengine.serverExecutable.',
                'Open Settings',
                'Dismiss'
            );
            if (choice === 'Open Settings') {
                await vscode.commands.executeCommand(
                    'workbench.action.openSettings',
                    'uzengine.serverExecutable'
                );
            }
            return false;
        }

        this._outputChannel.appendLine(
            `[UzEngine:${this.id.slice(0, 8)}] Starting: ${exePath} --ws-port ${this.wsPort} --ipc-dir "${this.ipcDir}" --scene "${this.scenePath}"`
        );

        const spawnArgs = [
            '--ws-port', String(this.wsPort),
            '--ipc-dir', this.ipcDir,
            '--scene', this.scenePath,
        ];

        try {
            this._process = cp.spawn(exePath, spawnArgs, {
                cwd: path.dirname(exePath),
                windowsHide: true,
                env: { ...process.env, UZENGINE_HEADLESS: '1' },
            });
        } catch (err) {
            this._outputChannel.appendLine(
                `[UzEngine:${this.id.slice(0, 8)}] Failed to start: ${String(err)}`
            );
            return false;
        }

        this._process.stdout?.on('data', (data: Buffer) => {
            this._outputChannel.append(data.toString());
        });

        this._process.stderr?.on('data', (data: Buffer) => {
            this._outputChannel.append(data.toString());
        });

        this._process.on('exit', (code, signal) => {
            this._outputChannel.appendLine(
                `[UzEngine:${this.id.slice(0, 8)}] Exited — code: ${String(code)} signal: ${String(signal)}`
            );
            this._process = undefined;
        });

        this._process.on('error', (err) => {
            this._outputChannel.appendLine(
                `[UzEngine:${this.id.slice(0, 8)}] Process error: ${err.message}`
            );
            this._process = undefined;
        });

        const ready = await EngineInstance._waitForPort(this.wsPort, 5000, () => this.isRunning);
        if (!ready) {
            this._outputChannel.appendLine(
                `[UzEngine:${this.id.slice(0, 8)}] Warning: server did not become ready within 5s ` +
                `(port=${this.wsPort} scene=${this.scenePath})`
            );
        }

        const running = this.isRunning;
        this._outputChannel.appendLine(
            `[UzEngine:${this.id.slice(0, 8)}] start() result: isRunning=${running} port=${this.wsPort}`
        );
        return running;
    }

    /** Stops the engine process. */
    stop(): void {
        if (!this._process) {
            return;
        }
        this._outputChannel.appendLine(
            `[UzEngine:${this.id.slice(0, 8)}] Stopping...`
        );
        this._process.kill();
        this._process = undefined;
    }

    /** Disposes this instance: stops the process and cleans up the IPC dir. */
    dispose(): void {
        if (this._disposed) {
            return;
        }
        this._disposed = true;
        this.stop();
        this.engineClient.dispose();

        // Clean up the IPC directory.
        try {
            fs.rmSync(this.ipcDir, { recursive: true, force: true });
        } catch {
            // Best effort — the OS will clean up temp dirs anyway.
        }
    }

    // -------------------------------------------------------------------------
    // Static helpers
    // -------------------------------------------------------------------------

    /**
     * Resolves the UzEngine.exe executable path.
     * Searches in order:
     *   1. uzengine.serverExecutable setting (absolute path)
     *   2. <workspace>/build/Release/UzEngine.exe
     *   3. <workspace>/build/Debug/UzEngine.exe
     *   4. <workspace>/build/UzEngine.exe
     *   5. <workspace>/UzEngine.exe
     */
    static resolveExecutable(existsSyncFn: (p: string) => boolean = fs.existsSync): string | undefined {
        const config = vscode.workspace.getConfiguration('uzengine');
        const explicit = config.get<string>('serverExecutable', '').trim();
        if (explicit && existsSyncFn(explicit)) {
            return explicit;
        }

        const workspaceFolders = vscode.workspace.workspaceFolders;
        if (!workspaceFolders || workspaceFolders.length === 0) {
            return undefined;
        }

        const root = workspaceFolders[0].uri.fsPath;
        const candidates = [
            path.join(root, 'build', 'Release', 'UzEngine.exe'),
            path.join(root, 'build', 'Debug', 'UzEngine.exe'),
            path.join(root, 'build', 'UzEngine.exe'),
            path.join(root, 'UzEngine.exe'),
        ];

        for (const candidate of candidates) {
            if (existsSyncFn(candidate)) {
                return candidate;
            }
        }

        return undefined;
    }

    /**
     * Creates a unique IPC directory in the OS temp folder.
     */
    static createIpcDir(instanceId: string): string {
        const dir = path.join(os.tmpdir(), `uzengine-${instanceId}`);
        fs.mkdirSync(dir, { recursive: true });
        return dir;
    }

    /**
     * Generates a new unique instance ID.
     */
    static generateId(): string {
        return randomUUID();
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /**
     * Polls a TCP port until connectable or timeout.
     */
    private static _waitForPort(
        port: number,
        timeoutMs: number,
        isRunning: () => boolean
    ): Promise<boolean> {
        return new Promise((resolve) => {
            const deadline = Date.now() + timeoutMs;

            const attempt = () => {
                if (!isRunning()) {
                    resolve(false);
                    return;
                }
                const sock = new net.Socket();
                sock.setTimeout(200);
                sock.once('connect', () => {
                    sock.destroy();
                    resolve(true);
                });
                sock.once('error', retry);
                sock.once('timeout', retry);
                sock.connect(port, '127.0.0.1');

                function retry() {
                    sock.destroy();
                    if (Date.now() >= deadline) {
                        resolve(false);
                    } else {
                        setTimeout(attempt, 100);
                    }
                }
            };

            attempt();
        });
    }
}
