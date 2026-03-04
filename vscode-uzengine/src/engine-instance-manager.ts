/**
 * engine-instance-manager.ts — Singleton manager for all UzEngine instances.
 *
 * Manages the lifecycle of multiple EngineInstance objects. Each open .uzscene
 * editor tab gets its own independent engine process with a unique WebSocket
 * port and IPC directory.
 *
 * Port allocation: picks a random port in the 15000–60000 range, validates
 * that it is free (TCP connect probe), and retries up to 20 times. This
 * avoids collisions with other services and with stale engine processes.
 *
 * Instance limit: controlled by `uzengine.maxInstances` setting (default 4).
 */

import * as net from 'net';
import * as vscode from 'vscode';
import { EngineInstance } from './engine-instance';

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/** Minimum port for random allocation (above common service range). */
const PORT_RANGE_MIN = 15000;
/** Maximum port for random allocation. */
const PORT_RANGE_MAX = 60000;
/** Maximum attempts to find a free port before giving up. */
const PORT_ALLOC_MAX_ATTEMPTS = 20;
const DEFAULT_MAX_INSTANCES = 4;

// ---------------------------------------------------------------------------
// EngineInstanceManager
// ---------------------------------------------------------------------------

export class EngineInstanceManager implements vscode.Disposable {
    private static _instance: EngineInstanceManager | undefined;

    private readonly _instances = new Map<string, EngineInstance>();
    private readonly _sceneToInstance = new Map<string, string>(); // scenePath → instanceId
    private readonly _usedPorts = new Set<number>();
    private readonly _outputChannel: vscode.OutputChannel;
    private readonly _statusBar: vscode.StatusBarItem;
    private readonly _onActiveChanged = new vscode.EventEmitter<EngineInstance | undefined>();
    public readonly onActiveChanged = this._onActiveChanged.event;
    private _activeInstanceId: string | undefined;
    private _disposed = false;

    // -------------------------------------------------------------------------
    // Singleton
    // -------------------------------------------------------------------------

    static getInstance(): EngineInstanceManager {
        if (!EngineInstanceManager._instance) {
            throw new Error('EngineInstanceManager has not been initialised');
        }
        return EngineInstanceManager._instance;
    }

    static initialize(
        context: vscode.ExtensionContext,
        outputChannel: vscode.OutputChannel,
    ): EngineInstanceManager {
        if (EngineInstanceManager._instance) {
            return EngineInstanceManager._instance;
        }
        const mgr = new EngineInstanceManager(outputChannel);
        EngineInstanceManager._instance = mgr;
        context.subscriptions.push(mgr);
        return mgr;
    }

    /** @internal For tests only. */
    static _resetForTests(): void {
        if (EngineInstanceManager._instance) {
            EngineInstanceManager._instance.dispose();
        }
        EngineInstanceManager._instance = undefined;
    }

    private constructor(outputChannel: vscode.OutputChannel) {
        this._outputChannel = outputChannel;
        this._statusBar = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left, 10
        );
        this._statusBar.command = 'uzengine.stopServer';
        this._updateStatusBar();
        this._statusBar.show();
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Creates or returns an existing engine instance for the given scene file.
     * If an instance already exists for this scene, returns it.
     * Otherwise, spawns a new UzEngine.exe process.
     */
    async createInstance(scenePath: string): Promise<EngineInstance> {
        // Check if an instance already exists for this scene.
        const existingId = this._sceneToInstance.get(scenePath);
        if (existingId) {
            const existing = this._instances.get(existingId);
            if (existing && !existing.isDisposed) {
                this._activeInstanceId = existingId;
                return existing;
            }
            // Clean up stale mapping.
            this._sceneToInstance.delete(scenePath);
            this._instances.delete(existingId!);
        }

        // Check instance limit.
        const config = vscode.workspace.getConfiguration('uzengine');
        const maxInstances = config.get<number>('maxInstances', DEFAULT_MAX_INSTANCES);
        if (this._instances.size >= maxInstances) {
            const choice = await vscode.window.showWarningMessage(
                `UzEngine: Maximum of ${maxInstances} engine instances reached. ` +
                'Close a scene tab to free up resources, or increase uzengine.maxInstances.',
                'Close Oldest',
                'Cancel'
            );
            if (choice === 'Close Oldest') {
                this._destroyOldestInactive();
            } else {
                throw new Error('Maximum engine instances reached');
            }
        }

        // Allocate a unique port.
        const port = await this._allocatePort();

        // Create the instance.
        const id = EngineInstance.generateId();
        const ipcDir = EngineInstance.createIpcDir(id);

        const instance = new EngineInstance({
            id,
            wsPort: port,
            ipcDir,
            scenePath,
            outputChannel: this._outputChannel,
        });

        this._instances.set(id, instance);
        this._sceneToInstance.set(scenePath, id);
        this._usedPorts.add(port);
        this._activeInstanceId = id;
        this._onActiveChanged.fire(instance);

        // Start the engine process.
        const started = await instance.start();
        if (!started) {
            this._outputChannel.appendLine(
                `[EngineInstanceManager] Warning: instance ${id.slice(0, 8)} failed to start`
            );
        }

        this._updateStatusBar();
        return instance;
    }

