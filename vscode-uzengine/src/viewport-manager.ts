/**
 * viewport-manager.ts — ViewportManager singleton.
 *
 * The ViewportManager is the single authority over all open ViewportPanels in
 * the extension. It:
 *
 *  1. Creates ViewportPanels on demand and registers them by ViewportId.
 *  2. Issues `viewport.create / destroy / setActive / resize` IPC calls to the
 *     engine at the right moments in the panel lifecycle.
 *  3. Routes incoming WebSocket JPEG frames to the correct ViewportPanel by
 *     demultiplexing the 4-byte viewport_id prefix on each binary message.
 *  4. Suspends all active viewports when the VSCode window loses focus and
 *     resumes visible panels when focus is regained.
 *
 * IPC calls that fail because the engine is not running are logged to the
 * output channel and swallowed — the UI remains open but shows a "suspended"
 * state until the engine comes back online.
 *
 * Singleton access: `ViewportManager.getInstance()`
 */

import * as vscode from 'vscode';
import { EngineClient, IpcError } from './engine-client';
import {
    ViewportConfig,
    ViewportCreateResult,
    ViewportId,
    VIEWPORT_ID_INVALID,
} from './viewport-types';
import { ViewportPanel, ViewportPanelHost } from './viewport-panel';

// ---------------------------------------------------------------------------
// ViewportManager
// ---------------------------------------------------------------------------

/**
 * Singleton that manages all open viewport panels and their engine-side state.
 *
 * Consumers should call `ViewportManager.getInstance()` to obtain the shared
 * instance. The instance is initialised via `initialize()` during extension
 * activation.
 */
export class ViewportManager implements vscode.Disposable, ViewportPanelHost {
    private static _instance: ViewportManager | undefined;

    /** Map from engine ViewportId → ViewportPanel. */
    private readonly _panels: Map<ViewportId, ViewportPanel> = new Map();

    /** Output channel shared with EngineClient and callers. */
    private readonly _outputChannel: vscode.OutputChannel;

    /** IPC client — shared singleton instance. */
    private readonly _engineClient: EngineClient;

    /** Whether dispose() has been called. */
    private _disposed = false;

    /** Disposables owned by the manager (window focus subscription, etc.). */
    private readonly _disposables: vscode.Disposable[] = [];

    /** Emits when a new viewport panel is created. */
    private static readonly _onViewportCreated = new vscode.EventEmitter<ViewportPanel>();
    static readonly onViewportCreated: vscode.Event<ViewportPanel> = ViewportManager._onViewportCreated.event;

    // -------------------------------------------------------------------------
    // Singleton access
    // -------------------------------------------------------------------------

    /** Returns the global ViewportManager instance. Throws if not initialised. */
    static getInstance(): ViewportManager {
        if (!ViewportManager._instance) {
            throw new Error(
                'ViewportManager has not been initialised. ' +
                'Call ViewportManager.initialize() in the activate() function.'
            );
        }
        return ViewportManager._instance;
    }

    /**
     * Initialises the global ViewportManager instance.
     * Must be called exactly once in `activate()`.
     *
     * @param context       Extension context (for subscription management).
     * @param outputChannel Shared output channel for diagnostic logging.
     */
    static initialize(
        context: vscode.ExtensionContext,
        outputChannel: vscode.OutputChannel
    ): ViewportManager {
        if (ViewportManager._instance) {
            return ViewportManager._instance;
        }
        ViewportManager._instance = new ViewportManager(context, outputChannel);
        return ViewportManager._instance;
    }

    /**
     * Resets the singleton (for use in tests only — production code must not
     * call this; it destroys all panels without IPC cleanup).
     *
     * @internal
     */
    static _resetForTests(): void {
        if (ViewportManager._instance) {
            ViewportManager._instance._disposed = true;
            ViewportManager._instance._panels.clear();
            ViewportManager._instance = undefined;
        }
    }

    // -------------------------------------------------------------------------
    // Construction (private — use static factory)
    // -------------------------------------------------------------------------

    private constructor(
        context: vscode.ExtensionContext,
        outputChannel: vscode.OutputChannel
    ) {
        this._outputChannel = outputChannel;
        this._engineClient = new EngineClient(outputChannel);

        // Subscribe to window focus changes to suspend/resume all viewports.
        const focusSub = vscode.window.onDidChangeWindowState(
            (state: vscode.WindowState) => {
                if (!state.focused) {
                    void this.suspendAll();
                }
                // Individual panel onDidChangeViewState events handle resumption
                // when the user clicks back into specific tabs.
            }
        );

        this._disposables.push(focusSub, this._engineClient);
        context.subscriptions.push(this);
    }

    // -------------------------------------------------------------------------
    // Public API — viewport creation / management
    // -------------------------------------------------------------------------

