# Hierarchy Panel — Scene Tree and Entity Organization

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/hierarchy.ts`, `engine/editor/scene_graph.h`
> **References**: Property System (§21), Scene Format (§18), UE5 World Outliner, Godot Scene Tree

## Purpose

The Hierarchy panel displays the scene as a tree of entities and their parent-child relationships. It enables viewing, selecting, organizing, and manipulating the scene structure. Unlike flat entity lists, the Hierarchy respects the spatial transform hierarchy and allows drag-drop reparenting, multi-select, and context menu operations. This is the primary UI for scene navigation and structure editing.

## Architecture

```
Hierarchy Panel (TypeScript/Webview)
    │
    ├─ Load scene graph from runtime
    │  └─ C++ SceneGraph::GetHierarchy() returns tree structure
    │
    ├─ Virtual scrolling container (for 10k+ entities)
    │  └─ Render only visible rows
    │
    └─ Entity rows (each with)
       ├─ Expand arrow (collapse/expand subtree)
       ├─ Component icon (visual indicator of node type)
       ├─ Entity name (editable)
       ├─ Visibility toggle (eye icon)
       ├─ Lock toggle (lock icon)
       │
       └─ Event handlers:
          ├─ Click → select entity
          ├─ Ctrl+Click → multi-select
          ├─ Shift+Click → range select
          ├─ Drag → reparenting
          ├─ Right-click → context menu
          └─ Double-click → rename inline
               │
               └─ Calls IPC to update hierarchy on C++ side
```

## Scene Hierarchy API (C++)

```cpp
/// Represents a single node in the scene hierarchy.
struct HierarchyNode {
    uint32_t entity_id;                    // Unique entity ID
    std::string name;                      // User-facing entity name
    uint32_t parent_id;                    // Parent entity ID (0 = root)
    std::vector<uint32_t> child_ids;       // Direct children (unordered)

    // Component summary
    struct ComponentMask {
        bool has_transform = false;
        bool has_mesh = false;
        bool has_light = false;
        bool has_script = false;
        bool has_audio = false;
        bool has_physics = false;
        bool has_particle = false;
    } component_mask;

    bool visible = true;                   // Visibility in viewport
    bool locked = false;                   // Locked from selection/editing
    std::vector<std::string> tags;         // User-defined tags
};

/// Full scene hierarchy snapshot.
struct SceneHierarchy {
    std::vector<HierarchyNode> nodes;      // All entities
    uint32_t root_node_id;                 // Root of the tree
};

/// Scene graph manager (C++ side).
class SceneGraph {
public:
    /// Get complete hierarchy snapshot.
    SceneHierarchy GetHierarchy() const;

    /// Get single entity info.
    HierarchyNode GetNode(uint32_t entity_id) const;

    /// Rename entity.
    void SetEntityName(uint32_t entity_id, const std::string& new_name);

    /// Reparent entity to new parent.
    /// Returns error if would create cycle (parent is descendant of entity).
    bool SetParent(uint32_t entity_id, uint32_t new_parent_id);

    /// Toggle visibility (and children if requested).
    void SetVisible(uint32_t entity_id, bool visible, bool recursive = false);

    /// Toggle locked state.
    void SetLocked(uint32_t entity_id, bool locked, bool recursive = false);

    /// Create new entity.
    uint32_t CreateEntity(const std::string& name, uint32_t parent_id = 0);

    /// Duplicate entity (and children).
    uint32_t DuplicateEntity(uint32_t entity_id, uint32_t new_parent_id);

    /// Delete entity (and children).
    void DeleteEntity(uint32_t entity_id);

    /// Add component to entity.
    void AddComponent(uint32_t entity_id, std::string_view component_type);
};
```

## Hierarchy Panel Component (TypeScript)

```typescript
/// Manages scene tree view with virtual scrolling, selection, and drag-drop.
export class HierarchyPanel {
    private sceneGraph: HierarchyNode[] = [];
    private selectedIds: Set<uint32_t> = new Set();
    private expandedIds: Set<uint32_t> = new Set();
    private virtualScroller: VirtualScroller;
    private renameField: HTMLInputElement | null = null;
    private renamingId: uint32_t | null = null;

