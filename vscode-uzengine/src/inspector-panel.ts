/**
 * inspector-panel.ts — Inspector for scene node properties.
 *
 * Exports two classes:
 *
 *   InspectorPanel       — legacy floating WebviewPanel (kept for compatibility)
 *   InspectorViewProvider — WebviewViewProvider that renders the Inspector as a
 *                           fixed sidebar view (uzengine.inspector), matching the
 *                           Unity/Godot/Unreal layout where Inspector is always
 *                           visible in the side panel.
 *
 * Supported property widget types:
 *  - string   → text input
 *  - float    → number input (accepts decimals)
 *  - int      → number input (integers only)
 *  - bool     → checkbox
 *  - vec3     → three number inputs (X, Y, Z)
 *  - color    → color picker (hex ↔ float {r,g,b})
 *  - resource → read-only path + Browse button
 *
 * Property changes are sent immediately via `scene.setProperty` IPC.
 */

import * as vscode from 'vscode';
import { IpcClient } from './ipc-client';
import { NodeItem } from './scene-tree-provider';

// ---------------------------------------------------------------------------
// Public types
// ---------------------------------------------------------------------------

/** Supported property value types rendered in the Inspector. */
export type PropertyType = 'string' | 'float' | 'int' | 'bool' | 'vec3' | 'color' | 'resource';

/** A single property descriptor for a scene node. */
export interface PropertyDescriptor {
    readonly key: string;
    readonly label: string;
    readonly type: PropertyType;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly value: any;
    readonly group: string;
    /** For resource-type properties, the asset type filter for the picker. */
    readonly assetType?: string;
}

/** Sentinel value for mixed property values across multi-select. */
export const MIXED_VALUE = '--';

/**
 * Computes the intersection of property descriptors across multiple nodes.
 * Properties that exist in all nodes (same key + same type) are included.
 * Values that differ across nodes are replaced with MIXED_VALUE.
 */
export function commonProperties(
    allNodeProps: PropertyDescriptor[][],
): PropertyDescriptor[] {
    if (allNodeProps.length === 0) { return []; }
    if (allNodeProps.length === 1) { return allNodeProps[0]; }

    const first = allNodeProps[0];
    const result: PropertyDescriptor[] = [];

    for (const prop of first) {
        let allHave = true;
        let allSame = true;

        for (let i = 1; i < allNodeProps.length; i++) {
            const match = allNodeProps[i].find(
                p => p.key === prop.key && p.type === prop.type
            );
            if (!match) {
                allHave = false;
                break;
            }
            if (allSame && JSON.stringify(match.value) !== JSON.stringify(prop.value)) {
                allSame = false;
            }
        }

        if (allHave) {
            result.push(allSame ? prop : {
                ...prop,
                value: MIXED_VALUE,
            });
        }
    }

    return result;
}

/** The message sent from the webview to the extension host when a property changes. */
interface PropertyChangeMessage {
    readonly command: 'propertyChanged';
    readonly nodeId: string;
    readonly key: string;
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly value: any;
}

// ---------------------------------------------------------------------------
// Asset QuickPick helper
// ---------------------------------------------------------------------------

/** Fetches assets from the engine and returns QuickPickItems filtered by type. */
export async function fetchAssetQuickPickItems(
    ipc: IpcClient,
    assetType: string,
): Promise<vscode.QuickPickItem[]> {
    try {
        const result = await ipc.request('asset.list', {}) as {
            assets?: Array<{ path: string; type: string; status: string }>;
        };
        const assets = result.assets ?? [];
        const filtered = assetType
            ? assets.filter(a => a.type === assetType)
            : assets;

        return filtered.map(a => {
            const name = a.path.split(/[\\/]/).pop() ?? a.path;
            return {
                label: name,
                description: a.path,
                detail: `${a.type} — ${a.status}`,
            };
        });
    } catch {
        return [];
    }
}

// ---------------------------------------------------------------------------
// InspectorPanel
// ---------------------------------------------------------------------------

/**
 * Manages the Inspector webview panel.
 *
 * One singleton panel is used — it is revealed/updated when a node is selected
 * and disposed when the extension deactivates.
 */
export class InspectorPanel implements vscode.Disposable {
    private static readonly VIEW_TYPE = 'uzengine.inspector';
    private static readonly TITLE = 'UzEngine Inspector';

    /** The underlying VSCode webview panel. */
    private _panel: vscode.WebviewPanel | null = null;

    /** The node currently displayed in the inspector. */
    private _currentNode: NodeItem | undefined;

    /** Whether this InspectorPanel has been disposed. */
    private _disposed = false;

    /** Disposables owned by the panel wrapper. */
    private readonly _disposables: vscode.Disposable[] = [];

    constructor(
        private readonly _extensionUri: vscode.Uri,
        private readonly _ipc: IpcClient
    ) { }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Opens the inspector for the given node, or clears it when node is undefined.
     * If the panel is already open, it is refreshed in place.
     *
     * @param node  The selected NodeItem, or undefined to show the "no selection" state.
     */
    showForNode(node: NodeItem | undefined): void {
        if (this._disposed) { return; }

        this._currentNode = node;

        if (!this._panel) {
            this._createPanel();
        } else {
            this._panel.reveal(vscode.ViewColumn.Two);
        }

        this._updateContent();
    }