    /**
     * Creates a new viewport: sends `viewport.create` IPC, creates a
     * ViewportPanel, and maps the engine-assigned ID to the panel.
     *
     * @param config   Viewport type, title, dimensions, fps, quality settings.
     * @param context  Extension context — used to register the panel disposable.
     * @returns        The newly created ViewportPanel.
     * @throws         IpcError if the IPC call fails and the engine is running.
     */
    async createViewport(
        config: ViewportConfig,
        context: vscode.ExtensionContext
    ): Promise<ViewportPanel> {
        if (this._disposed) {
            throw new Error('ViewportManager has been disposed');
        }

        // Create the panel first (shows UI immediately).
        const panel = new ViewportPanel(config, this, context);

        try {
            // Issue viewport.create IPC call.
            const result = await this._engineClient.call('viewport.create', {
                type: config.type,
                width: config.width,
                height: config.height,
                target_fps: config.targetFps ?? 60,
                jpeg_quality: config.jpegQuality ?? 85,
                debug_name: config.debugName ?? config.title,
            }) as unknown as ViewportCreateResult;

            const viewportId = result.viewport_id;
            const wsPort = result.ws_port;

            if (viewportId === VIEWPORT_ID_INVALID) {
                throw new IpcError(
                    'Engine returned an invalid viewport_id (0)',
                    'viewport.create',
                    ''
                );
            }

            // Finalise the panel with the assigned ID and WebSocket port.
            panel.initialize(viewportId, wsPort);

            // Register the panel in the map.
            this._panels.set(viewportId, panel);

            this._log(`Viewport created: id=${viewportId} type=${config.type} ${config.width}x${config.height}`);

            // Notify listeners (e.g. SelectionManager wiring in extension.ts).
            ViewportManager._onViewportCreated.fire(panel);

            // Activate immediately if visible.
            if (panel.isVisible) {
                await this._setActive(viewportId, true);
            }

            return panel;
        } catch (err) {
            // If IPC failed (engine not running), keep the panel open in an
            // inactive state — the user sees the "Initialising…" placeholder.
            // The panel is NOT registered in the map because it has no valid ID.
            const message = err instanceof Error ? err.message : String(err);
            this._log(`viewport.create failed: ${message}`);
            vscode.window.showWarningMessage(
                `UzEngine: Could not create viewport — engine may not be running. ${message}`
            );
            // Return the panel in inactive state so callers can still dispose it.
            return panel;
        }
    }

    /**
     * Explicitly destroys a viewport by ID: sends `viewport.destroy` IPC and
     * removes the panel from the registry. Safe to call if the engine is down.
     *
     * @param id  The ViewportId to destroy.
     */
    async closeViewport(id: ViewportId): Promise<void> {
        if (this._disposed) { return; }

        const panel = this._panels.get(id);
        this._panels.delete(id);

        try {
            await this._engineClient.call('viewport.destroy', { viewport_id: id });
            this._log(`Viewport destroyed: id=${id}`);
        } catch (err) {
            this._log(`viewport.destroy(${id}) failed: ${err instanceof Error ? err.message : String(err)}`);
            // Engine may already be down — safe to swallow.
        }

        panel?.dispose();
    }

    /**
     * Routes an incoming JPEG frame to the correct ViewportPanel.
     * Called by the WebSocket message handler after demultiplexing the 4-byte
     * viewport_id prefix prepended by the engine to every binary frame.
     *
     * Frame wire format: [4 bytes: viewport_id uint32 LE] [N bytes: JPEG data]
     *
     * @param viewportId  The parsed viewport_id from the frame header.
     * @param jpeg        The JPEG payload (prefix already stripped).
     */
    routeFrame(viewportId: ViewportId, jpeg: Uint8Array): void {
        const panel = this._panels.get(viewportId);
        if (panel) {
            panel.receiveFrame(jpeg);
        }
    }

    /**
     * Parses a raw binary WebSocket message (ArrayBuffer) and routes it to the
     * correct panel. Convenience method for the WebSocket `onmessage` handler.
     *
     * @param data  Raw ArrayBuffer from the WebSocket binary message.
     */
    routeRawFrame(data: ArrayBuffer): void {
        if (data.byteLength < 4) { return; }
        const view = new DataView(data);
        const viewportId = view.getUint32(0, true); // little-endian
        const jpeg = new Uint8Array(data, 4);
        this.routeFrame(viewportId, jpeg);
    }

    /**
     * Resizes a viewport's render targets.
     * Sends `viewport.resize` IPC. Silently fails if the engine is not running.
     *
     * @param id      Target viewport ID.
     * @param width   New width in pixels.
     * @param height  New height in pixels.
     */
    async resizeViewport(id: ViewportId, width: number, height: number): Promise<void> {
        if (this._disposed) { return; }
        try {
            await this._engineClient.call('viewport.resize', {
                viewport_id: id,
                width,
                height,
            });
        } catch (err) {
            this._log(`viewport.resize(${id}) failed: ${err instanceof Error ? err.message : String(err)}`);
        }
    }

