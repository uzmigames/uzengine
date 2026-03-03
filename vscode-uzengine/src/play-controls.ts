/**
 * play-controls.ts — Status bar Play / Pause / Stop controls for UzEngine.
 *
 * Creates three VSCode status bar items that control the engine's play mode:
 *
 *  ▶ Play  — sends `playmode.start` IPC (process spawning is phase1-play-mode)
 *  ⏸ Pause — sends `engine.pause` IPC
 *  ■ Stop  — sends `playmode.stop` IPC
 *
 * The control shows the current scene name and live FPS alongside the buttons.
 *
 * States:
 *  - idle    → only ▶ Play visible
 *  - playing → ⏸ Pause and ■ Stop visible; ▶ grayed
 *  - paused  → ▶ Resume and ■ Stop visible; ⏸ grayed
 */

import * as vscode from 'vscode';
import { IpcClient } from './ipc-client';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Play mode state machine values. */
export type PlayState = 'idle' | 'playing' | 'paused';

// ---------------------------------------------------------------------------
// PlayControls
// ---------------------------------------------------------------------------

/**
 * Manages the three status bar items that drive the play mode lifecycle.
 *
 * Call `initialize()` during extension activation to create the items, then
 * `dispose()` on deactivation.
 */
export class PlayControls implements vscode.Disposable {
    /** Current play state. */
    private _state: PlayState = 'idle';

    /** Name of the active scene (for status bar display). */
    private _sceneName: string = '';

    /** Latest FPS reading (for status bar display). */
    private _fps: number = 0;

    /** Status bar: ▶ Play button. */
    private readonly _playItem: vscode.StatusBarItem;

    /** Status bar: ⏸ Pause button. */
    private readonly _pauseItem: vscode.StatusBarItem;

    /** Status bar: ■ Stop button. */
    private readonly _stopItem: vscode.StatusBarItem;

    /** Status bar: scene name + FPS info segment. */
    private readonly _infoItem: vscode.StatusBarItem;

    /** Whether this object has been disposed. */
    private _disposed = false;

    constructor(private readonly _ipc: IpcClient) {
        // Play — leftmost, highest priority.
        this._playItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            100
        );
        this._playItem.command = 'uzengine.play';

        // Pause — second.
        this._pauseItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            99
        );
        this._pauseItem.command = 'uzengine.pause';

        // Stop — third.
        this._stopItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            98
        );
        this._stopItem.command = 'uzengine.stop';

        // Info — fourth (scene name + FPS).
        this._infoItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            97
        );

        this._applyState();
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /** Current play state. */
    get state(): PlayState {
        return this._state;
    }

    /**
     * Updates the displayed scene name.
     *
     * @param name  Scene name string (e.g. `"main.uzscene"`).
     */
    setSceneName(name: string): void {
        this._sceneName = name;
        this._applyState();
    }

    /**
     * Updates the displayed FPS value.
     *
     * @param fps  Frames per second (e.g. `60.0`).
     */
    setFps(fps: number): void {
        this._fps = fps;
        this._applyState();
    }

    /**
     * Transitions to the `playing` state and sends `playmode.start` IPC.
     */
    async play(): Promise<void> {
        if (this._disposed) { return; }
        if (this._state === 'playing') { return; }

        try {
            await this._ipc.request('playmode.start', {});
            this._state = 'playing';
            this._applyState();
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to start play mode — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Transitions to the `paused` state and sends `engine.pause` IPC.
     */
    async pause(): Promise<void> {
        if (this._disposed) { return; }
        if (this._state !== 'playing') { return; }

        try {
            await this._ipc.request('engine.pause', {});
            this._state = 'paused';
            this._applyState();
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to pause — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    /**
     * Transitions to the `idle` state and sends `playmode.stop` IPC.
     */
    async stop(): Promise<void> {
        if (this._disposed) { return; }
        if (this._state === 'idle') { return; }

        try {
            await this._ipc.request('playmode.stop', {});
        } catch {
            // Best-effort — still transition to idle on the UI side.
        }

        this._state = 'idle';
        this._fps = 0;
        this._applyState();
    }

    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;

        this._playItem.dispose();
        this._pauseItem.dispose();
        this._stopItem.dispose();
        this._infoItem.dispose();
    }

    // -------------------------------------------------------------------------
    // Private — state application
    // -------------------------------------------------------------------------

    /**
     * Updates all status bar items to reflect the current `_state`.
     */
    private _applyState(): void {
        if (this._disposed) { return; }

        switch (this._state) {
            case 'idle':
                this._configurePlay('$(play) Play', 'Start Play Mode', false);
                this._pauseItem.hide();
                this._stopItem.hide();
                this._infoItem.hide();
                break;

            case 'playing':
                this._configurePlay('$(play)', 'Play Mode Active', true);
                this._configurePause('$(debug-pause) Pause', 'Pause Play Mode', false);
                this._configureStop('$(debug-stop) Stop', 'Stop Play Mode');
                this._updateInfo();
                this._pauseItem.show();
                this._stopItem.show();
                this._infoItem.show();
                break;

            case 'paused':
                this._configurePlay('$(play) Resume', 'Resume Play Mode', false);
                this._configurePause('$(debug-pause)', 'Paused', true);
                this._configureStop('$(debug-stop) Stop', 'Stop Play Mode');
                this._updateInfo();
                this._pauseItem.show();
                this._stopItem.show();
                this._infoItem.show();
                break;
        }

        // Play item is always visible.
        this._playItem.show();
    }

    private _configurePlay(text: string, tooltip: string, dimmed: boolean): void {
        this._playItem.text = text;
        this._playItem.tooltip = tooltip;
        this._playItem.backgroundColor = dimmed
            ? new vscode.ThemeColor('statusBarItem.warningBackground')
            : undefined;
    }

    private _configurePause(text: string, tooltip: string, dimmed: boolean): void {
        this._pauseItem.text = text;
        this._pauseItem.tooltip = tooltip;
        this._pauseItem.backgroundColor = dimmed
            ? new vscode.ThemeColor('statusBarItem.warningBackground')
            : undefined;
    }

    private _configureStop(text: string, tooltip: string): void {
        this._stopItem.text = text;
        this._stopItem.tooltip = tooltip;
        this._stopItem.backgroundColor = undefined;
    }

    private _updateInfo(): void {
        const parts: string[] = [];
        if (this._sceneName) {
            parts.push(this._sceneName);
        }
        if (this._fps > 0) {
            parts.push(`${this._fps.toFixed(0)} FPS`);
        }
        this._infoItem.text = parts.length > 0 ? `| ${parts.join('  ')}` : '| UzEngine';
        this._infoItem.tooltip = 'UzEngine Play Mode Info';
    }
}
