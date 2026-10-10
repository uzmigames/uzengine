/// UzEngine VSCode extension entry point.
///
/// Activates language features (syntax highlighting, icons) immediately and
/// defers all native addon access until it is actually needed. The native
/// addon now runs inside an isolated child process managed by `RuntimeClient`.
/// This prevents GPU driver crashes or SEH exceptions from killing the VSCode
/// Extension Host process — only the worker process dies.
///
/// Isolation architecture:
///   Extension Host  →  RuntimeClient (IPC)  →  runtime-worker.js  →  uzengine.node
///
/// Panel ownership model:
///  - `EngineConsole` — permanent; lives for the full extension lifetime.
///    Its output channel is created immediately; log forwarding is wired once
///    a `RuntimeClient` is live via `attachClient()`.
///  - `SceneTreeProvider` — permanent; drives the "Scene Hierarchy" tree view.
///    Registered immediately; only queries the client when `refresh()` is called.
///  - `ViewportPanel` — created on demand; at most one instance at a time.
///    Set to `undefined` after the panel is closed so a new one can be opened.
///  - `EngineMode` — permanent; drives the status bar toggle.

import * as vscode from 'vscode';
import { RuntimeClient } from './runtime-client';
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

// ── RuntimeClient singleton ───────────────────────────────────────────────────

/// The single RuntimeClient for this extension session.
///
/// Created lazily on the first call to `ensureClient()`. Using a singleton
/// ensures that at most one worker process is alive at a time, which is
/// required because the native viewport is also a singleton inside the engine.
let _client: RuntimeClient | undefined;

/// Lazily creates and initialises the RuntimeClient.
///
/// The first call spawns the worker process and configures the engine directory.
/// Subsequent calls return the cached client immediately.
///
/// IMPORTANT: This function only creates the client and sets the engine dir.
/// It does NOT call `client.init()` — that is done lazily by the callers that
/// need the full engine (viewport creation, scene load, etc.) so that the addon
/// loads as late as possible without blocking the Extension Host startup.
function ensureClient(): RuntimeClient {
  if (_client) return _client;

  log.info('[Extension] ensureClient: creating RuntimeClient + spawning worker');
  _client = new RuntimeClient(log);

  // Wire up console log forwarding.
  engineConsole.attachClient(_client);

  // Remember the engine dir so RuntimeClient.init() can apply it.
  // Don't call setEngineDir here — the worker hasn't loaded the addon yet,
  // and the SceneEditorProvider may restart it immediately. The setEngineDir
  // is applied inside client.init() via rememberedEngineDir.
  // eslint-disable-next-line @typescript-eslint/no-require-imports
  const nodePath = require('path') as typeof import('path');
  const nativeDir = nodePath.join(__dirname, '..', 'native');
  _client.setEngineDir(nativeDir).catch(() => {
    // Expected to fail on first create — init() will re-apply.
  });

  return _client;
}

// ── Activation ────────────────────────────────────────────────────────────────