    constructor(
        private container: HTMLElement,
        private ipc: IPC,
        private vfxContext: ViewportContext
    ) {
        this.setupUI();
        this.attachEventListeners();
        this.loadSceneGraph();
    }

    /// Load hierarchy from C++ runtime.
    async loadSceneGraph() {
        const hierarchy = await this.ipc.call("editor.get_hierarchy", []);
        this.sceneGraph = hierarchy.nodes;
        this.render();

        // Subscribe to hierarchy changes
        this.ipc.on("hierarchy.changed", () => {
            this.loadSceneGraph();
        });
    }

    /// Render visible rows using virtual scroller.
    private render() {
        const flatList = this.flattenHierarchy();

        this.virtualScroller.setItems(flatList, (item, index) => {
            return this.createEntityRow(item);
        });
    }

    /// Flatten tree into single list (respecting expansion state).
    private flattenHierarchy(): HierarchyNode[] {
        const flat: HierarchyNode[] = [];
        const root = this.sceneGraph.find(n => n.parent_id === 0);

        const traverse = (nodeId: uint32_t, depth: number) => {
            const node = this.sceneGraph.find(n => n.entity_id === nodeId);
            if (!node) return;

            flat.push({ ...node, depth });

            // Add children if expanded
            if (this.expandedIds.has(nodeId)) {
                for (const childId of node.child_ids) {
                    traverse(childId, depth + 1);
                }
            }
        };

        if (root) traverse(root.entity_id, 0);
        return flat;
    }

    private createEntityRow(node: HierarchyNode): HTMLElement {
        const row = document.createElement('div');
        row.className = 'hierarchy-row';
        row.dataset.entityId = String(node.entity_id);
        if (this.selectedIds.has(node.entity_id)) {
            row.classList.add('selected');
        }

        // Indent based on depth
        const indent = document.createElement('div');
        indent.className = 'hierarchy-indent';
        indent.style.marginLeft = `${node.depth * 20}px`;
        row.appendChild(indent);

        // Expand arrow (if has children)
        if (node.child_ids.length > 0) {
            const arrow = document.createElement('button');
            arrow.className = 'expand-arrow';
            if (this.expandedIds.has(node.entity_id)) {
                arrow.classList.add('expanded');
            }
            arrow.onclick = (e) => {
                e.stopPropagation();
                this.toggleExpanded(node.entity_id);
            };
            indent.appendChild(arrow);
        } else {
            const spacer = document.createElement('div');
            spacer.style.width = '16px';
            indent.appendChild(spacer);
        }

        // Component icon (visual indicator of node type)
        const icon = document.createElement('img');
        icon.className = 'component-icon';
        icon.src = this.getComponentIcon(node);
        icon.title = this.getComponentTooltip(node);
        indent.appendChild(icon);

        // Entity name (editable)
        const nameLabel = document.createElement('span');
        nameLabel.className = 'entity-name';
        nameLabel.textContent = node.name;
        nameLabel.ondblclick = () => {
            this.startRename(node.entity_id, nameLabel);
        };
        indent.appendChild(nameLabel);

        // Visibility toggle
        const visButton = document.createElement('button');
        visButton.className = 'visibility-toggle';
        visButton.setAttribute('aria-label', 'Toggle visibility');
        if (!node.visible) {
            visButton.classList.add('hidden');
        }
        visButton.onclick = (e) => {
            e.stopPropagation();
            this.toggleVisibility(node.entity_id, !node.visible);
        };
        row.appendChild(visButton);

        // Lock toggle
        const lockButton = document.createElement('button');
        lockButton.className = 'lock-toggle';
        lockButton.setAttribute('aria-label', 'Toggle lock');
        if (node.locked) {
            lockButton.classList.add('locked');
        }
        lockButton.onclick = (e) => {
            e.stopPropagation();
            this.toggleLocked(node.entity_id, !node.locked);
        };
        row.appendChild(lockButton);

        // Event handlers
        row.onclick = (e) => {
            e.stopPropagation();
            this.selectEntity(node.entity_id, e);
        };

        row.oncontextmenu = (e) => {
            e.preventDefault();
            this.showContextMenu(node.entity_id, e);
        };

        // Drag-drop for reparenting
        row.draggable = true;
        row.ondragstart = (e) => {
            e.dataTransfer!.effectAllowed = 'move';
            e.dataTransfer!.setData('text/entity-id', String(node.entity_id));
        };
        row.ondragover = (e) => {
            e.preventDefault();
            e.dataTransfer!.dropEffect = 'move';
            row.classList.add('drag-over');
        };
        row.ondragleave = () => {
            row.classList.remove('drag-over');
        };
        row.ondrop = (e) => {
            e.preventDefault();
            row.classList.remove('drag-over');
            const sourceId = parseInt(e.dataTransfer!.getData('text/entity-id'));
            this.reparentEntity(sourceId, node.entity_id);
        };

        return row;
    }

