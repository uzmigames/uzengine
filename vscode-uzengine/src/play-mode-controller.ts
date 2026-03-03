/**
 * play-mode-controller.ts — Controls the UzEngine play-mode process lifecycle.
 *
 * Manages spawning UzEngine.exe in --play mode, sending pause/resume/stop
 * IPC commands, and monitoring the process for crashes. The play process
 * runs on a separate IPC port (default 9003) from the editor (9002).
 *
 * Key responsibilities:
 *   - launchPlay(scenePath)  — spawn UzEngine.exe --play --scene=<path>
 *   - stopPlay()             — send playmode.stop IPC, SIGTERM fallback
 *   - pausePlay()            — send engine.pause IPC
 *   - resumePlay()           — send engine.resume IPC
 *   - Process exit handling  — detect crashes, update status
 */

import * as cp from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import * as vscode from 'vscode';
import { EngineClient, IpcError } from './engine-client';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Current state of the play-mode controller. */
export type PlayState = 'idle' | 'launching' | 'running' | 'stopping';

/** Options for constructing a PlayModeController. */
export interface PlayModeControllerOptions {
    /** Override the UzEngine executable path. If not set, auto-detects. */
    readonly executablePath?: string;
    /** IPC directory for the play-mode process. Defaults to <workspace>/build/Release/preview-play */
    readonly ipcDirectory?: string;
    /** Port for the play-mode IPC server. Defaults to 9003. */
    readonly port?: number;
    /** Timeout in milliseconds for the stop IPC call before SIGTERM fallback. Defaults to 2000. */
    readonly stopTimeoutMs?: number;
}

// ---------------------------------------------------------------------------
// PlayModeController
// ---------------------------------------------------------------------------

/**
 * Controls the UzEngine play-mode process.
 *
 * Usage:
 * ```typescript
 * const controller = new PlayModeController(outputChannel);
 * await controller.launchPlay('scenes/level1.uzscene');
 * // ... game is running ...
 * await controller.pausePlay();
 * await controller.resumePlay();
 * await controller.stopPlay();
 * ```
 */
export class PlayModeController implements vscode.Disposable {
    private _process: cp.ChildProcess | null = null;
    private _state: PlayState = 'idle';
    private _ipcClient: EngineClient | null = null;
    private _disposed = false;

    private readonly _outputChannel: vscode.OutputChannel;
    private readonly _options: Required<PlayModeControllerOptions>;

    /** Injected spawn function for testability. */
    private readonly _spawnFn: typeof cp.spawn;

    /** Injected EngineClient factory for testability. */
    private readonly _clientFactory: (outputChannel: vscode.OutputChannel) => EngineClient;

    /** Injected file system check for testability. */
    private readonly _existsSyncFn: (path: string) => boolean;

    /** Callback invoked when the play process exits. */
    private _onExitCallback: ((code: number | null, signal: string | null) => void) | null = null;