    /**
     * Suspends all currently active viewports (e.g., when the VSCode window
     * loses focus). Issues `viewport.setActive(false)` IPC for each.
     */
    async suspendAll(): Promise<void> {
        if (this._disposed) { return; }
        const promises: Promise<void>[] = [];
        for (const [id, panel] of this._panels) {
            if (panel.state === 'active') {
                promises.push(this._setActive(id, false));
            }
        }
        await Promise.allSettled(promises);
    }

    /**
     * Resumes all viewports whose VSCode panels are currently visible.
     * Issues `viewport.setActive(true)` IPC for each visible, suspended panel.
     * Called when the window regains focus.
     */
    async resumeVisible(): Promise<void> {
        if (this._disposed) { return; }
        const promises: Promise<void>[] = [];
        for (const [id, panel] of this._panels) {
            if (panel.isVisible && panel.state !== 'active' && panel.state !== 'destroyed') {
                promises.push(this._setActive(id, true));
            }
        }
        await Promise.allSettled(promises);
    }

    /**
     * Returns the ViewportPanel registered for the given ID, or undefined.
     */
    getPanel(viewportId: ViewportId): ViewportPanel | undefined {
        return this._panels.get(viewportId);
    }

    /**
     * Returns all currently registered panels (for status/debug inspection).
     */
    getAllPanels(): ReadonlyArray<ViewportPanel> {
        return Array.from(this._panels.values());
    }

    /** The EngineClient instance (accessible for direct calls in edge cases). */
    get engineClient(): EngineClient {
        return this._engineClient;
    }

    // -------------------------------------------------------------------------
    // ViewportPanelHost implementation
    // -------------------------------------------------------------------------

    /**
     * Called by ViewportPanel.onDidChangeViewState when the VSCode tab's
     * visibility changes. Issues the appropriate IPC setActive call.
     */
    onPanelVisibilityChanged(panel: ViewportPanel, visible: boolean): void {
        const id = panel.viewportId;
        if (id === VIEWPORT_ID_INVALID) { return; }

        if (visible) {
            void this._setActive(id, true);
        } else {
            void this._setActive(id, false);
        }
    }

    /**
     * Called by ViewportPanel.onDidDispose when the user closes the tab.
     * Issues `viewport.destroy` IPC and removes the panel from the registry.
     */
    onPanelDisposed(panel: ViewportPanel): void {
        const id = panel.viewportId;
        if (id !== VIEWPORT_ID_INVALID) {
            this._panels.delete(id);
            // Fire-and-forget: destroy the viewport on the engine side.
            // Skip if the manager itself is disposing — the engine watchdog handles cleanup.
            if (!this._disposed) {
                void this._destroyViewport(id);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Dispose
    // -------------------------------------------------------------------------

    /**
     * Disposes all panels and the EngineClient. Called on extension deactivation.
     * Does NOT send viewport.destroy IPC for each panel — the engine's heartbeat
     * watchdog handles cleanup when the extension host disconnects.
     */
    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;

        // Snapshot the panels before iterating — panel.dispose() fires the
        // onDidDispose callback which calls onPanelDisposed() → _panels.delete(),
        // which would mutate the map mid-iteration without a snapshot.
        const panelsToDispose = Array.from(this._panels.values());
        this._panels.clear();
        for (const panel of panelsToDispose) {
            panel.dispose();
        }

        for (const d of this._disposables) {
            d.dispose();
        }
        this._disposables.length = 0;

        ViewportManager._instance = undefined;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /** Issues viewport.setActive IPC and updates the panel state accordingly. */
    private async _setActive(id: ViewportId, active: boolean): Promise<void> {
        if (this._disposed) { return; }
        const panel = this._panels.get(id);
        if (!panel || panel.isDisposed) { return; }

        try {
            await this._engineClient.call('viewport.setActive', {
                viewport_id: id,
                active,
            });

            if (active) {
                panel.markActive();
            } else {
                panel.markSuspended();
            }

            this._log(`Viewport ${id} ${active ? 'activated' : 'suspended'}`);
        } catch (err) {
            this._log(
                `viewport.setActive(${id}, ${String(active)}) failed: ` +
                (err instanceof Error ? err.message : String(err))
            );
            // Swallow — engine may not be running; panel retains its current state.
        }
    }

    /** Issues viewport.destroy IPC. Fire-and-forget; errors are swallowed. */
    private async _destroyViewport(id: ViewportId): Promise<void> {
        try {
            await this._engineClient.call('viewport.destroy', { viewport_id: id });
            this._log(`Viewport destroyed: id=${id}`);
        } catch (err) {
            this._log(
                `viewport.destroy(${id}) failed: ` +
                (err instanceof Error ? err.message : String(err))
            );
        }
    }

    private _log(message: string): void {
        this._outputChannel.appendLine(`[ViewportManager] ${message}`);
    }
}