    /**
     * Returns true if the inspector panel is currently visible.
     */
    get isVisible(): boolean {
        return this._panel?.visible ?? false;
    }

    dispose(): void {
        if (this._disposed) { return; }
        this._disposed = true;

        this._panel?.dispose();
        this._panel = null;

        for (const d of this._disposables) {
            d.dispose();
        }
        this._disposables.length = 0;
    }

    // -------------------------------------------------------------------------
    // Private — panel creation
    // -------------------------------------------------------------------------

    private _createPanel(): void {
        this._panel = vscode.window.createWebviewPanel(
            InspectorPanel.VIEW_TYPE,
            InspectorPanel.TITLE,
            vscode.ViewColumn.Two,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [this._extensionUri],
            }
        );

        // Listen for property changes sent from the webview.
        this._panel.webview.onDidReceiveMessage(
            (msg: unknown) => { void this._handleWebviewMessage(msg); },
            undefined,
            this._disposables
        );

        // Clean up when the panel is closed by the user.
        this._panel.onDidDispose(
            () => {
                this._panel = null;
                this._currentNode = undefined;
            },
            undefined,
            this._disposables
        );
    }

    // -------------------------------------------------------------------------
    // Private — content rendering
    // -------------------------------------------------------------------------

    private _updateContent(): void {
        if (!this._panel) { return; }

        const node = this._currentNode;

        if (!node) {
            this._panel.webview.html = this._renderNoSelection();
            return;
        }

        const properties = this._buildProperties(node);
        this._panel.webview.html = this._renderProperties(node, properties);
    }

    /**
     * Builds a list of property descriptors for the given node.
     * Phase 1: uses the node's metadata to populate transform + type properties.
     * Full property list will come from a `scene.getNodeProperties` IPC call in
     * a later phase.
     */
    private _buildProperties(node: NodeItem): PropertyDescriptor[] {
        const props: PropertyDescriptor[] = [
            // Node group — always present
            { key: 'name', label: 'Name', type: 'string', value: node.nodeName, group: 'Node' },
            { key: 'type', label: 'Type', type: 'string', value: node.nodeType, group: 'Node' },
            { key: 'id', label: 'ID', type: 'string', value: node.nodeId, group: 'Node' },
            // Visibility
            { key: 'visible', label: 'Visible', type: 'bool', value: true, group: 'Node' },
            // Transform group — standard for all Node3D+ types
            { key: 'position', label: 'Position', type: 'vec3', value: { x: 0, y: 0, z: 0 }, group: 'Transform' },
            { key: 'rotation', label: 'Rotation', type: 'vec3', value: { x: 0, y: 0, z: 0 }, group: 'Transform' },
            { key: 'scale', label: 'Scale', type: 'vec3', value: { x: 1, y: 1, z: 1 }, group: 'Transform' },
            // Rendering group
            { key: 'lod_bias', label: 'LOD Bias', type: 'float', value: 0.0, group: 'Rendering' },
            { key: 'layer', label: 'Layer', type: 'int', value: 0, group: 'Rendering' },
            // Material group
            { key: 'albedo_color', label: 'Albedo Color', type: 'color', value: { r: 1.0, g: 1.0, b: 1.0 }, group: 'Material' },
            { key: 'material', label: 'Material', type: 'resource', value: '', group: 'Material', assetType: 'Material' },
        ];

        return props;
    }

    /**
     * Renders a single property widget as an HTML string.
     */
    private _renderWidget(prop: PropertyDescriptor, nodeId: string): string {
        const esc = (v: string) => v
            .replace(/&/g, '&amp;')
            .replace(/"/g, '&quot;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;');

        const onChange = `onPropertyChange('${esc(nodeId)}', '${esc(prop.key)}', this)`;

        switch (prop.type) {
            case 'string':
                return `<input type="text" class="prop-input prop-string"
                    value="${esc(String(prop.value))}"
                    data-key="${esc(prop.key)}"
                    ${prop.key === 'id' || prop.key === 'type' ? 'readonly' : ''}
                    onchange="${onChange}" />`;

            case 'float':
                return `<input type="number" step="0.01" class="prop-input prop-float"
                    value="${String(Number(prop.value))}"
                    data-key="${esc(prop.key)}"
                    onchange="${onChange}" />`;

            case 'int':
                return `<input type="number" step="1" class="prop-input prop-int"
                    value="${String(Math.round(Number(prop.value)))}"
                    data-key="${esc(prop.key)}"
                    onchange="${onChange}" />`;

            case 'bool': {
                const checked = prop.value ? 'checked' : '';
                return `<input type="checkbox" class="prop-checkbox"
                    data-key="${esc(prop.key)}"
                    ${checked}
                    onchange="${onChange}" />`;
            }

            case 'vec3': {
                const v = prop.value as { x: number; y: number; z: number } ?? { x: 0, y: 0, z: 0 };
                return `<span class="vec3-group">
                    <label class="vec3-label">X</label>
                    <input type="number" step="0.01" class="prop-input prop-float vec3-x"
                        value="${String(v.x)}"
                        data-key="${esc(prop.key)}" data-component="x"
                        onchange="onVec3Change('${esc(nodeId)}', '${esc(prop.key)}', this)" />
                    <label class="vec3-label">Y</label>
                    <input type="number" step="0.01" class="prop-input prop-float vec3-y"
                        value="${String(v.y)}"
                        data-key="${esc(prop.key)}" data-component="y"
                        onchange="onVec3Change('${esc(nodeId)}', '${esc(prop.key)}', this)" />
                    <label class="vec3-label">Z</label>
                    <input type="number" step="0.01" class="prop-input prop-float vec3-z"
                        value="${String(v.z)}"
                        data-key="${esc(prop.key)}" data-component="z"
                        onchange="onVec3Change('${esc(nodeId)}', '${esc(prop.key)}', this)" />
                </span>`;
            }

            case 'color': {
                const c = prop.value as { r: number; g: number; b: number } ?? { r: 0, g: 0, b: 0 };
                const hex = '#' + [c.r, c.g, c.b].map(v =>
                    Math.max(0, Math.min(255, Math.round(v * 255)))
                        .toString(16).padStart(2, '0')
                ).join('');
                return `<input type="color" class="prop-input prop-color"
                    value="${hex}"
                    data-key="${esc(prop.key)}"
                    onchange="onColorChange('${esc(nodeId)}', '${esc(prop.key)}', this)" />`;
            }

            case 'resource': {
                const path = String(prop.value ?? '');
                const assetType = prop.assetType ?? '';
                return `<span class="resource-group">
                    <input type="text" class="prop-input prop-resource" readonly
                        value="${esc(path)}"
                        data-key="${esc(prop.key)}" />
                    <button class="asset-picker"
                        onclick="onAssetPicker('${esc(nodeId)}', '${esc(prop.key)}', '${esc(assetType)}')"
                        title="Pick from asset registry">[...]</button>
                    <button class="resource-browse"
                        onclick="onResourceBrowse('${esc(nodeId)}', '${esc(prop.key)}')">Browse...</button>
                </span>`;
            }
        }
    }

    /**
     * Renders the full inspector HTML for the given node and its properties.
     */
    private _renderProperties(node: NodeItem, properties: PropertyDescriptor[]): string {
        // Group properties by their group label.
        const groups = new Map<string, PropertyDescriptor[]>();
        for (const prop of properties) {
            if (!groups.has(prop.group)) {
                groups.set(prop.group, []);
            }
            groups.get(prop.group)!.push(prop);
        }

        let groupHtml = '';
        for (const [groupName, props] of groups) {
            const rows = props.map(prop => `
                <tr class="prop-row">
                    <td class="prop-label">${this._escHtml(prop.label)}</td>
                    <td class="prop-value">${this._renderWidget(prop, node.nodeId)}</td>
                </tr>`).join('');

            groupHtml += `
            <details class="prop-group" open>
                <summary class="prop-group-header">${this._escHtml(groupName)}</summary>
                <table class="prop-table">
                    <tbody>${rows}</tbody>
                </table>
            </details>`;
        }

        return this._wrapHtml(`
            <div class="inspector-header">
                <span class="node-icon">${this._escHtml(node.nodeType)}</span>
                <span class="node-name">${this._escHtml(node.nodeName)}</span>
            </div>
            ${groupHtml}
        `);
    }

    /**
     * Renders the "no node selected" placeholder HTML.
     */
    private _renderNoSelection(): string {
        return this._wrapHtml(`
            <div class="no-selection">
                <p>Select a node in the Scene Tree to inspect its properties.</p>
            </div>
        `);
    }

    /**
     * Wraps the inner HTML in a full HTML document with inline CSS and scripts.
     */
    private _wrapHtml(inner: string): string {
        return `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Content-Security-Policy"
          content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';">
    <title>UzEngine Inspector</title>
    <style>
        body {
            font-family: var(--vscode-font-family, sans-serif);
            font-size: var(--vscode-font-size, 13px);
            color: var(--vscode-foreground, #cccccc);
            background: var(--vscode-editor-background, #1e1e1e);
            margin: 0;
            padding: 8px;
        }
        .inspector-header {
            display: flex;
            align-items: center;
            gap: 6px;
            padding: 6px 0 10px 0;
            border-bottom: 1px solid var(--vscode-panel-border, #444);
            margin-bottom: 8px;
        }
        .node-icon { font-size: 1.1em; }
        .node-name { font-weight: 600; font-size: 1.05em; }
        .prop-group {
            margin-bottom: 6px;
            border: 1px solid var(--vscode-panel-border, #333);
            border-radius: 3px;
        }
        .prop-group-header {
            padding: 4px 8px;
            background: var(--vscode-sideBar-background, #252526);
            cursor: pointer;
            font-weight: 600;
            user-select: none;
            list-style: none;
        }
        .prop-table {
            width: 100%;
            border-collapse: collapse;
            padding: 4px;
        }
        .prop-row { border-bottom: 1px solid var(--vscode-panel-border, #2d2d2d); }
        .prop-row:last-child { border-bottom: none; }
        .prop-label {
            width: 35%;
            padding: 4px 8px;
            color: var(--vscode-descriptionForeground, #aaa);
            font-size: 0.9em;
            white-space: nowrap;
        }
        .prop-value { padding: 2px 4px; }
        .prop-input {
            width: 100%;
            box-sizing: border-box;
            background: var(--vscode-input-background, #3c3c3c);
            color: var(--vscode-input-foreground, #cccccc);
            border: 1px solid var(--vscode-input-border, #555);
            border-radius: 2px;
            padding: 2px 4px;
            font-size: 0.9em;
        }
        .prop-input:focus {
            outline: 1px solid var(--vscode-focusBorder, #007fd4);
        }
        .prop-input[readonly] {
            opacity: 0.6;
            cursor: default;
        }
        .vec3-group {
            display: flex;
            align-items: center;
            gap: 4px;
        }
        .vec3-label {
            font-size: 0.8em;
            color: var(--vscode-descriptionForeground, #aaa);
            min-width: 12px;
        }
        .vec3-group .prop-input { width: 60px; }
        .prop-checkbox { cursor: pointer; }
        .prop-color {
            width: 40px;
            height: 24px;
            padding: 1px 2px;
            cursor: pointer;
            border: 1px solid var(--vscode-input-border, #555);
        }
        .resource-group {
            display: flex;
            align-items: center;
            gap: 4px;
        }
        .resource-group .prop-input { flex: 1; }
        .resource-browse {
            background: var(--vscode-button-background, #0e639c);
            color: var(--vscode-button-foreground, #fff);
            border: none;
            border-radius: 2px;
            padding: 2px 8px;
            cursor: pointer;
            font-size: 0.85em;
            white-space: nowrap;
        }
        .resource-browse:hover {
            background: var(--vscode-button-hoverBackground, #1177bb);
        }
        .asset-picker {
            background: var(--vscode-button-secondaryBackground, #3a3d41);
            color: var(--vscode-button-secondaryForeground, #fff);
            border: none;
            border-radius: 2px;
            padding: 2px 6px;
            cursor: pointer;
            font-size: 0.85em;
            font-family: monospace;
            white-space: nowrap;
        }
        .asset-picker:hover {
            background: var(--vscode-button-secondaryHoverBackground, #505357);
        }
        .no-selection {
            padding: 20px;
            text-align: center;
            color: var(--vscode-descriptionForeground, #888);
        }
    </style>
</head>
<body>
    ${inner}
    <script>
        const vscode = acquireVsCodeApi();

        function onPropertyChange(nodeId, key, el) {
            let value;
            if (el.type === 'checkbox') {
                value = el.checked;
            } else if (el.type === 'number') {
                value = parseFloat(el.value);
                if (isNaN(value)) { return; }
            } else {
                value = el.value;
            }
            vscode.postMessage({ command: 'propertyChanged', nodeId, key, value });
        }

        function onVec3Change(nodeId, key, el) {
            const row = el.closest('.vec3-group');
            const x = parseFloat(row.querySelector('.vec3-x').value) || 0;
            const y = parseFloat(row.querySelector('.vec3-y').value) || 0;
            const z = parseFloat(row.querySelector('.vec3-z').value) || 0;
            vscode.postMessage({ command: 'propertyChanged', nodeId, key, value: { x, y, z } });
        }

        function onColorChange(nodeId, key, el) {
            const hex = el.value;
            const r = parseInt(hex.slice(1, 3), 16) / 255;
            const g = parseInt(hex.slice(3, 5), 16) / 255;
            const b = parseInt(hex.slice(5, 7), 16) / 255;
            vscode.postMessage({ command: 'propertyChanged', nodeId, key, value: { r, g, b } });
        }

        function onResourceBrowse(nodeId, key) {
            vscode.postMessage({ command: 'resourceBrowse', nodeId, key });
        }

        function onAssetPicker(nodeId, key, assetType) {
            vscode.postMessage({ command: 'openAssetPicker', nodeId, key, assetType });
        }

        window.addEventListener('message', function(event) {
            const msg = event.data;
            if (msg.command === 'resourcePathSelected') {
                const input = document.querySelector('input.prop-resource[data-key="' + msg.key + '"]');
                if (input) { input.value = msg.path; }
            }
        });
    </script>
</body>
</html>`;
    }

    private _escHtml(value: string): string {
        return value
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#39;');
    }

    // -------------------------------------------------------------------------
    // Private — message handling
    // -------------------------------------------------------------------------

    private async _handleWebviewMessage(msg: unknown): Promise<void> {
        if (this._disposed) { return; }
        if (typeof msg !== 'object' || msg === null) { return; }

        const message = msg as Record<string, unknown>;
        if (message.command === 'resourceBrowse') {
            await this._handleResourceBrowse(message as { nodeId: string; key: string });
            return;
        }
        if (message.command === 'openAssetPicker') {
            await this._handleAssetPicker(
                message as { nodeId: string; key: string; assetType: string }
            );
            return;
        }

        if (message.command !== 'propertyChanged') { return; }

        const propMsg = message as unknown as PropertyChangeMessage;
        try {
            await this._ipc.request('scene.setProperty', {
                nodeId: propMsg.nodeId,
                key: propMsg.key,
                // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
                value: propMsg.value,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to set property "${propMsg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    private async _handleAssetPicker(msg: { nodeId: string; key: string; assetType: string }): Promise<void> {
        const items = await fetchAssetQuickPickItems(this._ipc, msg.assetType);
        if (items.length === 0) {
            vscode.window.showInformationMessage(`No ${msg.assetType || 'assets'} found in registry.`);
            return;
        }

        const picked = await vscode.window.showQuickPick(items, {
            placeHolder: `Select ${msg.assetType || 'asset'} for "${msg.key}"`,
        });
        if (!picked) { return; }

        const selectedPath = picked.description ?? '';
        // Update webview display.
        if (this._panel) {
            void this._panel.webview.postMessage({
                command: 'resourcePathSelected',
                key: msg.key,
                path: selectedPath,
            });
        }
        // Assign via IPC.
        try {
            await this._ipc.request('scene.assignAsset', {
                nodeId: Number(msg.nodeId),
                resPath: selectedPath,
                assetType: msg.assetType,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to assign asset "${msg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    private async _handleResourceBrowse(msg: { nodeId: string; key: string }): Promise<void> {
        const result = await vscode.window.showOpenDialog({
            canSelectMany: false,
            filters: { 'Assets': ['uzmat', 'hlsl', 'uzscene', 'png', 'jpg', 'tga', 'obj', 'fbx'] },
        });

        if (!result || result.length === 0) { return; }

        const selectedPath = result[0].fsPath;
        // Update the webview input
        if (this._panel) {
            void this._panel.webview.postMessage({
                command: 'resourcePathSelected',
                key: msg.key,
                path: selectedPath,
            });
        }
        // Send IPC to engine
        try {
            await this._ipc.request('scene.setProperty', {
                nodeId: msg.nodeId,
                key: msg.key,
                value: selectedPath,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to set resource "${msg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }
}

// ---------------------------------------------------------------------------
// InspectorViewProvider — sidebar WebviewView (fixed, always visible)
// ---------------------------------------------------------------------------

/**
 * Provides the `uzengine.inspector` sidebar webview view.
 *
 * This is the recommended way to use the inspector — it appears as a fixed
 * panel in the UzEngine sidebar, below the Scene Tree, matching Unity/Godot
 * layout. Call `showForNode()` whenever the selection changes.
 *
 * Usage:
 * ```typescript
 * const provider = new InspectorViewProvider(ipcClient);
 * context.subscriptions.push(
 *   vscode.window.registerWebviewViewProvider('uzengine.inspector', provider)
 * );
 * sceneTree.onNodeSelected(node => provider.showForNode(node));
 * ```
 */
export class InspectorViewProvider implements vscode.WebviewViewProvider, vscode.Disposable {
    static readonly viewType = 'uzengine.inspector';

    private _view: vscode.WebviewView | undefined;
    private _currentNode: NodeItem | undefined;
    private _disposed = false;
    private _shellReady = false;

    constructor(private readonly _ipc: IpcClient) { }

    // -------------------------------------------------------------------------
    // WebviewViewProvider implementation
    // -------------------------------------------------------------------------

    resolveWebviewView(
        webviewView: vscode.WebviewView,
        _context: vscode.WebviewViewResolveContext,
        _token: vscode.CancellationToken
    ): void {
        this._view = webviewView;

        webviewView.webview.options = {
            enableScripts: true,
        };

        webviewView.webview.onDidReceiveMessage(
            (msg: unknown) => { void this._handleMessage(msg); }
        );

        // Set the HTML shell once — all subsequent updates go via postMessage.
        webviewView.webview.html = this._buildShellHtml();
        this._shellReady = true;

        // Push current state into the freshly resolved webview.
        this._render();
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    /**
     * Updates the inspector to display the given node, or shows the
     * "no selection" placeholder when `node` is undefined.
     */
    showForNode(node: NodeItem | undefined): void {
        if (this._disposed) { return; }
        this._currentNode = node;
        this._render();
    }

    /**
     * Updates the inspector for multi-node selection.
     * Shows common properties with mixed values displayed as "--".
     */
    showForNodes(nodes: NodeItem[]): void {
        if (this._disposed) { return; }
        if (nodes.length === 0) {
            this.showForNode(undefined);
            return;
        }
        if (nodes.length === 1) {
            this.showForNode(nodes[0]);
            return;
        }
        this._currentNode = undefined;
        this._renderMultiSelect(nodes);
    }

    /** Returns the node currently shown in the inspector. */
    get currentNode(): NodeItem | undefined {
        return this._currentNode;
    }

    dispose(): void {
        this._disposed = true;
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    private _render(): void {
        if (!this._view || !this._shellReady) { return; }

        const node = this._currentNode;
        if (!node) {
            void this._view.webview.postMessage({ command: 'clearNode' });
            return;
        }

        const properties = this._buildProperties(node);
        void this._view.webview.postMessage({
            command: 'updateNode',
            nodeId: node.nodeId,
            nodeName: node.nodeName,
            nodeType: node.nodeType,
            properties,
        });
    }

    private _renderMultiSelect(nodes: NodeItem[]): void {
        if (!this._view || !this._shellReady) { return; }

        const allProps = nodes.map(n => this._buildProperties(n));
        const common = commonProperties(allProps);

        void this._view.webview.postMessage({
            command: 'updateMultiSelect',
            count: nodes.length,
            properties: common,
        });
    }

    private _buildProperties(node: NodeItem): PropertyDescriptor[] {
        return [
            { key: 'name', label: 'Name', type: 'string', value: node.nodeName, group: 'Node' },
            { key: 'type', label: 'Type', type: 'string', value: node.nodeType, group: 'Node' },
            { key: 'id', label: 'ID', type: 'string', value: node.nodeId, group: 'Node' },
            { key: 'visible', label: 'Visible', type: 'bool', value: true, group: 'Node' },
            { key: 'position', label: 'Position', type: 'vec3', value: { x: 0, y: 0, z: 0 }, group: 'Transform' },
            { key: 'rotation', label: 'Rotation', type: 'vec3', value: { x: 0, y: 0, z: 0 }, group: 'Transform' },
            { key: 'scale', label: 'Scale', type: 'vec3', value: { x: 1, y: 1, z: 1 }, group: 'Transform' },
            { key: 'lod_bias', label: 'LOD Bias', type: 'float', value: 0.0, group: 'Rendering' },
            { key: 'layer', label: 'Layer', type: 'int', value: 0, group: 'Rendering' },
            { key: 'albedo_color', label: 'Albedo Color', type: 'color', value: { r: 1.0, g: 1.0, b: 1.0 }, group: 'Material' },
            { key: 'material', label: 'Material', type: 'resource', value: '', group: 'Material', assetType: 'Material' },
        ];
    }

    /**
     * Builds the static HTML shell. Set once; all dynamic content is driven
     * by postMessage from the extension host.
     */
    private _buildShellHtml(): string {
        return `<!DOCTYPE html>
<html lang="en"><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline';">
<style>
body{font-family:var(--vscode-font-family,sans-serif);font-size:var(--vscode-font-size,13px);color:var(--vscode-foreground,#ccc);background:var(--vscode-editor-background,#1e1e1e);margin:0;padding:8px;}
.inspector-header{display:flex;align-items:center;gap:6px;padding:6px 0 10px;border-bottom:1px solid var(--vscode-panel-border,#444);margin-bottom:8px;}
.node-icon{font-size:1.1em;}.node-name{font-weight:600;font-size:1.05em;}
.prop-group{margin-bottom:6px;border:1px solid var(--vscode-panel-border,#333);border-radius:3px;}
.prop-group-header{padding:4px 8px;background:var(--vscode-sideBar-background,#252526);cursor:pointer;font-weight:600;user-select:none;list-style:none;}
.prop-table{width:100%;border-collapse:collapse;padding:4px;}
.prop-row{border-bottom:1px solid var(--vscode-panel-border,#2d2d2d);}
.prop-row:last-child{border-bottom:none;}
.prop-label{width:35%;padding:4px 8px;color:var(--vscode-descriptionForeground,#aaa);font-size:.9em;white-space:nowrap;}
.prop-value{padding:2px 4px;}
.prop-input{width:100%;box-sizing:border-box;background:var(--vscode-input-background,#3c3c3c);color:var(--vscode-input-foreground,#ccc);border:1px solid var(--vscode-input-border,#555);border-radius:2px;padding:2px 4px;font-size:.9em;}
.prop-input:focus{outline:1px solid var(--vscode-focusBorder,#007fd4);}
.prop-input[readonly]{opacity:.6;cursor:default;}
.vec3-group{display:flex;align-items:center;gap:4px;}
.vec3-label{font-size:.8em;color:var(--vscode-descriptionForeground,#aaa);min-width:12px;}
.vec3-group .prop-input{width:60px;}
.prop-checkbox{cursor:pointer;}
.prop-color{width:40px;height:24px;padding:1px 2px;cursor:pointer;border:1px solid var(--vscode-input-border,#555);}
.resource-group{display:flex;align-items:center;gap:4px;}
.resource-group .prop-input{flex:1;}
.resource-browse{background:var(--vscode-button-background,#0e639c);color:var(--vscode-button-foreground,#fff);border:none;border-radius:2px;padding:2px 8px;cursor:pointer;font-size:.85em;white-space:nowrap;}
.resource-browse:hover{background:var(--vscode-button-hoverBackground,#1177bb);}
.asset-picker{background:var(--vscode-button-secondaryBackground,#3a3d41);color:var(--vscode-button-secondaryForeground,#fff);border:none;border-radius:2px;padding:2px 6px;cursor:pointer;font-size:.85em;font-family:monospace;white-space:nowrap;}
.asset-picker:hover{background:var(--vscode-button-secondaryHoverBackground,#505357);}
.no-selection{padding:20px;text-align:center;color:var(--vscode-descriptionForeground,#888);}
.mixed-value{color:var(--vscode-descriptionForeground,#888);font-style:italic;}
</style></head><body>
<div id="root"><div class="no-selection"><p>Select a node in the Scene Tree.</p></div></div>
<script>
const vscode=acquireVsCodeApi();
function esc(v){return String(v).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}

function renderWidget(prop, nodeId) {
  const key = esc(prop.key);
  const onCh = "onPropertyChange('"+esc(nodeId)+"','"+key+"',this)";
  switch(prop.type){
    case 'string': {
      const ro = (prop.key==='id'||prop.key==='type') ? ' readonly' : '';
      return '<input type="text" class="prop-input prop-string" value="'+esc(prop.value)+'" data-key="'+key+'"'+ro+' onchange="'+onCh+'" />';
    }
    case 'float':
      return '<input type="number" step="0.01" class="prop-input prop-float" value="'+Number(prop.value)+'" data-key="'+key+'" onchange="'+onCh+'" />';
    case 'int':
      return '<input type="number" step="1" class="prop-input prop-int" value="'+Math.round(Number(prop.value))+'" data-key="'+key+'" onchange="'+onCh+'" />';
    case 'bool':
      return '<input type="checkbox" class="prop-checkbox" data-key="'+key+'" '+(prop.value?'checked':'')+' onchange="'+onCh+'" />';
    case 'vec3': {
      const v = prop.value || {x:0,y:0,z:0};
      const v3Ch = "onVec3Change('"+esc(nodeId)+"','"+key+"',this)";
      return '<span class="vec3-group">'+
        '<label class="vec3-label">X</label><input type="number" step="0.01" class="prop-input prop-float vec3-x" value="'+v.x+'" data-key="'+key+'" data-component="x" onchange="'+v3Ch+'" />'+
        '<label class="vec3-label">Y</label><input type="number" step="0.01" class="prop-input prop-float vec3-y" value="'+v.y+'" data-key="'+key+'" data-component="y" onchange="'+v3Ch+'" />'+
        '<label class="vec3-label">Z</label><input type="number" step="0.01" class="prop-input prop-float vec3-z" value="'+v.z+'" data-key="'+key+'" data-component="z" onchange="'+v3Ch+'" />'+
        '</span>';
    }
    case 'color': {
      const c = prop.value || {r:0,g:0,b:0};
      const hex = '#' + [c.r,c.g,c.b].map(function(v){return Math.max(0,Math.min(255,Math.round(v*255))).toString(16).padStart(2,'0');}).join('');
      return '<input type="color" class="prop-input prop-color" value="'+hex+'" data-key="'+key+'" onchange="onColorChange(\''+esc(nodeId)+'\',\''+key+'\',this)" />';
    }
    case 'resource': {
      const path = String(prop.value || '');
      const aType = esc(prop.assetType || '');
      return '<span class="resource-group"><input type="text" class="prop-input prop-resource" readonly value="'+esc(path)+'" data-key="'+key+'" /><button class="asset-picker" onclick="onAssetPicker(\''+esc(nodeId)+'\',\''+key+'\',\''+aType+'\')" title="Pick from asset registry">[...]</button><button class="resource-browse" onclick="onResourceBrowse(\''+esc(nodeId)+'\',\''+key+'\')">Browse...</button></span>';
    }
  }
  return '';
}

function onPropertyChange(nodeId,key,el){
  let value;
  if(el.type==='checkbox'){value=el.checked;}
  else if(el.type==='number'){value=parseFloat(el.value);if(isNaN(value))return;}
  else{value=el.value;}
  vscode.postMessage({command:'propertyChanged',nodeId,key,value});
}
function onVec3Change(nodeId,key,el){
  const row=el.closest('.vec3-group');
  const x=parseFloat(row.querySelector('.vec3-x').value)||0;
  const y=parseFloat(row.querySelector('.vec3-y').value)||0;
  const z=parseFloat(row.querySelector('.vec3-z').value)||0;
  vscode.postMessage({command:'propertyChanged',nodeId,key,value:{x,y,z}});
}
function onColorChange(nodeId,key,el){
  const hex=el.value;
  const r=parseInt(hex.slice(1,3),16)/255;
  const g=parseInt(hex.slice(3,5),16)/255;
  const b=parseInt(hex.slice(5,7),16)/255;
  vscode.postMessage({command:'propertyChanged',nodeId,key,value:{r,g,b}});
}
function onResourceBrowse(nodeId,key){
  vscode.postMessage({command:'resourceBrowse',nodeId,key});
}
function onAssetPicker(nodeId,key,assetType){
  vscode.postMessage({command:'openAssetPicker',nodeId,key,assetType});
}

window.addEventListener('message', function(event) {
  const msg = event.data;
  const root = document.getElementById('root');
  if (!root) return;

  if (msg.command === 'clearNode') {
    root.innerHTML = '<div class="no-selection"><p>Select a node in the Scene Tree.</p></div>';
    return;
  }

  if (msg.command === 'resourcePathSelected') {
    const input = document.querySelector('input.prop-resource[data-key="'+msg.key+'"]');
    if (input) { input.value = msg.path; }
    return;
  }

  if (msg.command === 'updateMultiSelect') {
    let html = '<div class="inspector-header"><span class="node-name">'+esc(msg.count+' nodes selected')+'</span></div>';
    if (!msg.properties || msg.properties.length === 0) {
      html += '<div class="no-selection"><p>No common properties.</p></div>';
    } else {
      const groups = {};
      for (const prop of msg.properties) {
        if (!groups[prop.group]) groups[prop.group] = [];
        groups[prop.group].push(prop);
      }
      for (const groupName in groups) {
        const props = groups[groupName];
        let rows = '';
        for (const prop of props) {
          if (prop.value === '--') {
            rows += '<tr class="prop-row"><td class="prop-label">'+esc(prop.label)+'</td><td class="prop-value"><span class="mixed-value">--</span></td></tr>';
          } else {
            rows += '<tr class="prop-row"><td class="prop-label">'+esc(prop.label)+'</td><td class="prop-value">'+renderWidget(prop, 'multi')+'</td></tr>';
          }
        }
        html += '<details class="prop-group" open><summary class="prop-group-header">'+esc(groupName)+'</summary><table class="prop-table"><tbody>'+rows+'</tbody></table></details>';
      }
    }
    root.innerHTML = html;
    return;
  }

  if (msg.command === 'updateNode') {
    // Group properties
    const groups = {};
    for (const prop of msg.properties) {
      if (!groups[prop.group]) groups[prop.group] = [];
      groups[prop.group].push(prop);
    }

    let html = '<div class="inspector-header"><span class="node-icon">'+esc(msg.nodeType)+'</span><span class="node-name">'+esc(msg.nodeName)+'</span></div>';

    for (const groupName in groups) {
      const props = groups[groupName];
      let rows = '';
      for (const prop of props) {
        rows += '<tr class="prop-row"><td class="prop-label">'+esc(prop.label)+'</td><td class="prop-value">'+renderWidget(prop, msg.nodeId)+'</td></tr>';
      }
      html += '<details class="prop-group" open><summary class="prop-group-header">'+esc(groupName)+'</summary><table class="prop-table"><tbody>'+rows+'</tbody></table></details>';
    }

    root.innerHTML = html;
  }
});
</script></body></html>`;
    }

    private async _handleMessage(msg: unknown): Promise<void> {
        if (this._disposed) { return; }
        if (typeof msg !== 'object' || msg === null) { return; }

        const message = msg as Record<string, unknown>;
        if (message.command === 'resourceBrowse') {
            await this._handleResourceBrowse(message as { nodeId: string; key: string });
            return;
        }
        if (message.command === 'openAssetPicker') {
            await this._handleAssetPicker(
                message as { nodeId: string; key: string; assetType: string }
            );
            return;
        }

        if (message.command !== 'propertyChanged') { return; }
        const propMsg = message as unknown as PropertyChangeMessage;
        try {
            await this._ipc.request('scene.setProperty', {
                nodeId: propMsg.nodeId,
                key: propMsg.key,
                // eslint-disable-next-line @typescript-eslint/no-unsafe-assignment
                value: propMsg.value,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to set property "${propMsg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    private async _handleAssetPicker(msg: { nodeId: string; key: string; assetType: string }): Promise<void> {
        const items = await fetchAssetQuickPickItems(this._ipc, msg.assetType);
        if (items.length === 0) {
            vscode.window.showInformationMessage(`No ${msg.assetType || 'assets'} found in registry.`);
            return;
        }

        const picked = await vscode.window.showQuickPick(items, {
            placeHolder: `Select ${msg.assetType || 'asset'} for "${msg.key}"`,
        });
        if (!picked) { return; }

        const selectedPath = picked.description ?? '';
        if (this._view) {
            void this._view.webview.postMessage({
                command: 'resourcePathSelected',
                key: msg.key,
                path: selectedPath,
            });
        }
        try {
            await this._ipc.request('scene.assignAsset', {
                nodeId: Number(msg.nodeId),
                resPath: selectedPath,
                assetType: msg.assetType,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to assign asset "${msg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }

    private async _handleResourceBrowse(msg: { nodeId: string; key: string }): Promise<void> {
        const result = await vscode.window.showOpenDialog({
            canSelectMany: false,
            filters: { 'Assets': ['uzmat', 'hlsl', 'uzscene', 'png', 'jpg', 'tga', 'obj', 'fbx'] },
        });

        if (!result || result.length === 0) { return; }

        const selectedPath = result[0].fsPath;
        if (this._view) {
            void this._view.webview.postMessage({
                command: 'resourcePathSelected',
                key: msg.key,
                path: selectedPath,
            });
        }
        try {
            await this._ipc.request('scene.setProperty', {
                nodeId: msg.nodeId,
                key: msg.key,
                value: selectedPath,
            });
        } catch (err) {
            vscode.window.showErrorMessage(
                `UzEngine: Failed to set resource "${msg.key}" — ` +
                `${err instanceof Error ? err.message : String(err)}`
            );
        }
    }
}
