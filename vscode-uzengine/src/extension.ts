import * as vscode from 'vscode';
import { StreamPanel } from './stream-panel';

/**
 * Called by VSCode when the extension is activated.
 * Registers the `uzengine.openStreamViewer` command and subscribes
 * the disposable so VSCode cleans it up on deactivation.
 */
export function activate(context: vscode.ExtensionContext): void {
    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        () => {
            StreamPanel.createOrShow(context.extensionUri);
        }
    );

    context.subscriptions.push(openCommand);
}

/**
 * Called by VSCode when the extension is deactivated.
 * StreamPanel disposes itself via the panel's onDidDispose event,
 * so no additional cleanup is required here.
 */
export function deactivate(): void {
    // Nothing to do — StreamPanel.currentPanel is disposed via onDidDispose.
}
