# Asset Browser — Content Management and Drag-Drop Integration

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/asset_browser.ts`, `engine/editor/asset_manager.h`
> **References**: Asset Pipeline (§19), Texture Pipeline (§34), Mesh Pipeline (§35), UE5 Content Browser, Godot FileSystem dock

## Purpose

The Asset Browser provides a visual interface for browsing, searching, importing, and managing all project assets (meshes, textures, materials, scripts, sounds, scenes). Unlike a file browser, the Asset Browser presents assets as draggable thumbnails with type-specific icons, supports grid and list views, and integrates directly with the Inspector (drag-assign asset references) and Viewport (drag-create entities). This is the primary UI for content discovery and assignment.

## Architecture

```
Asset Browser (TypeScript/Webview)
    │
    ├─ Directory tree (left panel)
    │  └─ Browse content/ folders (res:// paths)
    │
    ├─ Asset grid (right panel)
    │  ├─ Thumbnail grid or list view
    │  ├─ Drag-drop support
    │  └─ Context menu (Import, Rename, Delete, etc.)
    │
    └─ Event handlers:
       ├─ Single-click → select asset
       ├─ Double-click → open in appropriate editor
       ├─ Drag → drag to Inspector, Hierarchy, or Viewport
       ├─ Right-click → context menu
       └─ Keyboard → delete, rename, import
            │
            └─ Calls IPC to update assets on C++ side
```

## Asset Management API (C++)

```cpp
/// Represents a single asset file in the project.
struct AssetMetadata {
    std::string asset_path;                // res://meshes/player.gltf (virtual path)
    std::string absolute_path;             // Full disk path
    std::string file_name;                 // player.gltf
    AssetType type;                        // Mesh, Texture, Material, Script, Sound, Scene, Animation, VFX
    uint64_t file_size;                    // Bytes
    int64_t modified_time;                 // Unix timestamp
    std::string thumbnail_path;            // res://.cache/thumbs/player_thumb.png
    std::vector<std::string> tags;         // User-defined tags
};

/// Asset type enumeration.
enum class AssetType : uint8_t {
    Unknown = 0,
    Mesh = 1,
    Texture = 2,
    Material = 3,
    Script = 4,
    Sound = 5,
    Scene = 6,
    Animation = 7,
    VFX = 8,
};

/// Asset manager (C++ side).
class AssetManager {
public:
    /// List assets in directory (non-recursive).
    std::vector<AssetMetadata> ListDirectory(std::string_view dir_path) const;

    /// Get asset metadata.
    AssetMetadata GetAsset(std::string_view asset_path) const;

    /// Find assets by type.
    std::vector<AssetMetadata> FindByType(AssetType type) const;

    /// Find assets by pattern (glob or substring).
    std::vector<AssetMetadata> FindByPattern(std::string_view pattern) const;

    /// Search assets by name, tag, or type.
    struct SearchQuery {
        std::optional<std::string> name_pattern;
        std::optional<AssetType> type_filter;
        std::optional<std::string> tag_filter;
        std::optional<std::pair<int64_t, int64_t>> size_range;  // Min, Max
        std::optional<std::pair<int64_t, int64_t>> date_range;
    };
    std::vector<AssetMetadata> Search(const SearchQuery& query) const;

    /// Generate thumbnail for asset (returns path to cached thumbnail).
    /// For images: crop/scale to 128x128.
    /// For meshes: offline uzcook generates thumbnail.
    /// For sounds: generate waveform visualization.
    std::string GetThumbnail(std::string_view asset_path);

    /// Rename asset.
    bool RenameAsset(std::string_view asset_path, std::string_view new_name);

    /// Move asset (changes directory).
    bool MoveAsset(std::string_view asset_path, std::string_view dest_dir);

    /// Delete asset (moves to trash).
    bool DeleteAsset(std::string_view asset_path);

    /// Duplicate asset.
    std::string DuplicateAsset(std::string_view asset_path);

    /// Get .import.json sidecar (if exists).
    struct ImportSettings {
        std::string compression;           // "bc7", "bc5", "none"
        int32_t quality = 80;              // 0-100
        bool generate_mipmaps = true;
        int32_t max_width = 4096, max_height = 4096;
    };
    std::optional<ImportSettings> GetImportSettings(std::string_view asset_path) const;

    /// Update import settings and re-cook asset.
    void SetImportSettings(std::string_view asset_path, const ImportSettings& settings);

    /// Get asset dependency graph (what references this asset?).
    std::vector<std::string> GetDependencies(std::string_view asset_path) const;

    /// Get recently used assets.
    std::vector<AssetMetadata> GetRecentlyUsed(size_t limit = 10) const;
};
```

## Asset Browser Component (TypeScript)

```typescript
/// Manages visual asset browsing with grid/list view, search, and drag-drop.
export class AssetBrowser {
    private currentDirectory: string = "res://";
    private assets: AssetMetadata[] = [];
    private selectedAssets: Set<string> = new Set();
    private viewMode: 'grid' | 'list' = 'grid';
    private searchQuery: string = '';
    private filterType: AssetType | null = null;

    private directoryTree: HTMLElement;
    private assetGrid: HTMLElement;
    private searchInput: HTMLInputElement;

    constructor(private container: HTMLElement, private ipc: IPC) {
        this.setupUI();
        this.attachEventListeners();
        this.loadDirectory(this.currentDirectory);
    }

    private setupUI() {
        this.container.className = 'asset-browser';

        // Top toolbar
        const toolbar = document.createElement('div');
        toolbar.className = 'asset-toolbar';

        // Search bar
        this.searchInput = document.createElement('input');
        this.searchInput.type = 'text';
        this.searchInput.placeholder = 'Search assets...';
        this.searchInput.className = 'asset-search';
        this.searchInput.oninput = () => this.performSearch();
        toolbar.appendChild(this.searchInput);

        // Filter dropdown
        const filterSelect = document.createElement('select');
        filterSelect.className = 'asset-filter';
        const options = [
            { value: '', label: 'All Types' },
            { value: 'Mesh', label: 'Meshes' },
            { value: 'Texture', label: 'Textures' },
            { value: 'Material', label: 'Materials' },
            { value: 'Script', label: 'Scripts' },
            { value: 'Sound', label: 'Sounds' },
            { value: 'Scene', label: 'Scenes' },
            { value: 'Animation', label: 'Animations' },
            { value: 'VFX', label: 'VFX' },
        ];
        for (const opt of options) {
            const el = document.createElement('option');
            el.value = opt.value;
            el.textContent = opt.label;
            filterSelect.appendChild(el);
        }
        filterSelect.onchange = () => {
            this.filterType = filterSelect.value as any;
            this.performSearch();
        };
        toolbar.appendChild(filterSelect);

        // View mode toggle
        const gridButton = document.createElement('button');
        gridButton.className = 'view-mode-btn active';
        gridButton.textContent = '⊞ Grid';
        gridButton.onclick = () => {
            this.viewMode = 'grid';
            gridButton.classList.add('active');
            listButton.classList.remove('active');
            this.render();
        };
        toolbar.appendChild(gridButton);

        const listButton = document.createElement('button');
        listButton.className = 'view-mode-btn';
        listButton.textContent = '☰ List';
        listButton.onclick = () => {
            this.viewMode = 'list';
            listButton.classList.add('active');
            gridButton.classList.remove('active');
            this.render();
        };
        toolbar.appendChild(listButton);

        this.container.appendChild(toolbar);

        // Left panel: directory tree
        const splitContainer = document.createElement('div');
        splitContainer.className = 'asset-browser-split';

        this.directoryTree = document.createElement('div');
        this.directoryTree.className = 'directory-tree';
        splitContainer.appendChild(this.directoryTree);

        // Right panel: asset grid
        this.assetGrid = document.createElement('div');
        this.assetGrid.className = 'asset-grid';
        splitContainer.appendChild(this.assetGrid);

        this.container.appendChild(splitContainer);
    }

    async loadDirectory(path: string) {
        this.currentDirectory = path;
        const assets = await this.ipc.call("assets.list_directory", [path]);
        this.assets = assets;
        this.render();
    }

    private render() {
        // Render directory tree
        this.renderDirectoryTree();

        // Render asset grid with current assets
        this.assetGrid.innerHTML = '';
        const container = this.viewMode === 'grid'
            ? document.createElement('div')
            : document.createElement('div');
        container.className = `asset-container asset-${this.viewMode}`;

        for (const asset of this.assets) {
            const item = this.createAssetItem(asset);
            container.appendChild(item);
        }

        this.assetGrid.appendChild(container);
    }

    private renderDirectoryTree() {
        this.directoryTree.innerHTML = '';

        const root = document.createElement('div');
        root.className = 'directory-item';

        const label = document.createElement('div');
        label.className = 'directory-label';
        label.textContent = 'res://';
        label.onclick = () => this.loadDirectory('res://');
        if (this.currentDirectory === 'res://') label.classList.add('selected');
        root.appendChild(label);

        // Build tree recursively
        const subdirs = [
            'res://meshes/',
            'res://textures/',
            'res://materials/',
            'res://scripts/',
            'res://sounds/',
            'res://scenes/',
            'res://animations/',
            'res://vfx/',
        ];

        for (const dir of subdirs) {
            const item = document.createElement('div');
            item.className = 'directory-item';
            const label = document.createElement('div');
            label.className = 'directory-label';
            label.textContent = dir.split('/')[1] || 'root';
            label.onclick = () => this.loadDirectory(dir);
            if (this.currentDirectory === dir) label.classList.add('selected');
            item.appendChild(label);
            root.appendChild(item);
        }

        this.directoryTree.appendChild(root);
    }

    private createAssetItem(asset: AssetMetadata): HTMLElement {
        const item = document.createElement('div');
        item.className = 'asset-item';
        item.dataset.assetPath = asset.asset_path;
        if (this.selectedAssets.has(asset.asset_path)) {
            item.classList.add('selected');
        }

        if (this.viewMode === 'grid') {
            // Grid view: thumbnail + name
            const thumbnail = document.createElement('img');
            thumbnail.className = 'asset-thumbnail';
            thumbnail.src = asset.thumbnail_path || 'icon-unknown.png';
            thumbnail.alt = asset.file_name;
            item.appendChild(thumbnail);

            const name = document.createElement('div');
            name.className = 'asset-name';
            name.textContent = asset.file_name;
            name.title = asset.asset_path;
            item.appendChild(name);

            const badge = document.createElement('div');
            badge.className = 'asset-badge';
            badge.textContent = this.getTypeBadge(asset.type);
            item.appendChild(badge);
        } else {
            // List view: icon + name + size + date
            const icon = document.createElement('img');
            icon.className = 'asset-list-icon';
            icon.src = this.getAssetIcon(asset.type);
            item.appendChild(icon);

            const name = document.createElement('div');
            name.className = 'asset-list-name';
            name.textContent = asset.file_name;
            item.appendChild(name);

            const size = document.createElement('div');
            size.className = 'asset-list-size';
            size.textContent = this.formatSize(asset.file_size);
            item.appendChild(size);

            const date = document.createElement('div');
            date.className = 'asset-list-date';
            date.textContent = new Date(asset.modified_time * 1000).toLocaleDateString();
            item.appendChild(date);
        }

        // Event handlers
        item.onclick = (e) => {
            if (e.ctrlKey) {
                this.selectedAssets.toggle(asset.asset_path);
            } else {
                this.selectedAssets.clear();
                this.selectedAssets.add(asset.asset_path);
            }
            this.render();
        };

        item.ondblclick = () => {
            this.openAsset(asset);
        };

        item.oncontextmenu = (e) => {
            e.preventDefault();
            this.showAssetContextMenu(asset, e);
        };

        // Drag-drop: dragging asset from browser
        item.draggable = true;
        item.ondragstart = (e) => {
            e.dataTransfer!.effectAllowed = 'copy';
            e.dataTransfer!.setData('application/x-asset-path', asset.asset_path);
            e.dataTransfer!.setData('application/x-asset-type', String(asset.type));
        };

        return item;
    }

    private async performSearch() {
        const query = this.searchInput.value.trim();
        const results = await this.ipc.call("assets.search", [{
            name_pattern: query || undefined,
            type_filter: this.filterType || undefined,
        }]);

        this.assets = results;
        this.render();
    }

    private async openAsset(asset: AssetMetadata) {
        // Open asset in appropriate editor based on type
        switch (asset.type) {
            case 'Mesh':
                // Open mesh in viewport preview
                await this.ipc.call("editor.preview_mesh", [asset.asset_path]);
                break;
            case 'Texture':
                // Open texture inspector
                await this.ipc.call("editor.preview_texture", [asset.asset_path]);
                break;
            case 'Scene':
                // Open scene in editor
                await this.ipc.call("editor.open_scene", [asset.asset_path]);
                break;
            case 'Script':
                // Open script in code editor
                await this.ipc.call("editor.open_script", [asset.asset_path]);
                break;
            default:
                console.log('No editor for asset type:', asset.type);
        }
    }

    private showAssetContextMenu(asset: AssetMetadata, event: MouseEvent) {
        const menu = document.createElement('div');
        menu.className = 'context-menu';
        menu.style.left = event.clientX + 'px';
        menu.style.top = event.clientY + 'px';

        const items = [
            { label: 'Open', action: () => this.openAsset(asset) },
            { label: 'Duplicate', action: () => this.duplicateAsset(asset.asset_path) },
            { label: 'Rename', action: () => this.renameAsset(asset) },
            { label: 'Move...', action: () => this.moveAsset(asset.asset_path) },
            { label: 'Delete', action: () => this.deleteAsset(asset.asset_path) },
            { divider: true },
            { label: 'Show in Explorer', action: () => this.showInExplorer(asset.absolute_path) },
            { label: 'Copy Path', action: () => this.copyToClipboard(asset.asset_path) },
            { divider: true },
            { label: 'View Dependencies', action: () => this.showDependencies(asset.asset_path) },
            { label: 'Import Settings...', action: () => this.showImportSettings(asset) },
        ];

        for (const item of items) {
            if (item.divider) {
                menu.appendChild(document.createElement('hr'));
            } else {
                const btn = document.createElement('button');
                btn.textContent = item.label!;
                btn.onclick = async () => {
                    await item.action!();
                    if (document.body.contains(menu)) {
                        document.body.removeChild(menu);
                    }
                };
                menu.appendChild(btn);
            }
        }

        document.body.appendChild(menu);

        const closeMenu = () => {
            if (document.body.contains(menu)) {
                document.body.removeChild(menu);
            }
            document.removeEventListener('click', closeMenu);
        };
        setTimeout(() => document.addEventListener('click', closeMenu), 0);
    }

    private async renameAsset(asset: AssetMetadata) {
        const newName = prompt('New name:', asset.file_name);
        if (newName && newName !== asset.file_name) {
            await this.ipc.call("assets.rename", [asset.asset_path, newName]);
            this.loadDirectory(this.currentDirectory);
        }
    }

    private async duplicateAsset(assetPath: string) {
        const newPath = await this.ipc.call("assets.duplicate", [assetPath]);
        this.loadDirectory(this.currentDirectory);
    }

    private async moveAsset(assetPath: string) {
        const newDir = prompt('Move to directory:', 'res://');
        if (newDir) {
            await this.ipc.call("assets.move", [assetPath, newDir]);
            this.loadDirectory(this.currentDirectory);
        }
    }

    private async deleteAsset(assetPath: string) {
        if (confirm(`Delete ${assetPath}?`)) {
            await this.ipc.call("assets.delete", [assetPath]);
            this.loadDirectory(this.currentDirectory);
        }
    }

    private async showDependencies(assetPath: string) {
        const deps = await this.ipc.call("assets.get_dependencies", [assetPath]);
        const depList = deps.join('\n');
        alert(`Dependencies of ${assetPath}:\n\n${depList}`);
    }

    private async showImportSettings(asset: AssetMetadata) {
        // Open import settings dialog
        console.log('Import settings for:', asset.asset_path);
    }

    private showInExplorer(path: string) {
        this.ipc.call("platform.show_in_explorer", [path]);
    }

    private copyToClipboard(text: string) {
        navigator.clipboard.writeText(text);
    }

    private getAssetIcon(type: AssetType): string {
        const icons: { [key: string]: string } = {
            'Mesh': 'icon-mesh.png',
            'Texture': 'icon-texture.png',
            'Material': 'icon-material.png',
            'Script': 'icon-script.png',
            'Sound': 'icon-sound.png',
            'Scene': 'icon-scene.png',
            'Animation': 'icon-animation.png',
            'VFX': 'icon-vfx.png',
        };
        return icons[type] || 'icon-unknown.png';
    }

    private getTypeBadge(type: AssetType): string {
        const badges: { [key: string]: string } = {
            'Mesh': '🔲',
            'Texture': '🖼',
            'Material': '✨',
            'Script': '📝',
            'Sound': '🔊',
            'Scene': '🌍',
            'Animation': '▶',
            'VFX': '💫',
        };
        return badges[type] || '?';
    }

    private formatSize(bytes: number): string {
        const units = ['B', 'KB', 'MB', 'GB'];
        let size = bytes;
        let unitIdx = 0;
        while (size >= 1024 && unitIdx < units.length - 1) {
            size /= 1024;
            unitIdx++;
        }
        return `${size.toFixed(1)} ${units[unitIdx]}`;
    }
}
```

## Thumbnail Generation

Thumbnails are generated in two ways:

1. **Offline (uzcook)**: Large assets (meshes, scenes) have thumbnails baked during cooking.
2. **On-demand (runtime)**: Images are automatically cropped/scaled to 128x128 on first load.

```cpp
/// Generate and cache thumbnail for asset.
std::string AssetManager::GetThumbnail(std::string_view asset_path) {
    const auto& meta = GetAsset(asset_path);

    // Check if cached thumbnail exists
    if (fs::exists(meta.thumbnail_path)) {
        return meta.thumbnail_path;
    }

    // Generate thumbnail based on type
    switch (meta.type) {
        case AssetType::Texture:
            return GenerateImageThumbnail(meta.absolute_path);
        case AssetType::Mesh:
            // Meshes should be pre-cooked with thumbnail
            return "icon-mesh.png";  // Fallback
        case AssetType::Scene:
            return GenerateSceneThumbnail(meta.absolute_path);
        default:
            return GetTypeIcon(meta.type);
    }
}

std::string AssetManager::GenerateImageThumbnail(std::string_view image_path) {
    // Load image with stb_image
    // Scale to 128x128 maintaining aspect ratio
    // Save as PNG to .cache/thumbs/
    // Return path
}
```

## CSS Styling

```css
.asset-browser {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: #252526;
    color: #cccccc;
}

.asset-toolbar {
    display: flex;
    gap: 8px;
    padding: 8px;
    background: #2d2d30;
    border-bottom: 1px solid #464647;
    align-items: center;
}

.asset-search {
    flex: 1;
    padding: 6px 8px;
    background: #3e3e42;
    border: 1px solid #444;
    color: #cccccc;
    border-radius: 2px;
}

.asset-filter,
.view-mode-btn {
    padding: 6px 12px;
    background: #3e3e42;
    border: 1px solid #444;
    color: #cccccc;
    border-radius: 2px;
    cursor: pointer;
}

.view-mode-btn.active {
    background: #094771;
    border-color: #0e639c;
}

.asset-browser-split {
    display: grid;
    grid-template-columns: 180px 1fr;
    gap: 1px;
    flex: 1;
    overflow: hidden;
    background: #1e1e1e;
}

.directory-tree {
    background: #252526;
    border-right: 1px solid #464647;
    overflow-y: auto;
    padding: 8px 0;
}

.directory-item {
    padding: 0;
}

.directory-label {
    padding: 6px 12px;
    cursor: pointer;
    user-select: none;
    font-size: 13px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.directory-label:hover {
    background: #3e3e42;
}

.directory-label.selected {
    background: #094771;
    color: #fff;
}

.asset-grid {
    overflow: auto;
    padding: 8px;
}

.asset-container {
    display: grid;
    gap: 8px;
}

.asset-container.asset-grid {
    grid-template-columns: repeat(auto-fill, minmax(128px, 1fr));
}

.asset-container.asset-list {
    grid-template-columns: 1fr;
}

.asset-item {
    cursor: pointer;
    user-select: none;
    border: 1px solid transparent;
    border-radius: 4px;
    padding: 4px;
    transition: background 0.2s;
}

.asset-item:hover {
    background: #3e3e42;
}

.asset-item.selected {
    background: #094771;
    border-color: #0e639c;
}

.asset-item.asset-grid {
    display: flex;
    flex-direction: column;
    gap: 4px;
    align-items: center;
}

.asset-thumbnail {
    width: 100%;
    aspect-ratio: 1;
    object-fit: cover;
    border-radius: 2px;
    background: #1e1e1e;
}

.asset-name {
    font-size: 12px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    text-align: center;
    width: 100%;
}

.asset-badge {
    position: absolute;
    bottom: 4px;
    right: 4px;
    font-size: 14px;
}

.asset-item.asset-list {
    display: grid;
    grid-template-columns: 20px 1fr 80px 120px;
    gap: 8px;
    align-items: center;
    padding: 6px 8px;
}

.asset-list-icon {
    width: 20px;
    height: 20px;
}

.asset-list-name {
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.asset-list-size {
    text-align: right;
    font-size: 12px;
    color: #969696;
}

.asset-list-date {
    text-align: right;
    font-size: 12px;
    color: #969696;
}

.context-menu {
    position: fixed;
    background: #252526;
    border: 1px solid #464647;
    border-radius: 2px;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.5);
    z-index: 10000;
    min-width: 200px;
}

.context-menu button {
    display: block;
    width: 100%;
    padding: 8px 12px;
    border: none;
    background: none;
    color: #cccccc;
    text-align: left;
    cursor: pointer;
    font-size: 12px;
}

.context-menu button:hover {
    background: #094771;
}

.context-menu hr {
    margin: 4px 0;
    border: none;
    border-top: 1px solid #464647;
}
```

## Comparison

| Feature | UE5 (Content Browser) | Godot (FileSystem) | Unity Project | **UzEngine** |
|---|---|---|---|---|
| **Grid view** | ✓ Thumbnails | ✗ List only | ✓ Thumbnails | **✓ Both grid/list** |
| **Directory tree** | ✓ Sidebar | ✓ Sidebar | ✓ Sidebar | **✓ Sidebar** |
| **Drag-drop to viewport** | ✓ Creates actor | ✓ Spawns node | ✓ Spawns prefab | **✓ Creates entity** |
| **Drag-drop to inspector** | ✓ Asset ref | ✓ Asset ref | ✓ Component ref | **✓ Asset ref** |
| **Search/filter** | ✓ Advanced | ✓ Basic | ✓ Advanced | **✓ Type + name + tag** |
| **Context menu** | ✓ Full | ✓ Full | ✓ Limited | **✓ Full** |
| **Thumbnail caching** | ✓ Offline baked | ✓ On-demand | ✓ On-demand | **✓ Both methods** |
| **Dependency viewer** | ✓ Reference viewer | ✗ | ✗ | **✓ Shows dependencies** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Grid/list view toggle, directory tree, asset icons, basic drag-drop to inspector, basic search |
| **Phase 3** | Thumbnail caching (on-demand + offline), context menu (Rename/Delete/Duplicate/Show in Explorer), import settings sidecar editing, dependency viewer, drag-drop to viewport (creates entity), type/date/size filters |
| **Phase 4** | Favorites/recently used, advanced search (tags, date range, size range), asset preview modal, multi-select bulk operations, asset tagging UI |

