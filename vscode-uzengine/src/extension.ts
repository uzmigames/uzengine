/**
 * extension.ts — VSCode extension entry point.
 *
 * Activation responsibilities:
 *  1. Start the ServerManager (manages render.exe child process).
 *  2. Initialise the ViewportManager singleton (manages all open viewport tabs).
 *  3. Subscribe to `vscode.window.onDidChangeWindowState` so all viewports are
 *     suspended when the developer switches to another application.
 *  4. Create the IpcClient (WebSocket-based IPC for editor panels).
 *  5. Register the SceneTreeProvider in the `uzengine.sceneTree` TreeView.
 *  6. Create the InspectorPanel and wire it to SceneTreeProvider selection.
 *  7. Create the PlayControls status bar items.
 *  8. Register commands:
 *       - uzengine.openStreamViewer  (start server + open scene viewport)
 *       - uzengine.startServer       (start server only)
 *       - uzengine.stopServer        (kill the server process)
 *       - uzengine.addNode           (add a child node to the selected node)
 *       - uzengine.deleteNode        (delete the selected node)
 *       - uzengine.renameNode        (rename the selected node)
 *       - uzengine.duplicateNode     (duplicate the selected node)
 *       - uzengine.selectNode        (internal: fires on tree item click)
 *       - uzengine.play              (start play mode)
 *       - uzengine.pause             (pause play mode)
 *       - uzengine.stop              (stop play mode)
 *
 * All registered disposables go into `context.subscriptions` so VSCode disposes
 * them automatically on deactivation — no explicit cleanup needed in `deactivate()`.
 */

import * as vscode from 'vscode';
import { ConsolePanel } from './console-panel';
import { EngineInstanceManager } from './engine-instance-manager';
import { InspectorPanel, InspectorViewProvider } from './inspector-panel';
import { IpcClient } from './ipc-client';
import { PlayControls } from './play-controls';
import { NodeItem, SceneTreeProvider } from './scene-tree-provider';
import { StreamPanel } from './stream-panel';
import { UzSceneEditorProvider } from './uzscene-editor';
import { parseUzScene } from './uzscene-parser';
import { ViewportManager } from './viewport-manager';

// ServerManager removed — each scene now manages its own render.exe
// via EngineInstanceManager.

/**
 * Called by VSCode when the extension is activated.
 * Registers commands, creates the ServerManager, and initialises the
 * ViewportManager singleton.
 */