    private selectEntity(entityId: uint32_t, event: MouseEvent) {
        if (event.ctrlKey) {
            // Toggle selection
            if (this.selectedIds.has(entityId)) {
                this.selectedIds.delete(entityId);
            } else {
                this.selectedIds.add(entityId);
            }
        } else if (event.shiftKey && this.selectedIds.size > 0) {
            // Range select (between last selected and this)
            const flatList = this.flattenHierarchy();
            const lastId = Array.from(this.selectedIds).pop()!;
            const startIdx = flatList.findIndex(n => n.entity_id === lastId);
            const endIdx = flatList.findIndex(n => n.entity_id === entityId);
            const [min, max] = startIdx < endIdx ? [startIdx, endIdx] : [endIdx, startIdx];
            for (let i = min; i <= max; i++) {
                this.selectedIds.add(flatList[i].entity_id);
            }
        } else {
            // Single select
            this.selectedIds.clear();
            this.selectedIds.add(entityId);
        }

        this.notifySelectionChanged();
        this.render();
    }

    private toggleExpanded(entityId: uint32_t) {
        if (this.expandedIds.has(entityId)) {
            this.expandedIds.delete(entityId);
        } else {
            this.expandedIds.add(entityId);
        }
        this.render();
    }

    private toggleVisibility(entityId: uint32_t, visible: boolean) {
        this.ipc.call("editor.set_visible", [entityId, visible]);
    }

    private toggleLocked(entityId: uint32_t, locked: boolean) {
        this.ipc.call("editor.set_locked", [entityId, locked]);
    }

    private startRename(entityId: uint32_t, label: HTMLElement) {
        const node = this.sceneGraph.find(n => n.entity_id === entityId)!;

        const input = document.createElement('input');
        input.type = 'text';
        input.className = 'rename-input';
        input.value = node.name;

        const finishRename = async (newName: string) => {
            if (newName && newName !== node.name) {
                await this.ipc.call("editor.rename_entity", [entityId, newName]);
            }
            this.renamingId = null;
            this.loadSceneGraph();
        };

        input.onblur = () => finishRename(input.value);
        input.onkeydown = (e) => {
            if (e.key === 'Enter') finishRename(input.value);
            if (e.key === 'Escape') this.loadSceneGraph();
        };

        label.replaceWith(input);
        input.focus();
        input.select();
        this.renamingId = entityId;
    }

    private reparentEntity(entityId: uint32_t, newParentId: uint32_t) {
        if (entityId === newParentId) return; // Can't parent to self

        this.ipc.call("editor.set_parent", [entityId, newParentId])
            .catch(err => console.error('Reparent failed:', err));
    }