    constructor(
        outputChannel: vscode.OutputChannel,
        options?: PlayModeControllerOptions,
        deps?: {
            spawn?: typeof cp.spawn;
            clientFactory?: (outputChannel: vscode.OutputChannel) => EngineClient;
            existsSync?: (path: string) => boolean;
        }
    ) {
        this._outputChannel = outputChannel;
        this._options = {
            executablePath: options?.executablePath ?? '',
            ipcDirectory: options?.ipcDirectory ?? '',
            port: options?.port ?? 9003,
            stopTimeoutMs: options?.stopTimeoutMs ?? 2000,
        };
        this._spawnFn = deps?.spawn ?? cp.spawn;
        this._clientFactory = deps?.clientFactory ?? ((ch) => new EngineClient(ch));
        this._existsSyncFn = deps?.existsSync ?? fs.existsSync;
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /** Current play state. */
    get state(): PlayState {
        return this._state;
    }

    /** Whether the play process is currently alive. */
    get isRunning(): boolean {
        return this._process !== null && this._process.exitCode === null;
    }

    /**
     * Register a callback for when the play process exits.
     * @param callback  Receives (exitCode, signal).
     */
    onExit(callback: (code: number | null, signal: string | null) => void): void {
        this._onExitCallback = callback;
    }

    /**
     * Launches UzEngine.exe in --play mode with the given scene.
     *
     * @param scenePath  Path to the .uzscene file to load.
     * @throws Error if already running, disposed, or executable not found.
     */
    launchPlay(scenePath: string): void {
        if (this._disposed) {
            throw new Error('PlayModeController has been disposed');
        }
        if (this._state !== 'idle') {
            throw new Error(`Cannot launch: current state is "${this._state}"`);
        }

        const exePath = this._resolveExecutable();
        if (!exePath) {
            vscode.window.showErrorMessage(
                'UzEngine executable not found. Build the project first or set uzengine.serverExecutable.'
            );
            throw new Error('UzEngine executable not found');
        }

        this._state = 'launching';

        const args = [
            '--play',
            `--scene=${scenePath}`,
            `--port=${this._options.port}`,
        ];

        this._log(`Launching play mode: ${exePath} ${args.join(' ')}`);

        this._process = this._spawnFn(exePath, args, {
            cwd: path.dirname(exePath),
            windowsHide: true,
            env: { ...process.env, UZENGINE_HEADLESS: '1' },
        });

        this._state = 'running';

        // Pipe stdout/stderr to the output channel.
        this._process.stdout?.on('data', (data: Buffer) => {
            this._outputChannel.appendLine(data.toString().trimEnd());
        });

        this._process.stderr?.on('data', (data: Buffer) => {
            this._outputChannel.appendLine(`[stderr] ${data.toString().trimEnd()}`);
        });

        // Handle process exit.
        this._process.on('exit', (code, signal) => {
            this._log(`Play process exited — code: ${String(code)} signal: ${String(signal)}`);

            const wasStopping = this._state === 'stopping';
            this._process = null;
            this._state = 'idle';
            this._disposeClient();

            // Show notification on unexpected crash (non-zero exit that was not
            // initiated by stopPlay()).
            if (!wasStopping && code !== null && code !== 0) {
                vscode.window.showWarningMessage(
                    `UzEngine play process crashed (exit code ${code})`
                );
            }

            if (this._onExitCallback) {
                this._onExitCallback(code, signal);
            }
        });

        this._process.on('error', (err: Error) => {
            this._log(`Play process error: ${err.message}`);
            this._process = null;
            this._state = 'idle';
            this._disposeClient();

            if (this._onExitCallback) {
                this._onExitCallback(null, null);
            }
        });

        // Create an IPC client for communicating with the play process.
        this._ipcClient = this._clientFactory(this._outputChannel);
    }

    /**
     * Stops the play process gracefully.
     *
     * First sends a `playmode.stop` IPC command. If the process does not exit
     * within `stopTimeoutMs`, falls back to SIGTERM (kill).
     */
    async stopPlay(): Promise<void> {
        if (!this._process || this._state === 'idle') {
            return;
        }

        this._state = 'stopping';
        this._log('Stopping play mode...');

        // Try IPC graceful stop first.
        if (this._ipcClient) {
            try {
                await this._ipcClient.call('playmode.stop', {});
                // Wait briefly for the process to exit on its own.
                const exited = await this._waitForExit(this._options.stopTimeoutMs);
                if (exited) {
                    return;
                }
            } catch {
                // IPC failed — fall through to SIGTERM.
                this._log('IPC stop failed, falling back to SIGTERM');
            }
        }

        // SIGTERM fallback.
        if (this._process) {
            this._process.kill();
            this._process = null;
            this._state = 'idle';
            this._disposeClient();
        }
    }

    /**
     * Sends an engine.pause IPC command to the play process.
     * @throws IpcError if the command fails or the process is not running.
     */
    async pausePlay(): Promise<void> {
        this._ensureRunning('pausePlay');
        await this._ipcClient!.call('engine.pause', {});
    }

    /**
     * Sends an engine.resume IPC command to the play process.
     * @throws IpcError if the command fails or the process is not running.
     */
    async resumePlay(): Promise<void> {
        this._ensureRunning('resumePlay');
        await this._ipcClient!.call('engine.resume', {});
    }

    /** Dispose the controller, killing any running play process. */
    dispose(): void {
        this._disposed = true;
        if (this._process) {
            this._process.kill();
            this._process = null;
        }
        this._state = 'idle';
        this._disposeClient();
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private _ensureRunning(method: string): void {
        if (!this._process || this._state !== 'running') {
            throw new IpcError(
                `Cannot ${method}: play process is not running`,
                method,
                ''
            );
        }
        if (!this._ipcClient) {
            throw new IpcError(
                `Cannot ${method}: no IPC client available`,
                method,
                ''
            );
        }
    }

    private _resolveExecutable(): string | undefined {
        const explicit = this._options.executablePath.trim();
        if (explicit && this._existsSyncFn(explicit)) {
            return explicit;
        }

        const folders = vscode.workspace.workspaceFolders;
        if (!folders || folders.length === 0) {
            return undefined;
        }

        const root = folders[0].uri.fsPath;
        const candidates = [
            path.join(root, 'build', 'Release', 'UzEngine.exe'),
            path.join(root, 'build', 'Debug', 'UzEngine.exe'),
            path.join(root, 'build', 'UzEngine.exe'),
            path.join(root, 'UzEngine.exe'),
        ];

        for (const candidate of candidates) {
            if (this._existsSyncFn(candidate)) {
                return candidate;
            }
        }

        return undefined;
    }

    /** Wait for the process to exit within `ms` milliseconds. */
    private _waitForExit(ms: number): Promise<boolean> {
        return new Promise((resolve) => {
            if (!this._process) {
                resolve(true);
                return;
            }

            const timeout = setTimeout(() => {
                resolve(false);
            }, ms);

            this._process.once('exit', () => {
                clearTimeout(timeout);
                resolve(true);
            });
        });
    }

    private _disposeClient(): void {
        if (this._ipcClient) {
            this._ipcClient.dispose();
            this._ipcClient = null;
        }
    }

    private _log(message: string): void {
        this._outputChannel.appendLine(`[PlayMode] ${message}`);
    }
}
