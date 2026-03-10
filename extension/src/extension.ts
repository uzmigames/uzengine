import * as vscode from 'vscode';
import * as path from 'path';

// eslint-disable-next-line @typescript-eslint/no-require-imports
const addon = require(path.join(__dirname, '..', 'native', 'uzengine.node'));

let log: vscode.LogOutputChannel;

export function activate(context: vscode.ExtensionContext) {
  log = vscode.window.createOutputChannel('UzEngine', { log: true });
  context.subscriptions.push(log);

  const version = addon.getVersion();
  const backend = addon.getBackend();
  log.info(`${backend} v${version}`);

  context.subscriptions.push(
    vscode.commands.registerCommand('uzengine.start', () => {
      addon.init();
      log.info(`Initialized v${version}`);
      vscode.window.showInformationMessage(`UzEngine ${version} initialized`);
    }),
  );
}

export function deactivate() {
  addon.shutdown();
}