/// Activates the UzEngine extension.
///
/// Called by VSCode when one of the extension's activation events fires
/// (e.g. the user opens a `.uzscene` file or runs an `uzengine.*` command).
///
/// Language features (syntax highlighting, icons) are available immediately.
/// The native addon is NOT loaded here — it is loaded lazily inside the
/// worker process via `ensureClient()` when the engine is first needed.
///
/// # Arguments
/// * `context` - Extension context used to register disposables and persist state.
export function activate(context: vscode.ExtensionContext): void {
  // ── Logging channel ───────────────────────────────────────────────────────
  log = vscode.window.createOutputChannel('UzEngine', { log: true });
  context.subscriptions.push(log);

  // ── Console panel — output channel created immediately, callback deferred ─
  engineConsole = new EngineConsole();
  context.subscriptions.push(engineConsole);

  // ── Scene tree provider — registered immediately, queries client lazily ───
  sceneTree = new SceneTreeProvider(() => _client);
  const treeView = vscode.window.createTreeView('uzengine.sceneTree', {
    treeDataProvider: sceneTree,
    showCollapseAll: true,
  });
  context.subscriptions.push(treeView);

  // ── Scene editor — opens .uzscene files in the viewport ──────────────────
  context.subscriptions.push(
    vscode.window.registerCustomEditorProvider(
      SceneEditorProvider.viewType,
      new SceneEditorProvider(ensureClient, sceneTree, log),
      { supportsMultipleEditorsPerDocument: false },
    ),
  );

  // ── Engine mode status bar toggle ─────────────────────────────────────────
  engineMode = new EngineMode(context);

  // ── Commands ──────────────────────────────────────────────────────────────
  context.subscriptions.push(
    // uzengine.start — initialise the runtime (useful for testing without
    // opening the full viewport).
    vscode.commands.registerCommand('uzengine.start', () => {
      const client = ensureClient();
      client
        .init()
        .then(() => client.getVersion())
        .then((version) => {
          log.info(`[Extension] Runtime initialized — v${version}`);
          void vscode.window.showInformationMessage(`UzEngine ${version} initialized`);
        })
        .catch((e: unknown) => {
          const msg = e instanceof Error ? e.message : String(e);
          log.error(`[Extension] Runtime init failed: ${msg}`);
          void vscode.window.showErrorMessage(`UzEngine init failed: ${msg}`);
        });
    }),

    // uzengine.openViewport — create the viewport panel (or reveal if already open).
    vscode.commands.registerCommand('uzengine.openViewport', () => {
      const client = ensureClient();
      if (viewport) {
        viewport.reveal();
        return;
      }
      viewport = new ViewportPanel(context.extensionUri, client, sceneTree);
      viewport.onDidDispose(() => {
        viewport = undefined;
      });
    }),

    // uzengine.openScene — show file picker, load .uzscene, refresh tree.
    vscode.commands.registerCommand('uzengine.openScene', async (uri?: vscode.Uri) => {
      if (!uri) {
        const files = await vscode.window.showOpenDialog({
          canSelectMany: false,
          filters: { 'UzEngine Scene': ['uzscene'] },
          title: 'Open UzEngine Scene',
        });
        if (!files || files.length === 0) return;
        uri = files[0];
      }

      const client = ensureClient();

      // Ensure viewport is open so the scene has somewhere to render.
      if (!viewport) {
        viewport = new ViewportPanel(context.extensionUri, client, sceneTree);
        viewport.onDidDispose(() => {
          viewport = undefined;
        });
      } else {
        viewport.reveal();
      }

      try {
        await client.loadScene(uri.fsPath);
        sceneTree.refresh();
        log.info(`[Extension] Scene loaded: ${uri.fsPath}`);
      } catch (e: unknown) {
        const msg = e instanceof Error ? e.message : String(e);
        log.error(`[Extension] Failed to load scene: ${msg}`);
        void vscode.window.showErrorMessage(`Failed to load scene: ${msg}`);
      }
    }),

    // uzengine.toggleEngineMode — switch between Code Mode and Engine Mode.
    vscode.commands.registerCommand('uzengine.toggleEngineMode', () => {
      engineMode.toggle();
    }),

    // uzengine.refreshSceneTree — force re-query of the entity hierarchy.
    vscode.commands.registerCommand('uzengine.refreshSceneTree', () => {
      sceneTree.refresh();
    }),
  );

  log.info('[Extension] UzEngine extension activated (worker deferred until first scene open)');
}

// ── Deactivation ──────────────────────────────────────────────────────────────

/// Deactivates the UzEngine extension.
///
/// Called by VSCode before the extension host process exits or when the
/// extension is explicitly disabled. Resources must be released promptly.
/// The RuntimeClient sends a graceful shutdown to the worker and then kills it.
export function deactivate(): void {
  if (viewport) {
    viewport.dispose();
    viewport = undefined;
  }
  if (_client) {
    _client.dispose();
    _client = undefined;
  }
}
