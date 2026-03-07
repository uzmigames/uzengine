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
import { AssetBrowserProvider, AssetGroupItem, AssetGroupKey, AssetItem } from './asset-browser-provider';
import { AssetWatcher } from './asset-watcher';
import { ConsolePanel } from './console-panel';
import { DragDropCoordinator } from './editor/dragDropCoordinator';
import { SelectionManager } from './editor/selectionManager';
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

/** File type filters for the "Import Asset Here..." context menu command. */
function groupImportFilters(group: AssetGroupKey): Record<string, string[]> | null {
    switch (group) {
        case 'Mesh':     return { 'Meshes': ['glb', 'gltf', 'fbx', 'obj'] };
        case 'Texture':  return { 'Textures': ['png', 'jpg', 'jpeg', 'tga', 'bmp', 'hdr'] };
        case 'Audio':    return { 'Audio': ['wav', 'ogg', 'mp3'] };
        case 'Material': return { 'Materials': ['uzmat'] };
        case 'Shader':   return { 'Shaders': ['hlsl'] };
        case 'Scene':    return { 'Scenes': ['uzscene'] };
        default:         return null;
    }
}

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
        canSelectMany: true,
        dragAndDropController: sceneTreeProvider.dragController,
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

    // -------------------------------------------------------------------------
    // 6b2. SelectionManager — multi-node selection state.
    // -------------------------------------------------------------------------
    const selectionManager = new SelectionManager();
    context.subscriptions.push(selectionManager);

    // Guard to prevent feedback loops between TreeView ↔ SelectionManager.
    let _syncingFromTree = false;
    let _syncingToTree = false;

    // Wire TreeView native multi-select → SelectionManager.
    // VSCode handles Ctrl+click (toggle) and Shift+click (range) natively
    // when canSelectMany is true. onDidChangeSelection fires with the
    // complete selection list after each click.
    const treeSelectionSub = treeView.onDidChangeSelection((e) => {
        if (_syncingToTree) { return; }
        _syncingFromTree = true;
        try {
            const ids = e.selection.map(item => parseInt(item.nodeId, 10));
            if (ids.length === 0) {
                selectionManager.clear();
            } else if (ids.length === 1) {
                selectionManager.selectOnly(ids[0]);
            } else {
                selectionManager.selectMany(ids);
            }
            // Update flat tree cache for range selection from other sources.
            selectionManager.setFlatTree(sceneTreeProvider.getFlatTree());
        } finally {
            _syncingFromTree = false;
        }
    });
    context.subscriptions.push(treeSelectionSub);

    // Reverse sync: SelectionManager → TreeView reveal (for viewport picks).
    selectionManager.onChanged((_selected) => {
        if (_syncingFromTree) { return; }
        // Reveal primary node in the tree so the user can see it.
        if (selectionManager.primaryId !== undefined) {
            const nodeItem = sceneTreeProvider.findNodeById(
                String(selectionManager.primaryId)
            );
            if (nodeItem) {
                _syncingToTree = true;
                treeView.reveal(nodeItem, { select: true, focus: false })
                    .then(() => { _syncingToTree = false; },
                          () => { _syncingToTree = false; });
            }
        }
    });

    // -------------------------------------------------------------------------
    // 6b3. DragDropCoordinator — cross-panel drag-and-drop state.
    // -------------------------------------------------------------------------
    const dragDropCoordinator = new DragDropCoordinator(ipcClient);
    context.subscriptions.push(dragDropCoordinator);
    // Export for Scene Tree drag controller.
    sceneTreeProvider.setDragDropCoordinator(dragDropCoordinator);

    // -------------------------------------------------------------------------
    // 6b4. AssetBrowserProvider — asset browser tree view.
    // -------------------------------------------------------------------------
    const assetBrowserProvider = new AssetBrowserProvider(ipcClient);
    context.subscriptions.push(assetBrowserProvider);
    assetBrowserProvider.setDragDropCoordinator(dragDropCoordinator);

    const assetBrowserView = vscode.window.createTreeView('uzengine.assetBrowser', {
        treeDataProvider: assetBrowserProvider,
        showCollapseAll: true,
        canSelectMany: false,
        dragAndDropController: assetBrowserProvider.dragController,
    });
    context.subscriptions.push(assetBrowserView);

    // Auto-refresh asset browser when IPC reconnects.
    const assetRefreshSub = instanceMgr.onActiveChanged(() => {
        // Small delay to let IPC connect first.
        setTimeout(() => void assetBrowserProvider.refresh(), 600);
    });
    context.subscriptions.push(assetRefreshSub);

    // Wire viewport pick events → SelectionManager + asset drop.
    ViewportManager.onViewportCreated((panel) => {
        panel.onNodePicked((ev) => {
            if (ev.nodeId === null) {
                selectionManager.clear();
            } else if (ev.ctrlKey) {
                selectionManager.toggle(ev.nodeId);
            } else {
                selectionManager.selectOnly(ev.nodeId);
            }
        });
        panel.onBoxPicked((ev) => {
            if (ev.nodeIds.length === 0) {
                selectionManager.clear();
            } else if (ev.ctrlKey) {
                selectionManager.addMany(ev.nodeIds);
            } else {
                selectionManager.selectMany(ev.nodeIds);
            }
        });
        // Asset drop on viewport → DragDropCoordinator.handleViewportDrop.
        panel.onAssetDropped(async (ev) => {
            const result = await dragDropCoordinator.handleViewportDrop(ev.x, ev.y);
            if (result.ok && result.nodeId) {
                selectionManager.selectOnly(result.nodeId);
                void sceneTreeProvider.refresh();
            } else if (!result.ok && result.error) {
                vscode.window.showErrorMessage(`Place asset failed: ${result.error}`);
            }
        });

        // Cancel drag on Escape key press in the viewport.
        panel.onDragCancelled(() => {
            dragDropCoordinator.endDrag();
        });

        // Relay DragDropCoordinator state changes to viewport panels.
        dragDropCoordinator.onStateChanged((state) => {
            panel.postDragState(state === 'dragging');
        });
    });

    // SelectionManager → Inspector sync + engine IPC.
    selectionManager.onChanged((selected) => {
        if (selected.size === 0) {
            inspectorViewProvider.showForNode(undefined);
        } else if (selected.size === 1) {
            const nodeId = [...selected][0];
            const nodeItem = sceneTreeProvider.findNodeById(String(nodeId));
            if (nodeItem) {
                inspectorViewProvider.showForNode(nodeItem);
            }
        } else {
            // Multi-select: show common properties with mixed values.
            const nodes = [...selected]
                .map(id => sceneTreeProvider.findNodeById(String(id)))
                .filter((n): n is NodeItem => n !== undefined);
            inspectorViewProvider.showForNodes(nodes);
        }
        // Task 3.5 / 4.1: send selection to engine so gizmo updates.
        void ipcClient.request('editor.setSelection', {
            nodeIds: [...selected],
        });
    });

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

    // Scene tree: programmatic selection command.
    const selectNodeCommand = vscode.commands.registerCommand(
        'uzengine.selectNode',
        async (item: NodeItem) => {
            selectionManager.selectOnly(parseInt(item.nodeId, 10));
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

    // Play Controls: ▶ Play — switch viewport to play port (9003).
    const playCommand = vscode.commands.registerCommand(
        'uzengine.play',
        async () => {
            await playControls.play();
            for (const panel of ViewportManager.getInstance().getAllPanels()) {
                panel.switchPort(9003);
            }
        }
    );

    // Play Controls: ⏸ Pause
    const pauseCommand = vscode.commands.registerCommand(
        'uzengine.pause',
        async () => {
            await playControls.pause();
        }
    );

    // Play Controls: ■ Stop — switch viewport back to editor port (9002).
    const stopPlayCommand = vscode.commands.registerCommand(
        'uzengine.stop',
        async () => {
            await playControls.stop();
            for (const panel of ViewportManager.getInstance().getAllPanels()) {
                panel.switchPort(9002);
            }
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

    // Asset Browser: Refresh
    const assetRefreshCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.refresh',
        () => void assetBrowserProvider.refresh(),
    );

    // Asset Browser: Reimport
    const assetReimportCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.reimport',
        async (item: AssetItem) => {
            await assetBrowserProvider.reimportAsset(item);
        },
    );

    // Asset Browser: New Material
    const assetNewMaterialCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.newMaterial',
        async () => void assetBrowserProvider.createMaterial(),
    );

    // Asset Browser: New Scene
    const assetNewSceneCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.newScene',
        async () => void assetBrowserProvider.createScene(),
    );

    // Asset Browser: New Shader
    const assetNewShaderCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.newShader',
        async () => void assetBrowserProvider.createShader(),
    );

    // Asset Browser: Reveal (no-op placeholder — click handler).
    const assetRevealCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.reveal',
        () => { /* TreeItem click — no action needed. */ },
    );

    // Asset Browser: Assign to selected node.
    const assetAssignCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.assignToSelected',
        async (item: AssetItem) => {
            const selected = [...selectionManager.selected];
            if (selected.length !== 1) {
                vscode.window.showWarningMessage('Select exactly one node in the Scene Tree first.');
                return;
            }
            const nodeId = selected[0];
            try {
                await ipcClient.request('scene.assignAsset', {
                    nodeId: Number(nodeId),
                    resPath: item.assetPath,
                    assetType: item.assetType,
                });
                vscode.window.showInformationMessage(
                    `Assigned ${item.assetPath} to node ${nodeId}.`
                );
            } catch (err) {
                vscode.window.showErrorMessage(
                    `Failed to assign asset: ${err instanceof Error ? err.message : String(err)}`
                );
            }
        },
    );
    context.subscriptions.push(assetAssignCmd);

    // Asset Browser: Import files from OS.
    const assetImportCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.importFiles',
        async () => void assetBrowserProvider.importFiles(),
    );
    context.subscriptions.push(assetImportCmd);

    // Asset Browser: Import asset into specific group (context menu on group items).
    const assetImportHereCmd = vscode.commands.registerCommand(
        'uzengine.assetBrowser.importHere',
        async (item: AssetGroupItem) => {
            const filters = groupImportFilters(item.groupKey);
            if (!filters) { return; }
            const uris = await vscode.window.showOpenDialog({
                canSelectMany: true,
                openLabel: 'Import',
                filters,
            });
            if (!uris || uris.length === 0) { return; }
            const wsFolder = vscode.workspace.workspaceFolders?.[0];
            if (!wsFolder) { return; }
            for (const uri of uris) {
                const fileName = uri.fsPath.split(/[\\/]/).pop() || 'asset';
                const destUri = vscode.Uri.joinPath(wsFolder.uri, 'assets', fileName);
                try {
                    await vscode.workspace.fs.copy(uri, destUri, { overwrite: true });
                    await ipcClient.request('asset.import', { path: destUri.fsPath });
                } catch (e) {
                    vscode.window.showErrorMessage(
                        `Import failed for ${fileName}: ${e instanceof Error ? e.message : String(e)}`
                    );
                }
            }
            await assetBrowserProvider.refresh();
        },
    );
    context.subscriptions.push(assetImportHereCmd);

    // AssetWatcher — auto-import when files are added to assets/.
    const assetWatcher = new AssetWatcher(ipcClient, () => void assetBrowserProvider.refresh());
    context.subscriptions.push(assetWatcher);

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

    // Multi-select: Select All (Ctrl+A)
    const selectAllCmd = vscode.commands.registerCommand(
        'uzengine.selectAll',
        () => {
            const flat = sceneTreeProvider.getFlatTree();
            if (flat.length > 0) {
                selectionManager.selectMany(flat.map(n => n.id));
            }
        }
    );

    // Multi-select: Clear Selection (Escape)
    const clearSelectionCmd = vscode.commands.registerCommand(
        'uzengine.clearSelection',
        () => {
            selectionManager.clear();
        }
    );

    // Multi-select: Delete Selected (Delete key)
    const deleteSelectedCmd = vscode.commands.registerCommand(
        'uzengine.deleteSelected',
        async () => {
            const ids = selectionManager.ids;
            if (ids.length === 0) { return; }
            for (const id of ids) {
                const item = sceneTreeProvider.findNodeById(String(id));
                if (item) {
                    await sceneTreeProvider.deleteNode(item);
                }
            }
            selectionManager.clear();
        }
    );

    // Multi-select: Duplicate Selected (Ctrl+D)
    const duplicateSelectedCmd = vscode.commands.registerCommand(
        'uzengine.duplicateSelected',
        async () => {
            const ids = selectionManager.ids;
            if (ids.length === 0) { return; }
            for (const id of ids) {
                const item = sceneTreeProvider.findNodeById(String(id));
                if (item) {
                    await sceneTreeProvider.duplicateNode(item);
                }
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
        selectAllCmd,
        clearSelectionCmd,
        deleteSelectedCmd,
        duplicateSelectedCmd,
        assetRefreshCmd,
        assetReimportCmd,
        assetNewMaterialCmd,
        assetNewSceneCmd,
        assetNewShaderCmd,
        assetRevealCmd,
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
