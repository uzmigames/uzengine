import * as vscode from 'vscode';
import * as path from 'path';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const addon = require(path.join(__dirname, '..', 'native', 'uzengine.node'));

export function activate(context: vscode.ExtensionContext) {
    const version = addon.getVersion();
    const backend = addon.getBackend();
    console.log(`[UzEngine] ${backend} v${version}`);

    context.subscriptions.push(
        vscode.commands.registerCommand('uzengine.start', () => {
            addon.init();
            vscode.window.showInformationMessage(`UzEngine ${version} initialized`);
        })
    );
}

export function deactivate() {
    addon.shutdown();
}
