/**
 * extension.ts — VSCode extension entry point.
 *
 * Activation responsibilities:
 *  1. Start the ServerManager (manages UzEngine.exe child process).
 *  2. Initialise the ViewportManager singleton (manages all open viewport tabs).
 *  3. Subscribe to `vscode.window.onDidChangeWindowState` so all viewports are
 *     suspended when the developer switches to another application.
 *  4. Register the three legacy commands:
 *       - uzengine.openStreamViewer  (start server + open scene viewport)
 *       - uzengine.startServer       (start server only)
 *       - uzengine.stopServer        (kill the server process)
 *
 * All registered disposables go into `context.subscriptions` so VSCode disposes
 * them automatically on deactivation — no explicit cleanup needed in `deactivate()`.
 */

import * as vscode from 'vscode';
import { StreamPanel } from './stream-panel';
import { ServerManager } from './server-manager';
import { ViewportManager } from './viewport-manager';

let serverManager: ServerManager | undefined;

/**
 * Called by VSCode when the extension is activated.
 * Registers commands, creates the ServerManager, and initialises the
 * ViewportManager singleton.
 */
export function activate(context: vscode.ExtensionContext): void {
    // -------------------------------------------------------------------------
    // 1. Shared output channel (used by ServerManager + ViewportManager + EngineClient)
    // -------------------------------------------------------------------------
    const outputChannel = vscode.window.createOutputChannel('UzEngine');
    context.subscriptions.push(outputChannel);

    // -------------------------------------------------------------------------
    // 2. Server manager — manages UzEngine.exe child process lifecycle
    // -------------------------------------------------------------------------
    serverManager = new ServerManager();
    context.subscriptions.push(serverManager);

    // -------------------------------------------------------------------------
    // 3. ViewportManager singleton — manages all open viewport panels.
    //    The manager subscribes to onDidChangeWindowState internally to
    //    suspend all viewports when the VSCode window loses focus.
    // -------------------------------------------------------------------------
    ViewportManager.initialize(context, outputChannel);
    // ViewportManager is already pushed to context.subscriptions inside initialize().

    // -------------------------------------------------------------------------
    // 4. Commands
    // -------------------------------------------------------------------------

    // uzengine.openStreamViewer — start server + open scene viewport
    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        async () => {
            const started = await serverManager!.start();
            if (started) {
                await StreamPanel.createOrShow(context.extensionUri, context);
            }
        }
    );

    // uzengine.startServer — start server without opening a panel
    const startCommand = vscode.commands.registerCommand(
        'uzengine.startServer',
        async () => {
            await serverManager!.start();
        }
    );

    // uzengine.stopServer — kill the server process
    const stopCommand = vscode.commands.registerCommand(
        'uzengine.stopServer',
        () => {
            serverManager!.stop();
        }
    );

    context.subscriptions.push(openCommand, startCommand, stopCommand);
}

/**
 * Called by VSCode when the extension is deactivated.
 * All cleanup is handled through `context.subscriptions` — no explicit work
 * is needed here. The ViewportManager.dispose() call triggered by the
 * subscriptions array closes all open panels, and the engine's heartbeat
 * watchdog auto-suspends viewports when IPC goes silent.
 */
export function deactivate(): void {
    // Intentionally empty — context.subscriptions handles all cleanup.
}