export function activate(context: vscode.ExtensionContext): void {
    // -------------------------------------------------------------------------
    // 1. Shared output channel (used by EngineInstanceManager + ViewportManager)
    // -------------------------------------------------------------------------
    const outputChannel = vscode.window.createOutputChannel('UzEngine');
    context.subscriptions.push(outputChannel);

    // -------------------------------------------------------------------------
    // 2. EngineInstanceManager — manages per-scene engine instances.
    //    Each .uzscene editor tab spawns its own independent render.exe
    //    with a unique WebSocket port and IPC directory.
    // -------------------------------------------------------------------------
    const instanceMgr = EngineInstanceManager.initialize(context, outputChannel);

    // -------------------------------------------------------------------------
    // 3. ViewportManager singleton — manages all open viewport panels.
    //    The manager subscribes to onDidChangeWindowState internally to
    //    suspend all viewports when the VSCode window loses focus.
    // -------------------------------------------------------------------------
    ViewportManager.initialize(context, outputChannel);
    // ViewportManager is already pushed to context.subscriptions inside initialize().

    // -------------------------------------------------------------------------
    // 4. IpcClient — WebSocket-based IPC for editor panel communication.
    //    Dynamically connects to the ACTIVE engine instance's WS port.
    //    Reconnects whenever the active instance changes.
    // -------------------------------------------------------------------------
    const ipcClient = new IpcClient();
    context.subscriptions.push({
        dispose: () => ipcClient.disconnect(),
    });

    // Reconnect IpcClient when the active engine instance changes.
    // Also auto-subscribe to ERROR/FATAL logs (§8.5).
    const errorLogBuffer: Array<{ level: string; system: string; message: string; timestamp: number }> = [];
    const MAX_ERROR_BUFFER = 50;

    function reconnectIpcToActive(): void {
        try {
            const active = instanceMgr.getActiveInstance();
            if (active && active.isRunning) {
                ipcClient.connect(`ws://localhost:${active.wsPort}`);

                // Auto-subscribe to ERROR+ logs after a short delay (wait for WS open).
                setTimeout(async () => {
                    try {
                        await ipcClient.request('subscribe_logs', { level: 'ERROR', systems: [] });
                        outputChannel.appendLine('[UzEngine] Auto-subscribed to ERROR/FATAL logs.');
                    } catch {
                        // Engine may not support subscribe_logs yet — ignore.
                    }
                }, 500);
            }
        } catch {
            // EngineInstanceManager not ready yet — ignore.
        }
    }
    const activeChangedSub = instanceMgr.onActiveChanged(() => {
        reconnectIpcToActive();
    });
    context.subscriptions.push(activeChangedSub);

    // §8.6: Buffer error/fatal log entries and show notification for AI.
    const errorLogUnsub = ipcClient.on('engine.log', (payload: unknown) => {
        const p = payload as { level?: string; system?: string; message?: string; timestamp_ms?: number } | null;
        if (!p || typeof p.message !== 'string') { return; }
        if (p.level === 'ERROR' || p.level === 'FATAL') {
            errorLogBuffer.push({
                level: p.level,
                system: p.system || '',
                message: p.message,
                timestamp: p.timestamp_ms || Date.now(),
            });
            while (errorLogBuffer.length > MAX_ERROR_BUFFER) {
                errorLogBuffer.shift();
            }

            // Show notification for FATAL errors.
            if (p.level === 'FATAL') {
                vscode.window.showErrorMessage(
                    `Engine FATAL: ${p.message.slice(0, 100)}`,
                    'Show Error Log'
                ).then(choice => {
                    if (choice === 'Show Error Log') {
                        const content = JSON.stringify(errorLogBuffer, null, 2);
                        vscode.workspace.openTextDocument({ content, language: 'json' })
                            .then(doc => vscode.window.showTextDocument(doc));
                    }
                });
            }
        }
    });
    context.subscriptions.push({ dispose: errorLogUnsub });

    // -------------------------------------------------------------------------
    // 5. SceneTreeProvider — populates the uzengine.sceneTree TreeView.
    // -------------------------------------------------------------------------
    const sceneTreeProvider = new SceneTreeProvider(ipcClient);
    context.subscriptions.push(sceneTreeProvider);

    const treeView = vscode.window.createTreeView('uzengine.sceneTree', {
        treeDataProvider: sceneTreeProvider,
        showCollapseAll: true,
        canSelectMany: false,
    });
    context.subscriptions.push(treeView);

    // -------------------------------------------------------------------------
    // 6a. InspectorViewProvider — fixed sidebar view (Unity/Godot style).
    // -------------------------------------------------------------------------
    const inspectorViewProvider = new InspectorViewProvider(ipcClient);
    context.subscriptions.push(inspectorViewProvider);
    context.subscriptions.push(
        vscode.window.registerWebviewViewProvider(
            InspectorViewProvider.viewType,
            inspectorViewProvider
        )
    );

    // 6b. InspectorPanel — legacy floating tab (kept for compatibility).
    const inspectorPanel = new InspectorPanel(context.extensionUri, ipcClient);
    context.subscriptions.push(inspectorPanel);

    // Wire SceneTreeProvider selection → sidebar Inspector only.
    // (InspectorPanel is the legacy floating tab — no longer auto-opened.)
    const selectionSub = sceneTreeProvider.onNodeSelected(node => {
        inspectorViewProvider.showForNode(node);
    });
    context.subscriptions.push(selectionSub);

    // -------------------------------------------------------------------------
    // 6c. ConsolePanel — sidebar console view.
    // -------------------------------------------------------------------------
    const consolePanel = new ConsolePanel();
    context.subscriptions.push(consolePanel);
    context.subscriptions.push(
        vscode.window.registerWebviewViewProvider(
            ConsolePanel.viewType,
            consolePanel
        )
    );

    // Forward engine.log push events from IPC to the console panel.
    const logUnsub = ipcClient.on('engine.log', (payload: unknown) => {
        const p = payload as { level?: string; message?: string } | null;
        if (!p || typeof p.message !== 'string') { return; }
        const level = p.level === 'warning' ? 'warning'
            : p.level === 'error' ? 'error'
                : 'info';
        consolePanel.log(level, p.message);
    });
    context.subscriptions.push({ dispose: logUnsub });

    // -------------------------------------------------------------------------
    // 6d. UzSceneEditorProvider — custom editor for .uzscene files.
    // -------------------------------------------------------------------------
    context.subscriptions.push(
        vscode.window.registerCustomEditorProvider(
            UzSceneEditorProvider.viewType,
            new UzSceneEditorProvider(context),
            { webviewOptions: { retainContextWhenHidden: true } }
        )
    );

    // Wire viewport click-to-select → SceneTreeProvider node selection.
    UzSceneEditorProvider.onPickResult = (nodeId: number | null) => {
        void sceneTreeProvider.selectNodeById(nodeId);
    };

    // -------------------------------------------------------------------------
    // 7. PlayControls — status bar ▶ ⏸ ■ buttons.
    // -------------------------------------------------------------------------
    const playControls = new PlayControls(ipcClient);
    context.subscriptions.push(playControls);

    // -------------------------------------------------------------------------
    // 8. Commands
    // -------------------------------------------------------------------------

    // uzengine.openStreamViewer — open the legacy stream panel (uses active instance)
    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        async () => {
            // Ensure at least one instance is running.
            const active = instanceMgr.getActiveInstance();
            if (active) {
                await StreamPanel.createOrShow(context.extensionUri, context);
            } else {
                vscode.window.showInformationMessage(
                    'UzEngine: Open a .uzscene file first to start the engine.'
                );
            }
        }
    );

    // uzengine.startServer — no-op hint (engines are per-scene now)
    const startCommand = vscode.commands.registerCommand(
        'uzengine.startServer',
        () => {
            vscode.window.showInformationMessage(
                'UzEngine: Engines are now started automatically when you open a .uzscene file.'
            );
        }
    );

    // uzengine.stopServer — stop all per-scene engine instances
    const stopCommand = vscode.commands.registerCommand(
        'uzengine.stopServer',
        () => {
            for (const inst of instanceMgr.getAllInstances()) {
                instanceMgr.destroyInstance(inst.id);
            }
        }
    );

    // Scene tree: internal selection command (fired by NodeItem.command)
    const selectNodeCommand = vscode.commands.registerCommand(
        'uzengine.selectNode',
        async (item: NodeItem) => {
            await sceneTreeProvider.selectNode(item);
        }
    );

    // Scene tree: Add Node (prompts for type, adds under selected node)
    const addNodeCommand = vscode.commands.registerCommand(
        'uzengine.addNode',
        async (item?: NodeItem) => {
            await sceneTreeProvider.addNode(item);
        }
    );

    // Scene tree: Delete Node
    const deleteNodeCommand = vscode.commands.registerCommand(
        'uzengine.deleteNode',
        async (item: NodeItem) => {
            await sceneTreeProvider.deleteNode(item);
        }
    );

    // Scene tree: Rename Node
    const renameNodeCommand = vscode.commands.registerCommand(
        'uzengine.renameNode',
        async (item: NodeItem) => {
            await sceneTreeProvider.renameNode(item);
        }
    );

    // Scene tree: Duplicate Node
    const duplicateNodeCommand = vscode.commands.registerCommand(
        'uzengine.duplicateNode',
        async (item: NodeItem) => {
            await sceneTreeProvider.duplicateNode(item);
        }
    );

    // Play Controls: ▶ Play
    const playCommand = vscode.commands.registerCommand(
        'uzengine.play',
        async () => {
            await playControls.play();
        }
    );

    // Play Controls: ⏸ Pause
    const pauseCommand = vscode.commands.registerCommand(
        'uzengine.pause',
        async () => {
            await playControls.pause();
        }
    );

    // Play Controls: ■ Stop
    const stopPlayCommand = vscode.commands.registerCommand(
        'uzengine.stop',
        async () => {
            await playControls.stop();
        }
    );

    // Scene: Save Scene
    const saveSceneCommand = vscode.commands.registerCommand(
        'uzengine.saveScene',
        async () => {
            try {
                await ipcClient.request('scene.save', {});
                consolePanel.log('info', 'Scene saved.');
            } catch (err) {
                vscode.window.showErrorMessage(
                    `UzEngine: Failed to save scene — ` +
                    `${err instanceof Error ? err.message : String(err)}`
                );
            }
        }
    );

    // Scene: Refresh Scene Tree
    const refreshSceneCommand = vscode.commands.registerCommand(
        'uzengine.refreshScene',
        () => {
            sceneTreeProvider.refresh();
        }
    );

    // Console: Clear
    const consoleClearCommand = vscode.commands.registerCommand(
        'uzengine.consoleClear',
        () => {
            consolePanel.clear();
        }
    );

    // Gizmo: Translate (W)
    const gizmoTranslateCmd = vscode.commands.registerCommand(
        'uzengine.gizmoTranslate',
        () => {
            UzSceneEditorProvider.postToActiveWebview({ type: 'set_gizmo_mode', mode: 'translate' });
        }
    );

    // Gizmo: Rotate (E)
    const gizmoRotateCmd = vscode.commands.registerCommand(
        'uzengine.gizmoRotate',
        () => {
            UzSceneEditorProvider.postToActiveWebview({ type: 'set_gizmo_mode', mode: 'rotate' });
        }
    );

    // Gizmo: Scale (R)
    const gizmoScaleCmd = vscode.commands.registerCommand(
        'uzengine.gizmoScale',
        () => {
            UzSceneEditorProvider.postToActiveWebview({ type: 'set_gizmo_mode', mode: 'scale' });
        }
    );

    // Gizmo: Toggle Local/World
    const gizmoSpaceCmd = vscode.commands.registerCommand(
        'uzengine.gizmoSpaceToggle',
        () => {
            UzSceneEditorProvider.postToActiveWebview({ type: 'toggle_gizmo_space' });
        }
    );

    // AI Debug: Diagnose Crash — reads latest.json and opens it for AI analysis.
    const diagnoseCrashCmd = vscode.commands.registerCommand(
        'uzengine.diagnoseCrash',
        async () => {
            const active = instanceMgr.getActiveInstance();
            if (!active) {
                vscode.window.showWarningMessage('No active engine instance.');
                return;
            }
            try {
                const result = await active.engineClient.call('engine.getCrashReport', {});
                if (result.ok && result.report) {
                    const doc = await vscode.workspace.openTextDocument({
                        content: JSON.stringify(result.report, null, 2),
                        language: 'json',
                    });
                    await vscode.window.showTextDocument(doc);
                    consolePanel.log('info', 'Crash report opened for analysis.');
                } else {
                    vscode.window.showInformationMessage(
                        'No crash report found. The engine has not crashed recently.'
                    );
                }
            } catch (err) {
                // Engine may be dead — try reading the file directly.
                const fs = await import('fs');
                const path = await import('path');
                const exePath = (await import('./engine-instance')).EngineInstance.resolveExecutable();
                if (exePath) {
                    const crashPath = path.join(
                        path.dirname(exePath), '.uzengine', 'crash_reports', 'latest.json'
                    );
                    if (fs.existsSync(crashPath)) {
                        const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(crashPath));
                        await vscode.window.showTextDocument(doc);
                        consolePanel.log('info', 'Crash report opened from file (engine offline).');
                        return;
                    }
                }
                vscode.window.showErrorMessage(
                    `Failed to read crash report: ${err instanceof Error ? err.message : String(err)}`
                );
            }
        }
    );

    // AI Debug: Analyze Frame — captures a screenshot for AI visual analysis.
    const analyzeFrameCmd = vscode.commands.registerCommand(
        'uzengine.analyzeFrame',
        async () => {
            const active = instanceMgr.getActiveInstance();
            if (!active || !active.isRunning) {
                vscode.window.showWarningMessage('No running engine instance.');
                return;
            }
            try {
                const result = await active.engineClient.call('engine.screenshot', {});
                if (result.path) {
                    const uri = vscode.Uri.file(result.path as string);
                    await vscode.commands.executeCommand('vscode.open', uri);
                    consolePanel.log('info', `Frame captured: ${result.path}`);
                } else {
                    vscode.window.showInformationMessage('Screenshot captured (no file path returned).');
                }
            } catch (err) {
                vscode.window.showErrorMessage(
                    `Failed to capture frame: ${err instanceof Error ? err.message : String(err)}`
                );
            }
        }
    );

    context.subscriptions.push(
        openCommand,
        startCommand,
        stopCommand,
        selectNodeCommand,
        addNodeCommand,
        deleteNodeCommand,
        renameNodeCommand,
        duplicateNodeCommand,
        playCommand,
        pauseCommand,
        stopPlayCommand,
        saveSceneCommand,
        refreshSceneCommand,
        consoleClearCommand,
        gizmoTranslateCmd,
        gizmoRotateCmd,
        gizmoScaleCmd,
        gizmoSpaceCmd,
        diagnoseCrashCmd,
        analyzeFrameCmd,
    );

    // -------------------------------------------------------------------------
    // 9. Local .uzscene file watcher — populate Scene Tree from disk
    //    NOTE: We no longer send scene.load IPC here. Each render.exe
    //    auto-loads its scene via the --scene command-line argument.
    //    This section only updates the Scene Tree sidebar.
    // -------------------------------------------------------------------------

    function loadSceneTreeFromDocument(doc: vscode.TextDocument): void {
        if (!doc.fileName.endsWith('.uzscene')) { return; }
        const parsed = parseUzScene(doc.getText());
        sceneTreeProvider.loadFromFile(parsed.roots);
        consolePanel.log('info', `Scene loaded: ${parsed.sceneName || doc.fileName}`);
    }

    // Load any .uzscene already open when the extension activates.
    for (const doc of vscode.workspace.textDocuments) {
        loadSceneTreeFromDocument(doc);
    }

    const openDocSub = vscode.workspace.onDidOpenTextDocument(doc => {
        loadSceneTreeFromDocument(doc);
    });

    const changeDocSub = vscode.workspace.onDidChangeTextDocument(e => {
        loadSceneTreeFromDocument(e.document);
    });

    const closeDocSub = vscode.workspace.onDidCloseTextDocument(doc => {
        if (!doc.fileName.endsWith('.uzscene')) { return; }
        sceneTreeProvider.clear();
    });

    context.subscriptions.push(openDocSub, changeDocSub, closeDocSub);
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