    private showContextMenu(entityId: uint32_t, event: MouseEvent) {
        const menu = document.createElement('div');
        menu.className = 'context-menu';
        menu.style.left = event.clientX + 'px';
        menu.style.top = event.clientY + 'px';

        const items = [
            { label: 'Create Entity', action: () => this.createEntity(entityId) },
            { label: 'Duplicate', action: () => this.duplicateEntity(entityId) },
            { label: 'Delete', action: () => this.deleteEntity(entityId) },
            { label: 'Rename', action: () => {
                const row = document.querySelector(`[data-entity-id="${entityId}"] .entity-name`);
                if (row) this.startRename(entityId, row as HTMLElement);
            }},
            { divider: true },
            { label: 'Add Component', action: () => this.showAddComponentMenu(entityId) },
        ];

        for (const item of items) {
            if (item.divider) {
                menu.appendChild(document.createElement('hr'));
            } else {
                const btn = document.createElement('button');
                btn.textContent = item.label!;
                btn.onclick = () => {
                    item.action!();
                    document.body.removeChild(menu);
                };
                menu.appendChild(btn);
            }
        }

        document.body.appendChild(menu);

        // Close menu on click outside
        const closeMenu = () => {
            if (document.body.contains(menu)) {
                document.body.removeChild(menu);
            }
            document.removeEventListener('click', closeMenu);
        };
        document.addEventListener('click', closeMenu);
    }

    private async createEntity(parentId: uint32_t) {
        const name = prompt('Entity name:');
        if (name) {
            const newId = await this.ipc.call("editor.create_entity", [name, parentId]);
            this.loadSceneGraph();
            this.selectEntity(newId, new MouseEvent('click'));
        }
    }

    private async duplicateEntity(entityId: uint32_t) {
        const node = this.sceneGraph.find(n => n.entity_id === entityId)!;
        const newId = await this.ipc.call("editor.duplicate_entity", [
            entityId,
            node.parent_id
        ]);
        this.loadSceneGraph();
        this.selectEntity(newId, new MouseEvent('click'));
    }

    private async deleteEntity(entityId: uint32_t) {
        if (confirm('Delete entity and its children?')) {
            await this.ipc.call("editor.delete_entity", [entityId]);
            this.loadSceneGraph();
        }
    }

    private showAddComponentMenu(entityId: uint32_t) {
        // Delegate to component palette (similar to context menu)
        console.log('Add component for:', entityId);
    }

    private notifySelectionChanged() {
        // Emit event so Inspector panel and Viewport update
        document.dispatchEvent(new CustomEvent('hierarchy-selection-changed', {
            detail: { selectedIds: Array.from(this.selectedIds) }
        }));
    }

    private getComponentIcon(node: HierarchyNode): string {
        const { component_mask } = node;
        if (component_mask.has_script) return 'icon-script.png';
        if (component_mask.has_light) return 'icon-light.png';
        if (component_mask.has_mesh) return 'icon-mesh.png';
        if (component_mask.has_physics) return 'icon-physics.png';
        if (component_mask.has_audio) return 'icon-audio.png';
        if (component_mask.has_particle) return 'icon-particle.png';
        return 'icon-entity.png';
    }

    private getComponentTooltip(node: HierarchyNode): string {
        const { component_mask } = node;
        const components = [];
        if (component_mask.has_transform) components.push('Transform');
        if (component_mask.has_mesh) components.push('Mesh');
        if (component_mask.has_light) components.push('Light');
        if (component_mask.has_script) components.push('Script');
        if (component_mask.has_audio) components.push('Audio');
        if (component_mask.has_physics) components.push('Physics');
        if (component_mask.has_particle) components.push('Particle');
        return components.join(', ');
    }
}
```

## Search and Filter

The Hierarchy panel includes a search bar to quickly locate entities:

```typescript
private setupSearch() {
    const searchInput = document.createElement('input');
    searchInput.type = 'text';
    searchInput.placeholder = 'Search entities...';
    searchInput.className = 'hierarchy-search';

    searchInput.oninput = () => {
        this.filterHierarchy(searchInput.value);
    };

    this.container.prepend(searchInput);
}

private filterHierarchy(query: string) {
    const query_lower = query.toLowerCase();

    // Show all ancestors of matching nodes
    const matching = new Set<uint32_t>();

    for (const node of this.sceneGraph) {
        if (node.name.toLowerCase().includes(query_lower)) {
            matching.add(node.entity_id);

            // Mark all ancestors as visible
            let current = node;
            while (current.parent_id !== 0) {
                current = this.sceneGraph.find(n => n.entity_id === current.parent_id)!;
                matching.add(current.entity_id);
                this.expandedIds.add(current.entity_id);
            }
        }
    }

    // Filter visible rows
    this.visibleFilter = matching;
    this.render();
}
```

## CSS Styling

```css
.hierarchy-panel {
    display: flex;
    flex-direction: column;
    height: 100%;
    background: #252526;
    color: #cccccc;
}

