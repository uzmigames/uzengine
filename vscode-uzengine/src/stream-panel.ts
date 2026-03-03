/**
 * stream-panel.ts — Legacy single-viewport scene stream panel.
 *
 * This module preserves the original `StreamPanel.createOrShow()` API that was
 * wired to the `uzengine.openStreamViewer` command. Internally it now delegates
 * to `ViewportManager.createViewport()` so the scene viewport participates in
 * the full viewport lifecycle (suspend/resume, IPC, multi-viewport routing).
 *
 * Migration note (see docs/specs/editor/viewport-manager.md §Migration):
 *   Phase 1 — wrap existing behaviour. The legacy single viewport becomes
 *   viewport_id=1 type='scene'. All existing JPEG streaming behaviour is
 *   preserved unchanged through the ViewportPanel → WebSocket pipeline.
 *
 * Callers that previously used `StreamPanel` directly can continue to do so;
 * the static `createOrShow` method still works identically from the outside.
 */

import * as vscode from 'vscode';
import { ViewportManager } from './viewport-manager';
import { ViewportPanel } from './viewport-panel';

/**
 * Thin façade over ViewportManager for the legacy "open stream viewer" command.
 *
 * Maintains a singleton reference to the scene ViewportPanel so that calling
 * `createOrShow` multiple times reveals the existing tab rather than opening
 * a duplicate, exactly as the original implementation did.
 */
export class StreamPanel {
    /**
     * The currently active scene viewport panel, or undefined if none is open.
     * Typed as the underlying ViewportPanel — callers that held a StreamPanel
     * reference can use this to access panel methods directly.
     */
    public static currentPanel: ViewportPanel | undefined;

    // -------------------------------------------------------------------------
    // Static factory (public API — backward-compatible with original)
    // -------------------------------------------------------------------------

    /**
     * Creates a new scene viewport panel or reveals the existing one.
     *
     * Behaviour is identical to the original StreamPanel.createOrShow():
     *  - If a panel is already open, it is revealed in its current column.
     *  - If no panel is open, a new viewport is created via ViewportManager.
     *
     * @param extensionUri  Extension install URI (retained for API compatibility).
     * @param context       Extension context — required for panel registration.
     */
    public static async createOrShow(
        _extensionUri: vscode.Uri,
        context: vscode.ExtensionContext
    ): Promise<void> {

        // Reveal existing panel.
        if (StreamPanel.currentPanel && !StreamPanel.currentPanel.isDisposed) {
            const column =
                vscode.window.activeTextEditor?.viewColumn ?? vscode.ViewColumn.One;
            StreamPanel.currentPanel.reveal(column);
            return;
        }

        // Create a new scene viewport via the manager.
        let manager: ViewportManager;
        try {
            manager = ViewportManager.getInstance();
        } catch {
            vscode.window.showErrorMessage(
                'UzEngine: ViewportManager is not initialised. ' +
                'Make sure the extension activated correctly.'
            );
            return;
        }

        const config = vscode.workspace.getConfiguration('uzengine');
        const targetFps   = config.get<number>('viewport.sceneTargetFps', 60);
        const jpegQuality = config.get<number>('viewport.sceneJpegQuality', 85);

        try {
            const panel = await manager.createViewport(
                {
                    type:         'scene',
                    title:        'UzEngine Scene',
                    width:        1280,
                    height:       720,
                    targetFps,
                    jpegQuality,
                    debugName:    'Scene Viewport',
                },
                context
            );

            StreamPanel.currentPanel = panel;

            // Clear the reference when the panel is disposed so createOrShow
            // creates a fresh one next time.
            const disposeSub = { dispose: () => { StreamPanel.currentPanel = undefined; } };
            context.subscriptions.push(disposeSub);

        } catch (err) {
            const message = err instanceof Error ? err.message : String(err);
            vscode.window.showErrorMessage(`UzEngine: Failed to open scene viewport — ${message}`);
        }
    }

    /**
     * @deprecated Use StreamPanel.currentPanel?.dispose() directly, or close
     * the tab. This method exists only for backward compatibility.
     */
    public static dispose(): void {
        StreamPanel.currentPanel?.dispose();
        StreamPanel.currentPanel = undefined;
    }
}
