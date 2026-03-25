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
import type { UzEngineAddon } from './uzengine';
import { ViewportPanel } from './panels/viewport-panel';
import { SceneTreeProvider } from './panels/scene-tree-panel';
import { EngineConsole } from './panels/console-panel';
import { EngineMode } from './engine-mode';
import { SceneEditorProvider } from './panels/scene-editor-provider';

// ── Module-level state ────────────────────────────────────────────────────────

let log: vscode.LogOutputChannel;
let engineConsole: EngineConsole;
let sceneTree: SceneTreeProvider;
let viewport: ViewportPanel | undefined;
let engineMode: EngineMode;

// ── Native addon helpers ──────────────────────────────────────────────────────

/// Lazily loads the native .node addon. Called inside activate() — NOT at
/// module scope — because require() of a .node triggers DllMain and other
/// native init that can block the Extension Host event loop long enough to
/// trip the 10-s debugger timeout.
function loadNativeAddon(): { addon?: UzEngineAddon; error?: string } {
    try {
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const nodePath = require('path') as typeof import('path');
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const mod = require(
            nodePath.join(__dirname, '..', 'native', 'uzengine.node')
        ) as UzEngineAddon;
        return { addon: mod };
    } catch (e: unknown) {
        return { error: e instanceof Error ? e.message : String(e) };
    }
}

/// Cached addon reference — populated once during activate().
let _addon: UzEngineAddon | undefined;

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

    // ── Load native addon (lazy — not at module scope) ────────────────────────
    const result = loadNativeAddon();
    if (!result.addon) {
        const msg = `UzEngine native addon failed to load: ${result.error}`;
        log.error(msg);
        vscode.window.showErrorMessage(msg);
        return;
    }
    _addon = result.addon;
    const addon: UzEngineAddon = _addon;

    // Point the engine at the native/ directory so it finds dxcompiler.dll,
    // D3D12/ Agility SDK, etc. instead of looking in the Electron exe dir.
    const nodePath = require('path') as typeof import('path');
    const nativeDir = nodePath.join(__dirname, '..', 'native');
    addon.setEngineDir(nativeDir);

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

    // ── Scene editor — opens .uzscene files in the viewport ─────────────────
    context.subscriptions.push(
        vscode.window.registerCustomEditorProvider(
            SceneEditorProvider.viewType,
            new SceneEditorProvider(addon, sceneTree),
            { supportsMultipleEditorsPerDocument: false }
        )
    );

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
    if (!_addon) return;
    // Unregister the log callback before the addon shuts down.
    try { _addon.setLogCallback(null); } catch { /* ignore */ }
    try { _addon.destroyViewport(); } catch { /* ignore */ }
    try { _addon.shutdown(); } catch { /* ignore */ }
}