.hierarchy-search {
    padding: 6px 8px;
    margin: 8px;
    background: #3e3e42;
    border: 1px solid #444;
    color: #cccccc;
    border-radius: 2px;
}

.hierarchy-scroller {
    flex: 1;
    overflow-y: auto;
    overflow-x: hidden;
}

.hierarchy-row {
    display: flex;
    align-items: center;
    height: 24px;
    padding: 0 4px;
    user-select: none;
    cursor: pointer;
    gap: 4px;
}

.hierarchy-row:hover {
    background: #3e3e42;
}

.hierarchy-row.selected {
    background: #094771;
}

.hierarchy-row.drag-over {
    border-bottom: 2px solid #0e639c;
}

.hierarchy-indent {
    display: flex;
    align-items: center;
    gap: 2px;
    flex: 1;
    min-width: 0;
}

.expand-arrow {
    width: 16px;
    height: 16px;
    padding: 0;
    border: none;
    background: none;
    cursor: pointer;
    color: #969696;
}

.expand-arrow::before {
    content: '▶';
    display: block;
    font-size: 10px;
}

.expand-arrow.expanded::before {
    content: '▼';
}

.component-icon {
    width: 16px;
    height: 16px;
    flex-shrink: 0;
}

.entity-name {
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    flex: 1;
}

.rename-input {
    flex: 1;
    padding: 2px 4px;
    background: #3e3e42;
    border: 1px solid #0e639c;
    color: #cccccc;
    border-radius: 2px;
}

.visibility-toggle,
.lock-toggle {
    width: 20px;
    height: 20px;
    padding: 0;
    border: none;
    background: none;
    cursor: pointer;
    opacity: 0.6;
}

.visibility-toggle:hover,
.lock-toggle:hover {
    opacity: 1;
}

.visibility-toggle.hidden::after {
    content: '🚫';
}

.visibility-toggle:not(.hidden)::after {
    content: '👁';
}

.lock-toggle.locked::after {
    content: '🔒';
}

.lock-toggle:not(.locked)::after {
    content: '🔓';
}

.context-menu {
    position: fixed;
    background: #252526;
    border: 1px solid #464647;
    border-radius: 2px;
    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.5);
    z-index: 10000;
    min-width: 180px;
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

| Feature | UE5 (World Outliner) | Godot (Scene Tree) | Unity Hierarchy | **UzEngine** |
|---|---|---|---|---|
| **Tree view** | ✓ Unlimited depth | ✓ Unlimited depth | ✓ Unlimited depth | **✓ Full hierarchy** |
| **Virtual scrolling** | ✗ (slower for 10k+) | ✗ (slower for 10k+) | ✗ (slower for 10k+) | **✓ Virtual scroller** |
| **Drag-drop reparenting** | ✓ (visual feedback) | ✓ (drag preview) | ✓ (drop target indicator) | **✓ All three** |
| **Multi-select** | ✓ Ctrl/Shift | ✓ Ctrl/Shift | ✓ Ctrl/Shift | **✓ Full support** |
| **Visibility toggle** | ✓ Eye icon | ✓ Eye icon | ✓ Eye icon, recursive | **✓ With recursive** |
| **Lock toggle** | ✓ Prevents selection | ✓ Prevents interaction | ✓ Prevents all | **✓ Prevents selection** |
| **Search/filter** | ✓ By name/type | ✓ By name only | ✓ By name only | **✓ By name + component type** |
| **Context menu** | ✓ Full operations | ✓ Full operations | ✓ Limited | **✓ Full operations** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Basic tree view, single-select, expand/collapse, visibility & lock toggles, rename, entity creation |
| **Phase 3** | Virtual scrolling for 10k+ entities, drag-drop reparenting, multi-select (Ctrl/Shift), context menu (Create/Duplicate/Delete/Add Component), search/filter by name, component icons |
| **Phase 4** | Advanced filtering (by component type, by tag), entity grouping (folders, layers), drag-drop into viewport (creates reference), focus camera on selected entity |

