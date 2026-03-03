/**
 * uzscene-editor.ts — CustomTextEditorProvider for .uzscene files.
 *
 * Opening a .uzscene file shows the live DX12 render stream directly inside
 * the editor tab (full-page canvas, 60 fps JPEG over WebSocket).
 *
 * Responsibilities:
 *  1. Starts UzEngine.exe (via ServerManager) if not already running.
 *  2. Replaces the loading page with the JPEG streaming canvas once the
 *     engine is up, using viewport_id=0 (legacy raw-JPEG mode — no 4-byte
 *     prefix on the wire).
 *  3. Updates the tab title from the scene name in the .uzscene file.
 *
 * The Scene Tree is shown in the "SCENE" side-panel (SceneTreeProvider),
 * not inside this editor tab.
 */

import * as vscode from 'vscode';
import { randomBytes } from 'crypto';
import { parseUzScene } from './uzscene-parser';
import { ServerManager } from './server-manager';
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
 * Registration in extension.ts:
 * ```ts
 * vscode.window.registerCustomEditorProvider(
 *   UzSceneEditorProvider.viewType,
 *   new UzSceneEditorProvider(serverManager, context),
 *   { webviewOptions: { retainContextWhenHidden: true } }
 * )
 * ```
 */
export class UzSceneEditorProvider implements vscode.CustomTextEditorProvider {

    /** View type string that must match package.json `customEditors[].viewType`. */
    public static readonly viewType = 'uzengine.uzsceneEditor';

    constructor(
        private readonly _serverManager: ServerManager,
        _context: vscode.ExtensionContext,
    ) {}

    // -------------------------------------------------------------------------
    // CustomTextEditorProvider implementation
    // -------------------------------------------------------------------------

    /**
     * Called by VSCode when a .uzscene file is opened.
     * 1. Shows a loading splash immediately.
     * 2. Starts UzEngine.exe.
     * 3. Swaps the webview to the live JPEG viewport canvas.
     * 4. Updates the tab title from the scene name.
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

        // Show the viewport canvas immediately — the webview JS has a WebSocket
        // reconnect loop, so it connects as soon as the engine is up on ws://localhost:<port>.
        const config = vscode.workspace.getConfiguration('uzengine');
        const wsPort = config.get<number>('server.wsPort', 9002);
        const nonce  = randomBytes(16).toString('base64');

        webviewPanel.webview.html = buildViewportStreamHtml({
            nonce,
            title:      document.fileName.split(/[\\/]/).pop() || 'UzScene',
            wsPort,
            viewportId: 1, // default scene viewport (engine prefixes frames with this ID)
        });

        // Fire-and-forget: try to start engine if it's not already running.
        // If it fails (e.g. exe not found), the canvas stays black — no crash.
        this._serverManager.start().catch(() => { /* best-effort */ });
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
