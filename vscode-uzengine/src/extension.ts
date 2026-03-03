/**
 * extension.ts — VSCode extension entry point.
 *
 * Activation responsibilities:
 *  1. Start the ServerManager (manages UzEngine.exe child process).
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
import * as fs from 'fs';
import * as path from 'path';
import { randomUUID } from 'crypto';
import { StreamPanel } from './stream-panel';
import { ServerManager } from './server-manager';
import { ViewportManager } from './viewport-manager';
import { IpcClient } from './ipc-client';
import { SceneTreeProvider, NodeItem, SceneNodeData } from './scene-tree-provider';
import { InspectorPanel, InspectorViewProvider } from './inspector-panel';
import { PlayControls } from './play-controls';
import { ConsolePanel } from './console-panel';
import { parseUzScene } from './uzscene-parser';
import { UzSceneEditorProvider } from './uzscene-editor';

let serverManager: ServerManager | undefined;

/**
 * Called by VSCode when the extension is activated.
 * Registers commands, creates the ServerManager, and initialises the
 * ViewportManager singleton.
 */
export function activate(context: vscode.ExtensionContext): void {
    // -------------------------------------------------------------------------
    // 1. Shared output channel (used by ServerManager + ViewportManager + EngineClient)
    // -------------------------------------------------------------------------
    const outputChannel = vscode.window.createOutputChannel('UzEngine');
    context.subscriptions.push(outputChannel);

    // -------------------------------------------------------------------------
    // 2. Server manager — manages UzEngine.exe child process lifecycle
    // -------------------------------------------------------------------------
    serverManager = new ServerManager();
    context.subscriptions.push(serverManager);

    // -------------------------------------------------------------------------
    // 3. ViewportManager singleton — manages all open viewport panels.
    //    The manager subscribes to onDidChangeWindowState internally to
    //    suspend all viewports when the VSCode window loses focus.
    // -------------------------------------------------------------------------
    ViewportManager.initialize(context, outputChannel);
    // ViewportManager is already pushed to context.subscriptions inside initialize().

    // -------------------------------------------------------------------------
    // 4. IpcClient — WebSocket-based IPC for editor panel communication.
    //    Reads the server URL from settings; auto-reconnects every 5 seconds.
    // -------------------------------------------------------------------------
    const ipcConfig = vscode.workspace.getConfiguration('uzengine');
    const serverUrl = ipcConfig.get<string>('serverUrl', 'ws://localhost:9002');

    const ipcClient = new IpcClient();
    ipcClient.connect(serverUrl);
    context.subscriptions.push({
        dispose: () => ipcClient.disconnect(),
    });

    // Reconnect when the server URL setting changes.
    const configSub = vscode.workspace.onDidChangeConfiguration(e => {
        if (e.affectsConfiguration('uzengine.serverUrl')) {
            const newConfig = vscode.workspace.getConfiguration('uzengine');
            const newUrl = newConfig.get<string>('serverUrl', 'ws://localhost:9002');
            ipcClient.connect(newUrl);
        }
    });
    context.subscriptions.push(configSub);

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
                    : p.level === 'error'   ? 'error'
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
            new UzSceneEditorProvider(serverManager!, context),
            { webviewOptions: { retainContextWhenHidden: true } }
        )
    );

    // -------------------------------------------------------------------------
    // 7. PlayControls — status bar ▶ ⏸ ■ buttons.
    // -------------------------------------------------------------------------
    const playControls = new PlayControls(ipcClient);
    context.subscriptions.push(playControls);

    // -------------------------------------------------------------------------
    // 8. Commands
    // -------------------------------------------------------------------------

    // Legacy: uzengine.openStreamViewer — start server + open scene viewport
    const openCommand = vscode.commands.registerCommand(
        'uzengine.openStreamViewer',
        async () => {
            const started = await serverManager!.start();
            if (started) {
                await StreamPanel.createOrShow(context.extensionUri, context);
            }
        }
    );

    // Legacy: uzengine.startServer — start server without opening a panel
    const startCommand = vscode.commands.registerCommand(
        'uzengine.startServer',
        async () => {
            await serverManager!.start();
        }
    );

    // Legacy: uzengine.stopServer — kill the server process
    const stopCommand = vscode.commands.registerCommand(
        'uzengine.stopServer',
        () => {
            serverManager!.stop();
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
    );

    // -------------------------------------------------------------------------
    // 9. Local .uzscene file watcher — populate Scene Tree from disk
    // -------------------------------------------------------------------------

    function loadSceneFromDocument(doc: vscode.TextDocument): void {
        if (!doc.fileName.endsWith('.uzscene')) { return; }
        const parsed = parseUzScene(doc.getText());
        sceneTreeProvider.loadFromFile(parsed.roots);
        consolePanel.log('info', `Scene loaded: ${parsed.sceneName || doc.fileName}`);

        // Send scene data to engine for 3D wireframe preview (file-based IPC).
        const nodes = flattenSceneNodes(parsed.roots);
        sendSceneLoad(nodes);
    }

    /**
     * Sends a scene.load command via the engine's file-based IPC protocol.
     * Writes command.json to build/Release/preview/ — the engine polls this file.
     */
    function sendSceneLoad(
        nodes: Array<{name: string; type: string; position: number[]; rotation: number[]; scale: number[]}>,
    ): void {
        const wsRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        if (!wsRoot) { return; }
        const commandFile = path.join(wsRoot, 'build', 'Release', 'preview', 'command.json');
        const cmd = JSON.stringify({
            id: randomUUID(),
            type: 'call',
            function: 'scene.load',
            args: { nodes },
        });
        try {
            const tmp = commandFile + '.tmp';
            fs.writeFileSync(tmp, cmd, 'utf8');
            fs.renameSync(tmp, commandFile);
        } catch {
            // Engine not running or preview dir missing — ignore.
        }
    }

    /** Recursively flattens a scene node tree into a flat array for IPC. */
    function flattenSceneNodes(roots: SceneNodeData[]): Array<{name: string; type: string; position: number[]; rotation: number[]; scale: number[]}> {
        const result: Array<{name: string; type: string; position: number[]; rotation: number[]; scale: number[]}> = [];
        function walk(nodes: SceneNodeData[]): void {
            for (const n of nodes) {
                result.push({
                    name: n.name,
                    type: n.type,
                    position: [0, 0, 0],
                    rotation: [0, 0, 0],
                    scale: [1, 1, 1],
                });
                if (n.children) { walk(n.children); }
            }
        }
        walk(roots);
        return result;
    }

    // Load any .uzscene already open when the extension activates.
    for (const doc of vscode.workspace.textDocuments) {
        loadSceneFromDocument(doc);
    }

    const openDocSub = vscode.workspace.onDidOpenTextDocument(doc => {
        loadSceneFromDocument(doc);
    });

    const changeDocSub = vscode.workspace.onDidChangeTextDocument(e => {
        loadSceneFromDocument(e.document);
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
