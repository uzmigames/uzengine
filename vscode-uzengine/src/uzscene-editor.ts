/**
 * uzscene-editor.ts — CustomTextEditorProvider for .uzscene files.
 *
 * Opening a .uzscene file shows the live DX12 render stream directly inside
 * the editor tab (full-page canvas, 60 fps JPEG over WebSocket).
 *
 * Responsibilities:
 *  1. Spawns an independent UzEngine.exe instance (via EngineInstanceManager)
 *     for this scene file, with a unique WebSocket port and IPC directory.
 *  2. Replaces the loading page with the JPEG streaming canvas once the
 *     engine is up.
 *  3. Updates the tab title from the scene name in the .uzscene file.
 *  4. Disposes the engine instance when the editor tab is closed.
 *
 * The Scene Tree is shown in the "SCENE" side-panel (SceneTreeProvider),
 * not inside this editor tab.
 */

import { randomBytes } from 'crypto';
import * as vscode from 'vscode';
import { EngineInstanceManager } from './engine-instance-manager';
import { parseUzScene } from './uzscene-parser';
import { buildViewportStreamHtml } from './viewport-panel';

// ---------------------------------------------------------------------------
// Loading HTML
// ---------------------------------------------------------------------------

/** CSP for the loading page — no scripts needed. */
const LOADING_CSP = `default-src 'none'; style-src 'unsafe-inline';`;

