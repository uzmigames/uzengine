import * as vscode from 'vscode';
import { StreamPanel } from './stream-panel';
import { ServerManager } from './server-manager';

let serverManager: ServerManager | undefined;

/**
 * Called by VSCode when the extension is activated.
 * Registers commands and creates the server manager singleton.
 */
export function activate(context: vscode.ExtensionContext): void {
    serverManager = new ServerManager();
    context.subscriptions.push(serverManager);

    // uzengine.openStreamViewer — start server + open panel
    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        async () => {
            const started = await serverManager!.start();
            if (started) {
                StreamPanel.createOrShow(context.extensionUri);
            }
        }
    );

    // uzengine.startServer — start server without opening the panel
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
 * The ServerManager is in context.subscriptions so it disposes automatically.
 */
export function deactivate(): void {
    // ServerManager.dispose() kills the process via context.subscriptions
}
