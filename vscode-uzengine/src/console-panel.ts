/**
 * console-panel.ts — WebviewViewProvider for the UzEngine Console panel.
 *
 * Renders engine log output (info/warning/error) inside the sidebar Console
 * view (uzengine.console). Messages are pushed from the IpcClient via the
 * `engine.log` push event and from the play-mode process stdout/stderr.
 *
 * Log levels:
 *   info    — white text
 *   warning — yellow text  ($(warning) icon)
 *   error   — red text     ($(error) icon)
 *
 * The panel retains its HTML when hidden so scroll position is preserved.
 * A "Clear" toolbar button fires `uzengine.consoleClear`.
 */

import * as vscode from 'vscode';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Severity of a console log entry. */
export type LogLevel = 'info' | 'warning' | 'error';

/** A single console log entry. */
export interface LogEntry {
    readonly level: LogLevel;
    readonly message: string;
    readonly timestamp: Date;
}

// ---------------------------------------------------------------------------
// ConsolePanel
// ---------------------------------------------------------------------------

/**
 * Provides the `uzengine.console` sidebar webview view.
 *
 * Usage:
 * ```typescript
 * const panel = new ConsolePanel();
 * context.subscriptions.push(
 *   vscode.window.registerWebviewViewProvider('uzengine.console', panel)
 * );
 * panel.log('info', 'Engine started');
 * ```
 */
export class ConsolePanel implements vscode.WebviewViewProvider, vscode.Disposable {
    static readonly viewType = 'uzengine.console';

    /** Maximum number of log entries retained in memory. */
    static readonly MAX_ENTRIES = 500;

    private _view: vscode.WebviewView | undefined;
    private readonly _entries: LogEntry[] = [];
    private _disposed = false;

    // -------------------------------------------------------------------------
    // WebviewViewProvider implementation
    // -------------------------------------------------------------------------

    resolveWebviewView(
        webviewView: vscode.WebviewView,
        _context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken
    ): void {
        this._view = webviewView;

        webviewView.webview.options = {
            enableScripts: true,
        };

        webviewView.webview.html = this._buildHtml();

        // Push all buffered entries to the new webview.
        for (const entry of this._entries) {
            this._postEntry(entry);
        }
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Appends a log entry to the console.
     *
     * @param level    Severity level.
     * @param message  Text to display.
     */
    log(level: LogLevel, message: string): void {
        if (this._disposed) { return; }

        const entry: LogEntry = { level, message, timestamp: new Date() };

        // Evict oldest entry when over the limit.
        if (this._entries.length >= ConsolePanel.MAX_ENTRIES) {
            this._entries.shift();
        }
        this._entries.push(entry);

        this._postEntry(entry);
    }

    /** Clears all console entries. */
    clear(): void {
        if (this._disposed) { return; }
        this._entries.length = 0;
        this._view?.webview.postMessage({ type: 'clear' });
    }

    /** Returns a snapshot of all current log entries. */
    get entries(): readonly LogEntry[] {
        return this._entries;
    }

    dispose(): void {
        this._disposed = true;
        this._entries.length = 0;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private _postEntry(entry: LogEntry): void {
        if (!this._view) { return; }
        this._view.webview.postMessage({
            type: 'log',
            level: entry.level,
            message: entry.message,
            timestamp: entry.timestamp.toLocaleTimeString(),
        });
    }

    private _buildHtml(): string {
        return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';">
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: var(--vscode-editor-font-family, monospace);
    font-size: var(--vscode-editor-font-size, 12px);
    background: var(--vscode-panel-background, #1e1e1e);
    color: var(--vscode-foreground, #cccccc);
    height: 100vh;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
  }
  #log {
    flex: 1;
    padding: 4px 6px;
    overflow-y: auto;
  }
  .entry {
    display: flex;
    gap: 6px;
    padding: 1px 0;
    border-bottom: 1px solid var(--vscode-panel-border, #333);
    white-space: pre-wrap;
    word-break: break-all;
    line-height: 1.4;
  }
  .ts {
    color: var(--vscode-descriptionForeground, #888);
    flex-shrink: 0;
    user-select: none;
  }
  .icon { flex-shrink: 0; }
  .msg { flex: 1; }
  .info  .icon { color: var(--vscode-foreground, #ccc); }
  .warning .icon { color: var(--vscode-editorWarning-foreground, #cca700); }
  .warning .msg  { color: var(--vscode-editorWarning-foreground, #cca700); }
  .error .icon   { color: var(--vscode-editorError-foreground, #f44747); }
  .error .msg    { color: var(--vscode-editorError-foreground, #f44747); }
  #empty {
    color: var(--vscode-descriptionForeground, #888);
    padding: 12px 8px;
    font-style: italic;
  }
</style>
</head>
<body>
<div id="log">
  <div id="empty">No output yet.</div>
</div>
<script>
  const log = document.getElementById('log');
  const empty = document.getElementById('empty');

  const ICONS = { info: '●', warning: '⚠', error: '✖' };

  window.addEventListener('message', e => {
    const msg = e.data;
    if (msg.type === 'clear') {
      log.innerHTML = '';
      log.appendChild(empty);
      empty.style.display = '';
      return;
    }
    if (msg.type !== 'log') { return; }

    if (empty.parentNode) { empty.style.display = 'none'; }

    const row = document.createElement('div');
    row.className = 'entry ' + msg.level;

    const ts = document.createElement('span');
    ts.className = 'ts';
    ts.textContent = msg.timestamp;

    const icon = document.createElement('span');
    icon.className = 'icon';
    icon.textContent = ICONS[msg.level] || '●';

    const text = document.createElement('span');
    text.className = 'msg';
    text.textContent = msg.message;

    row.appendChild(ts);
    row.appendChild(icon);
    row.appendChild(text);
    log.appendChild(row);

    // Auto-scroll to bottom.
    log.scrollTop = log.scrollHeight;
  });
</script>
</body>
</html>`;
    }
}
