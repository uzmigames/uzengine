/// UzEngine VSCode extension entry point.
///
/// This module initializes the UzEngine runtime addon and registers VSCode commands.
/// It manages the lifecycle of the native addon, including initialization and shutdown.

import * as vscode from 'vscode';
import * as path from 'path';

// eslint-disable-next-line @typescript-eslint/no-require-imports
const addon = require(path.join(__dirname, '..', 'native', 'uzengine.node'));

let log: vscode.LogOutputChannel;

/// Activates the UzEngine extension.
///
/// # Arguments
/// * `context` - The extension context provided by VSCode, used to manage subscriptions.
///
/// # Behavior
/// - Creates a log output channel for UzEngine
/// - Loads the native addon and displays version/backend info
/// - Registers the 'uzengine.start' command to initialize the runtime
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

/// Deactivates the UzEngine extension.
///
/// # Behavior
/// - Cleanly shuts down the native addon
/// - Releases all GPU and system resources
export function deactivate() {
  addon.shutdown();
}
