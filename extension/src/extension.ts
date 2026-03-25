/// UzEngine VSCode extension entry point.
///
/// Activates the native addon, wires up all panels (viewport, scene tree,
/// console), registers commands and keybindings, and manages clean shutdown.
///
/// Panel ownership model:
///  - `EngineConsole` — permanent; lives for the full extension lifetime.
///  - `SceneTreeProvider` — permanent; drives the "Scene Hierarchy" tree view.
///  - `ViewportPanel` — created on demand; at most one instance at a time.
///    Set to `undefined` after the panel is closed so a new one can be opened.
///  - `EngineMode` — permanent; drives the status bar toggle.

import * as vscode from 'vscode';
import * as path from 'path';
import type { UzEngineAddon } from './uzengine';
import { ViewportPanel } from './panels/viewport-panel';
import { SceneTreeProvider } from './panels/scene-tree-panel';
import { EngineConsole } from './panels/console-panel';
import { EngineMode } from './engine-mode';

// ── Native addon ──────────────────────────────────────────────────────────────

// The .node binary is built separately and placed next to package.json.
// Using require() because N-API addons cannot be imported via ESM.
// eslint-disable-next-line @typescript-eslint/no-require-imports
const addon: UzEngineAddon = require(
    path.join(__dirname, '..', 'native', 'uzengine.node')
) as UzEngineAddon;

// ── Module-level state ────────────────────────────────────────────────────────

let log: vscode.LogOutputChannel;
let engineConsole: EngineConsole;
let sceneTree: SceneTreeProvider;
let viewport: ViewportPanel | undefined;
let engineMode: EngineMode;

// ── Activation ────────────────────────────────────────────────────────────────

/// Activates the UzEngine extension.
///
/// Called by VSCode when one of the extension's activation events fires
/// (e.g. the user opens a `.uzscene` file or runs an `uzengine.*` command).
///
/// # Arguments
/// * `context` - Extension context used to register disposables and persist state.
export function activate(context: vscode.ExtensionContext): void {
    // ── Logging channel ───────────────────────────────────────────────────────
    log = vscode.window.createOutputChannel('UzEngine', { log: true });
    context.subscriptions.push(log);

    const version = addon.getVersion();
    const backend = addon.getBackend();
    log.info(`UzEngine v${version} (${backend})`);

    // ── Console panel — runtime log stream ────────────────────────────────────
    engineConsole = new EngineConsole(addon);
    context.subscriptions.push(engineConsole);

    // ── Scene tree provider ───────────────────────────────────────────────────
    sceneTree = new SceneTreeProvider(addon);
    const treeView = vscode.window.createTreeView('uzengine.sceneTree', {
        treeDataProvider: sceneTree,
        showCollapseAll: true,
    });
    context.subscriptions.push(treeView);

    // ── Engine mode status bar toggle ─────────────────────────────────────────
    engineMode = new EngineMode(context);

    // ── Commands ──────────────────────────────────────────────────────────────
    context.subscriptions.push(

        // uzengine.start — initialise the runtime (useful for testing the addon
        // without opening the full viewport).
        vscode.commands.registerCommand('uzengine.start', () => {
            try {
                addon.init();
                log.info('Runtime initialized');
                vscode.window.showInformationMessage(
                    `UzEngine ${version} initialized`
                );
            } catch (e: unknown) {
                const msg = e instanceof Error ? e.message : String(e);
                log.error(`Runtime init failed: ${msg}`);
                vscode.window.showErrorMessage(`UzEngine init failed: ${msg}`);
            }
        }),

        // uzengine.openViewport — create the viewport panel (or reveal if already open).
        vscode.commands.registerCommand('uzengine.openViewport', () => {
            if (viewport) {
                viewport.reveal();
                return;
            }
            viewport = new ViewportPanel(context.extensionUri, addon, sceneTree);
            viewport.onDidDispose(() => {
                viewport = undefined;
            });
        }),

        // uzengine.openScene — show file picker, load .uzscene, refresh tree.
        vscode.commands.registerCommand(
            'uzengine.openScene',
            async (uri?: vscode.Uri) => {
                if (!uri) {
                    const files = await vscode.window.showOpenDialog({
                        canSelectMany: false,
                        filters: { 'UzEngine Scene': ['uzscene'] },
                        title: 'Open UzEngine Scene',
                    });
                    if (!files || files.length === 0) return;
                    uri = files[0];
                }

                // Ensure viewport is open so the scene has somewhere to render.
                if (!viewport) {
                    viewport = new ViewportPanel(
                        context.extensionUri, addon, sceneTree
                    );
                    viewport.onDidDispose(() => {
                        viewport = undefined;
                    });
                } else {
                    viewport.reveal();
                }

                try {
                    addon.loadScene(uri.fsPath);
                    sceneTree.refresh();
                    log.info(`Scene loaded: ${uri.fsPath}`);
                } catch (e: unknown) {
                    const msg = e instanceof Error ? e.message : String(e);
                    log.error(`Failed to load scene: ${msg}`);
                    vscode.window.showErrorMessage(
                        `Failed to load scene: ${msg}`
                    );
                }
            }
        ),

        // uzengine.toggleEngineMode — switch between Code Mode and Engine Mode.
        vscode.commands.registerCommand('uzengine.toggleEngineMode', () => {
            engineMode.toggle();
        }),

        // uzengine.refreshSceneTree — force re-query of the entity hierarchy.
        vscode.commands.registerCommand('uzengine.refreshSceneTree', () => {
            sceneTree.refresh();
        })
    );

    log.info('UzEngine extension activated');
}

// ── Deactivation ──────────────────────────────────────────────────────────────

/// Deactivates the UzEngine extension.
///
/// Called by VSCode before the extension host process exits or when the
/// extension is explicitly disabled. Resources must be released synchronously
/// because the host may exit immediately after this function returns.
export function deactivate(): void {
    if (viewport) {
        viewport.dispose();
        viewport = undefined;
    }
    // Unregister the log callback before the addon shuts down.
    try { addon.setLogCallback(null); } catch { /* ignore */ }
    try { addon.destroyViewport(); } catch { /* ignore */ }
    try { addon.shutdown(); } catch { /* ignore */ }
}
