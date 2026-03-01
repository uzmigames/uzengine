import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';
import * as fs from 'fs';

/**
 * Manages the UzEngine.exe child process lifecycle.
 *
 * The extension starts the server automatically when the stream viewer opens,
 * and stops it when VSCode is deactivated or the user runs "Stop Server".
 *
 * The executable is located by searching in order:
 *   1. uzengine.serverExecutable setting (absolute path)
 *   2. <workspace>/build/Release/UzEngine.exe
 *   3. <workspace>/build/Debug/UzEngine.exe
 *   4. <workspace>/build/UzEngine.exe
 */
export class ServerManager implements vscode.Disposable {
    private _process: cp.ChildProcess | undefined;
    private _outputChannel: vscode.OutputChannel;
    private _statusBar: vscode.StatusBarItem;

    constructor() {
        this._outputChannel = vscode.window.createOutputChannel('UzEngine Server');
        this._statusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 10);
        this._statusBar.command = 'uzengine.stopServer';
        this._setStatus('stopped');
        this._statusBar.show();
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /** Returns true if the server process is currently running. */
    get isRunning(): boolean {
        return this._process !== undefined && this._process.exitCode === null;
    }

    /**
     * Starts the UzEngine server. No-op if already running.
     * @returns true if the process was started (or was already running).
     */
    async start(): Promise<boolean> {
        if (this.isRunning) {
            return true;
        }

        const exePath = this._resolveExecutable();
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

        this._outputChannel.clear();
        this._outputChannel.appendLine(`[UzEngine] Starting: ${exePath}`);
        this._outputChannel.show(true);

        try {
            this._process = cp.spawn(exePath, [], {
                cwd: path.dirname(exePath),
                windowsHide: false, // show the render window
            });
        } catch (err) {
            this._outputChannel.appendLine(`[UzEngine] Failed to start: ${String(err)}`);
            this._setStatus('stopped');
            return false;
        }

        this._setStatus('running', exePath);

        this._process.stdout?.on('data', (data: Buffer) => {
            this._outputChannel.append(data.toString());
        });

        this._process.stderr?.on('data', (data: Buffer) => {
            this._outputChannel.append(data.toString());
        });

        this._process.on('exit', (code, signal) => {
            this._outputChannel.appendLine(
                `\n[UzEngine] Server exited — code: ${String(code)} signal: ${String(signal)}`
            );
            this._process = undefined;
            this._setStatus('stopped');
        });

        this._process.on('error', (err) => {
            this._outputChannel.appendLine(`[UzEngine] Process error: ${err.message}`);
            this._process = undefined;
            this._setStatus('stopped');
        });

        return true;
    }

    /** Stops the server process if running. */
    stop(): void {
        if (!this._process) {
            return;
        }
        this._outputChannel.appendLine('[UzEngine] Stopping server...');
        this._process.kill();
        this._process = undefined;
        this._setStatus('stopped');
    }

    dispose(): void {
        this.stop();
        this._outputChannel.dispose();
        this._statusBar.dispose();
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private _resolveExecutable(): string | undefined {
        const config = vscode.workspace.getConfiguration('uzengine');
        const explicit = config.get<string>('serverExecutable', '').trim();
        if (explicit && fs.existsSync(explicit)) {
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
            if (fs.existsSync(candidate)) {
                return candidate;
            }
        }

        return undefined;
    }

    private _setStatus(state: 'running' | 'stopped', exePath?: string): void {
        if (state === 'running') {
            const name = exePath ? path.basename(exePath) : 'UzEngine.exe';
            this._statusBar.text = `$(debug-stop) UzEngine: ${name}`;
            this._statusBar.tooltip = `UzEngine server running — click to stop\n${exePath ?? ''}`;
            this._statusBar.backgroundColor = new vscode.ThemeColor('statusBarItem.warningBackground');
        } else {
            this._statusBar.text = '$(play) UzEngine: stopped';
            this._statusBar.tooltip = 'UzEngine server is not running';
            this._statusBar.backgroundColor = undefined;
        }
    }
}