function escapeHtml(str: string): string {
    return str
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

/**
 * Builds a "Starting UzEngine..." placeholder page shown immediately while
 * the engine process starts up.
 */
export function buildLoadingHtml(fileName: string): string {
    const name = escapeHtml(fileName.split(/[\\/]/).pop() || fileName);
    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta http-equiv="Content-Security-Policy" content="${LOADING_CSP}"/>
<title>UzScene Editor</title>
<style>
  :root {
    font-family: var(--vscode-font-family, 'Segoe UI', sans-serif);
    font-size: var(--vscode-font-size, 13px);
    color: var(--vscode-foreground, #cccccc);
    background: var(--vscode-editor-background, #1e1e1e);
  }
  body { margin: 0; padding: 12px 16px; }
  h1 {
    display: flex; align-items: center; gap: 8px;
    font-size: 1.1em; font-weight: 600;
    color: var(--vscode-titleBar-activeForeground, #cccccc);
    margin: 0 0 12px 0;
    border-bottom: 1px solid var(--vscode-panel-border, #333);
    padding-bottom: 6px;
  }
  .scene-icon { color: #4FC3F7; font-size: 14px; }
  .loading-msg {
    margin-top: 32px;
    color: var(--vscode-descriptionForeground, #9d9d9d);
    font-style: italic;
  }
  .spinner {
    display: inline-block;
    animation: spin 1.2s linear infinite;
    margin-right: 6px;
  }
  @keyframes spin { to { transform: rotate(360deg); } }
</style>
</head>
<body>
<h1><span class="scene-icon">&#x2B21;</span>${name}</h1>
<p class="loading-msg"><span class="spinner">&#x27F3;</span> Starting UzEngine&hellip;</p>
</body>
</html>`;
}

// ---------------------------------------------------------------------------
// UzSceneEditorProvider
// ---------------------------------------------------------------------------

/**
 * CustomTextEditorProvider that opens .uzscene files as a live 3D render.
 *
 * Each .uzscene tab spawns its own independent UzEngine.exe instance via
 * EngineInstanceManager, ensuring fully isolated rendering per scene.
 *
 * Registration in extension.ts:
 * ```ts
 * vscode.window.registerCustomEditorProvider(
 *   UzSceneEditorProvider.viewType,
 *   new UzSceneEditorProvider(context),
 *   { webviewOptions: { retainContextWhenHidden: true } }
 * )
 * ```
 */
export class UzSceneEditorProvider implements vscode.CustomTextEditorProvider {

    /** View type string that must match package.json `customEditors[].viewType`. */
    public static readonly viewType = 'uzengine.uzsceneEditor';

    /** Tracks all active webview panels for postMessage forwarding. */
    private static readonly _activePanels = new Set<vscode.WebviewPanel>();

    /**
     * Forward a message to all visible uzscene editor webviews.
     * Used by extension.ts gizmo commands to sync the toolbar state.
     */
    static postToActiveWebview(message: unknown): void {
        for (const panel of UzSceneEditorProvider._activePanels) {
            if (panel.visible) {
                void panel.webview.postMessage(message);
            }
        }
    }

    /**
     * Static callback invoked when the engine returns a pick result from the viewport.
     * Set this from extension.ts to wire up to the SceneTreeProvider.selectNodeById().
     */
    static onPickResult: ((nodeId: number | null) => void) | null = null;

    constructor(
        _context: vscode.ExtensionContext,
    ) { }

    // -------------------------------------------------------------------------
    // CustomTextEditorProvider implementation
    // -------------------------------------------------------------------------

    /**
     * Called by VSCode when a .uzscene file is opened.
     * 1. Spawns an independent UzEngine.exe instance for this scene.
     * 2. Shows the live JPEG viewport canvas connected to the instance's port.
     * 3. Updates the tab title from the scene name.
     * 4. Cleans up the instance when the tab is closed.
     */
    async resolveCustomTextEditor(
        document: vscode.TextDocument,
        webviewPanel: vscode.WebviewPanel,
        _token: vscode.CancellationToken,
    ): Promise<void> {
        // Enable scripts so the WebSocket + canvas JS can run inside the webview.
        webviewPanel.webview.options = { enableScripts: true };

        // Update title from scene name (best-effort, non-blocking).
        this._updateTitle(document, webviewPanel);

        // Listen for messages from the webview (e.g. pickResult from engine).
        webviewPanel.webview.onDidReceiveMessage((message: { type: string; nodeId?: number | null }) => {
            if (message.type === 'pickResult' && UzSceneEditorProvider.onPickResult) {
                UzSceneEditorProvider.onPickResult(message.nodeId ?? null);
            }
        });

        // Get or create an independent engine instance for this scene.
        let instanceMgr: EngineInstanceManager;
        try {
            instanceMgr = EngineInstanceManager.getInstance();
        } catch {
            vscode.window.showErrorMessage(
                'UzEngine: EngineInstanceManager is not initialised. ' +
                'Make sure the extension activated correctly.'
            );
            return;
        }

        try {
            const instance = await instanceMgr.createInstance(document.uri.fsPath);

            if (!instance.isRunning) {
                const sceneName = document.fileName.split(/[\\/]/).pop() || 'scene';
                vscode.window.showWarningMessage(
                    `UzEngine: Engine for "${sceneName}" failed to start ` +
                    `(port ${instance.wsPort}). Check Output → UzEngine for details.`
                );
            }

            const wsPort = instance.wsPort;
            const nonce = randomBytes(16).toString('base64');

            webviewPanel.webview.html = buildViewportStreamHtml({
                nonce,
                title: document.fileName.split(/[\\/]/).pop() || 'UzScene',
                wsPort,
                viewportId: 1,
            });

            // Set this as the active instance when the tab gains focus.
            webviewPanel.onDidChangeViewState(() => {
                if (webviewPanel.visible) {
                    instanceMgr.setActiveInstance(instance.id);
                }
            });

            // Destroy the engine instance when the tab is closed.
            webviewPanel.onDidDispose(() => {
                UzSceneEditorProvider._activePanels.delete(webviewPanel);
                instanceMgr.destroyInstance(instance.id);
            });

            // Track this panel for postMessage forwarding.
            UzSceneEditorProvider._activePanels.add(webviewPanel);

        } catch (err) {
            const message = err instanceof Error ? err.message : String(err);
            vscode.window.showErrorMessage(`UzEngine: Failed to open scene — ${message}`);
        }
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private _updateTitle(document: vscode.TextDocument, webviewPanel: vscode.WebviewPanel): void {
        try {
            const parsed = parseUzScene(document.getText());
            webviewPanel.title = parsed.sceneName
                ? `${parsed.sceneName} — UzScene`
                : 'UzScene Editor';
        } catch {
            // Non-fatal — leave the default title.
        }
    }

    dispose(): void {
        // No provider-level resources.
    }
}