    /**
     * Returns the instance associated with the given scene path, if any.
     */
    getInstanceByScene(scenePath: string): EngineInstance | undefined {
        const id = this._sceneToInstance.get(scenePath);
        return id ? this._instances.get(id) : undefined;
    }

    /**
     * Returns the instance with the given ID, if any.
     */
    getInstanceById(id: string): EngineInstance | undefined {
        return this._instances.get(id);
    }

    /**
     * Returns the currently active (focused) engine instance.
     */
    getActiveInstance(): EngineInstance | undefined {
        if (!this._activeInstanceId) return undefined;
        return this._instances.get(this._activeInstanceId);
    }

    /**
     * Sets the active instance (called when a scene tab gains focus).
     */
    setActiveInstance(id: string): void {
        if (this._instances.has(id)) {
            this._activeInstanceId = id;
            this._onActiveChanged.fire(this._instances.get(id));
        }
    }

    /**
     * Returns all running instances.
     */
    getAllInstances(): ReadonlyArray<EngineInstance> {
        return Array.from(this._instances.values());
    }

    /**
     * Destroys a specific engine instance by ID.
     */
    destroyInstance(id: string): void {
        const instance = this._instances.get(id);
        if (!instance) return;

        this._usedPorts.delete(instance.wsPort);
        this._sceneToInstance.delete(instance.scenePath);
        this._instances.delete(id);

        instance.dispose();

        if (this._activeInstanceId === id) {
            // Switch active to the most recent remaining instance.
            const remaining = Array.from(this._instances.keys());
            this._activeInstanceId = remaining.length > 0
                ? remaining[remaining.length - 1]
                : undefined;
            this._onActiveChanged.fire(this.getActiveInstance());
        }

        this._updateStatusBar();
    }

    /**
     * Destroys the instance associated with the given scene path.
     */
    destroyInstanceByScene(scenePath: string): void {
        const id = this._sceneToInstance.get(scenePath);
        if (id) {
            this.destroyInstance(id);
        }
    }

    /** Number of active instances. */
    get instanceCount(): number {
        return this._instances.size;
    }

    dispose(): void {
        if (this._disposed) return;
        this._disposed = true;

        for (const instance of this._instances.values()) {
            instance.dispose();
        }
        this._instances.clear();
        this._sceneToInstance.clear();
        this._usedPorts.clear();

        this._statusBar.dispose();
        this._onActiveChanged.dispose();
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private async _allocatePort(): Promise<number> {
        for (let attempt = 0; attempt < PORT_ALLOC_MAX_ATTEMPTS; attempt++) {
            const port = PORT_RANGE_MIN + Math.floor(
                Math.random() * (PORT_RANGE_MAX - PORT_RANGE_MIN)
            );
            // Skip ports we've already handed out in this session.
            if (this._usedPorts.has(port)) { continue; }
            // Check that nothing else is listening on this port.
            const free = await EngineInstanceManager._isPortAvailable(port);
            if (free) {
                return port;
            }
            this._outputChannel.appendLine(
                `[EngineInstanceManager] Port ${port} already in use, retrying...`
            );
        }
        throw new Error(
            `Could not find a free port after ${PORT_ALLOC_MAX_ATTEMPTS} attempts ` +
            `(range ${PORT_RANGE_MIN}–${PORT_RANGE_MAX})`
        );
    }

    /**
     * Returns true if no application is currently listening on the given port.
     * Performs a quick TCP connect probe with a 300 ms timeout.
     */
    private static _isPortAvailable(port: number): Promise<boolean> {
        return new Promise((resolve) => {
            const sock = new net.Socket();
            sock.setTimeout(300);
            sock.once('connect', () => {
                // Something is already listening — port is NOT available.
                sock.destroy();
                resolve(false);
            });
            sock.once('error', () => {
                sock.destroy();
                resolve(true); // Connection refused → port is free.
            });
            sock.once('timeout', () => {
                sock.destroy();
                resolve(true); // Timed out → nothing responded → port is free.
            });
            sock.connect(port, '127.0.0.1');
        });
    }

    private _destroyOldestInactive(): void {
        // Find the first instance that is NOT the active one.
        for (const [id] of this._instances) {
            if (id !== this._activeInstanceId) {
                this.destroyInstance(id);
                return;
            }
        }
        // If all instances are active (shouldn't happen with 1 active),
        // destroy the first one.
        const firstId = this._instances.keys().next().value;
        if (firstId) {
            this.destroyInstance(firstId);
        }
    }

    private _updateStatusBar(): void {
        const count = this._instances.size;
        if (count === 0) {
            this._statusBar.text = '$(play) UzEngine: no instances';
            this._statusBar.tooltip = 'No UzEngine instances running';
            this._statusBar.backgroundColor = undefined;
        } else {
            this._statusBar.text = `$(debug-stop) UzEngine: ${count} instance${count > 1 ? 's' : ''}`;
            this._statusBar.tooltip = `${count} UzEngine instance${count > 1 ? 's' : ''} running — click to stop all`;
            this._statusBar.backgroundColor = new vscode.ThemeColor(
                'statusBarItem.warningBackground'
            );
        }
    }
}
