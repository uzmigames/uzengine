# Inspector Panel — Auto-Generated Property UI

> **Status**: Planned (Phase 2–3)
> **Source**: `extension/editor/inspector.ts`, `engine/core/property.h`
> **References**: Property System (§21), UE5 SDetailsView, Godot Inspector, Unity Inspector

## Purpose

The Inspector panel displays all properties of a selected entity and its components, allowing real-time editing. Unlike manual UI code for each component, the Inspector auto-generates from the Property System (§21) metadata. This reduces boilerplate and ensures consistency.

## Architecture

```
Inspector Panel (TypeScript)
    │
    ├─ Listen for selection change event
    │
    └─ Fetch entity + components from runtime
        │
        ├─ For each component:
        │  └─ Get Property::Descriptor metadata
        │
        └─ Render property grid
            ├─ Bool → Checkbox
            ├─ Int → Number input with spinner
            ├─ Float → Slider or text input
            ├─ String → Text box
            ├─ Enum → Dropdown
            ├─ Color → Color picker
            ├─ Vec3 → 3x Float inputs
            ├─ Entity reference → Entity picker + search
            └─ Asset path → Asset browser picker
            │
            └─ On value change: call IPC to set property on C++ side
                │
                └─ C++ PropertyCommand created, pushed to UndoStack
```

## Property Descriptors

The Property System (§21) exposes metadata for each property:

```cpp
/// Describes a single property of a component.
struct Property::Descriptor {
    std::string name;                   // "speed", "color", "mesh_path"
    std::string display_name;           // "Speed (m/s)" (human-readable)
    std::string description;            // Tooltip text
    Property::Type type;                // Int, Float, Bool, String, Enum, Color, Vec3, Entity, Path
    void* default_value;                // Default value pointer

    // Type-specific constraints
    struct {
        int32_t min = INT32_MIN, max = INT32_MAX;  // For Int
        float fmin = -3.4e38f, fmax = 3.4e38f;     // For Float
        std::vector<std::string> enum_values;      // For Enum
        std::string asset_filter;                  // For Path ("*.gltf", "*.uzshader")
    } constraints;

    bool read_only = false;             // Disable editing
    bool hidden = false;                // Don't show in inspector
    std::string category = "General";   // Group properties by category
};
```

## Inspector Component (TypeScript)

```typescript
/// Displays properties of selected entity.
export class InspectorPanel {
    private currentEntity: Entity | null = null;
    private components: Map<string, PropertyDescriptor[]> = new Map();
    private propertyEditors: Map<string, PropertyEditor> = new Map();

    constructor(private container: HTMLElement, private ipc: IPC) {
        this.setupUI();
        this.attachEventListeners();
    }

    /// Called when user selects a different entity.
    async setEntity(entity: Entity | null) {
        this.currentEntity = entity;
        this.propertyEditors.clear();

        if (!entity) {
            this.container.innerHTML = '<p>No entity selected</p>';
            return;
        }

        // Fetch entity metadata
        const metadata = await this.ipc.call("editor.get_entity_metadata", [entity.id]);
        this.components = new Map(
            Object.entries(metadata.components).map(([name, props]: [string, any]) => [
                name,
                props as PropertyDescriptor[]
            ])
        );

        this.render();
    }

    private render() {
        this.container.innerHTML = '';

        if (!this.currentEntity) return;

        // Render entity name at top
        const nameGroup = this.createPropertyGroup("Entity", [
            { name: "name", display_name: "Name", type: "String", value: this.currentEntity.name }
        ]);
        this.container.appendChild(nameGroup);

        // Render components grouped by type
        for (const [componentName, properties] of this.components) {
            const group = this.createPropertyGroup(componentName, properties);
            this.container.appendChild(group);
        }
    }

    private createPropertyGroup(
        componentName: string,
        properties: PropertyDescriptor[]
    ): HTMLElement {
        const group = document.createElement('div');
        group.className = 'property-group';

        // Component header (collapsible)
        const header = document.createElement('div');
        header.className = 'group-header';
        header.textContent = componentName;
        header.onclick = () => {
            group.classList.toggle('collapsed');
        };
        group.appendChild(header);

        // Properties
        const content = document.createElement('div');
        content.className = 'group-content';
        for (const prop of properties) {
            if (prop.hidden) continue;

            const row = this.createPropertyRow(componentName, prop);
            content.appendChild(row);
        }
        group.appendChild(content);

        return group;
    }

    private createPropertyRow(componentName: string, descriptor: PropertyDescriptor): HTMLElement {
        const row = document.createElement('div');
        row.className = 'property-row';

        // Label
        const label = document.createElement('label');
        label.textContent = descriptor.display_name || descriptor.name;
        label.title = descriptor.description || '';
        row.appendChild(label);

        // Editor widget (varies by type)
        const editor = this.createPropertyEditor(componentName, descriptor);
        row.appendChild(editor);

        return row;
    }

    private createPropertyEditor(componentName: string, prop: PropertyDescriptor): HTMLElement {
        const editor = document.createElement('div');
        editor.className = 'property-editor';

        const key = `${componentName}.${prop.name}`;

        switch (prop.type) {
            case 'Bool':
                return this.createBoolEditor(key, prop);
            case 'Int':
                return this.createIntEditor(key, prop);
            case 'Float':
                return this.createFloatEditor(key, prop);
            case 'String':
                return this.createStringEditor(key, prop);
            case 'Color':
                return this.createColorEditor(key, prop);
            case 'Vec3':
                return this.createVec3Editor(key, prop);
            case 'Enum':
                return this.createEnumEditor(key, prop);
            case 'Entity':
                return this.createEntityEditor(key, prop);
            case 'Path':
                return this.createAssetEditor(key, prop);
            default:
                const unknown = document.createElement('span');
                unknown.textContent = '(Unsupported type)';
                return unknown;
        }
    }

    private createBoolEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.disabled = prop.read_only || false;
        checkbox.checked = prop.value || false;

        checkbox.onchange = () => {
            this.setProperty(key, checkbox.checked);
        };

        return checkbox;
    }

    private createIntEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'number-editor';

        const input = document.createElement('input');
        input.type = 'number';
        input.step = '1';
        input.disabled = prop.read_only || false;
        input.value = String(prop.value || 0);

        if (prop.constraints?.min !== undefined) {
            input.min = String(prop.constraints.min);
        }
        if (prop.constraints?.max !== undefined) {
            input.max = String(prop.constraints.max);
        }

        input.onchange = () => {
            const value = parseInt(input.value, 10);
            this.setProperty(key, value);
        };

        container.appendChild(input);
        return container;
    }

    private createFloatEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'slider-editor';

        const input = document.createElement('input');
        input.type = 'range';
        input.step = '0.01';
        input.disabled = prop.read_only || false;
        input.value = String(prop.value || 0);

        if (prop.constraints?.fmin !== undefined) {
            input.min = String(prop.constraints.fmin);
        }
        if (prop.constraints?.fmax !== undefined) {
            input.max = String(prop.constraints.fmax);
        }

        const textInput = document.createElement('input');
        textInput.type = 'number';
        textInput.step = '0.01';
        textInput.disabled = prop.read_only || false;
        textInput.value = String(prop.value || 0);

        // Sync slider ↔ text
        input.oninput = () => {
            textInput.value = input.value;
            this.setProperty(key, parseFloat(input.value));
        };

        textInput.onchange = () => {
            input.value = textInput.value;
            this.setProperty(key, parseFloat(textInput.value));
        };

        container.appendChild(input);
        container.appendChild(textInput);
        return container;
    }

    private createStringEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const input = document.createElement('input');
        input.type = 'text';
        input.disabled = prop.read_only || false;
        input.value = String(prop.value || '');

        input.onchange = () => {
            this.setProperty(key, input.value);
        };

        return input;
    }

    private createColorEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'color-editor';

        const colorInput = document.createElement('input');
        colorInput.type = 'color';
        colorInput.disabled = prop.read_only || false;
        // Convert RGBA to hex
        colorInput.value = this.colorToHex(prop.value);

        colorInput.onchange = () => {
            const rgba = this.hexToColor(colorInput.value);
            this.setProperty(key, rgba);
        };

        container.appendChild(colorInput);

        // Also show RGBA values
        const rgbaText = document.createElement('span');
        rgbaText.textContent = this.colorToString(prop.value);
        container.appendChild(rgbaText);

        return container;
    }

    private createVec3Editor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'vec3-editor';

        const [x, y, z] = prop.value || [0, 0, 0];

        const createAxis = (axis: string, value: number, index: number) => {
            const label = document.createElement('label');
            label.textContent = axis;
            const input = document.createElement('input');
            input.type = 'number';
            input.step = '0.1';
            input.value = String(value);
            input.disabled = prop.read_only || false;

            input.onchange = () => {
                const vec = [x, y, z];
                vec[index] = parseFloat(input.value);
                this.setProperty(key, vec);
            };

            const row = document.createElement('div');
            row.appendChild(label);
            row.appendChild(input);
            return row;
        };

        container.appendChild(createAxis('X', x, 0));
        container.appendChild(createAxis('Y', y, 1));
        container.appendChild(createAxis('Z', z, 2));

        return container;
    }

    private createEnumEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const select = document.createElement('select');
        select.disabled = prop.read_only || false;

        for (const option of prop.constraints?.enum_values || []) {
            const opt = document.createElement('option');
            opt.value = option;
            opt.textContent = option;
            if (option === prop.value) opt.selected = true;
            select.appendChild(opt);
        }

        select.onchange = () => {
            this.setProperty(key, select.value);
        };

        return select;
    }

    private createEntityEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'entity-picker';

        const currentLabel = document.createElement('span');
        currentLabel.textContent = prop.value?.name || '(None)';
        container.appendChild(currentLabel);

        const pickButton = document.createElement('button');
        pickButton.textContent = 'Pick...';
        pickButton.onclick = () => {
            this.showEntityPicker((selected) => {
                currentLabel.textContent = selected?.name || '(None)';
                this.setProperty(key, selected?.id || null);
            });
        };
        container.appendChild(pickButton);

        return container;
    }

    private createAssetEditor(key: string, prop: PropertyDescriptor): HTMLElement {
        const container = document.createElement('div');
        container.className = 'asset-picker';

        const input = document.createElement('input');
        input.type = 'text';
        input.placeholder = 'Asset path...';
        input.value = String(prop.value || '');
        input.disabled = true;  // Don't allow manual typing
        container.appendChild(input);

        const browseButton = document.createElement('button');
        browseButton.textContent = 'Browse...';
        browseButton.onclick = () => {
            this.showAssetBrowser(
                prop.constraints?.asset_filter || '*',
                (path) => {
                    input.value = path;
                    this.setProperty(key, path);
                }
            );
        };
        container.appendChild(browseButton);

        return container;
    }

    private async setProperty(key: string, value: any) {
        const [componentName, propertyName] = key.split('.');

        try {
            await this.ipc.call("editor.set_property", [
                this.currentEntity!.id,
                componentName,
                propertyName,
                value
            ]);

            // Refresh display
            this.setEntity(this.currentEntity);
        } catch (err) {
            console.error('Failed to set property:', err);
        }
    }

    // Helper methods (color conversion, entity picker, etc.)
    private colorToHex(color: [number, number, number, number]): string {
        // RGBA to hex
        return '#' + color.slice(0, 3).map(c => {
            const hex = (c * 255).toString(16);
            return hex.length === 1 ? '0' + hex : hex;
        }).join('');
    }

    private hexToColor(hex: string): [number, number, number, number] {
        // Hex to RGBA
        const r = parseInt(hex.slice(1, 3), 16) / 255;
        const g = parseInt(hex.slice(3, 5), 16) / 255;
        const b = parseInt(hex.slice(5, 7), 16) / 255;
        return [r, g, b, 1.0];
    }

    private colorToString(color: [number, number, number, number]): string {
        return `RGBA(${(color[0]*255).toFixed(0)}, ${(color[1]*255).toFixed(0)}, ${(color[2]*255).toFixed(0)}, ${color[3].toFixed(2)})`;
    }

    private showEntityPicker(onSelected: (entity: Entity | null) => void) {
        // Modal dialog to select entity from scene hierarchy
        console.log('Entity picker not implemented');
    }

    private showAssetBrowser(filter: string, onSelected: (path: string) => void) {
        // Modal dialog to browse assets matching filter
        console.log('Asset browser not implemented');
    }
}
```

## Custom Property Editors

For specialized property types, games can register custom drawers:

```typescript
/// Register custom editor for specific property type or component type.
export class PropertyEditorRegistry {
    private editors: Map<string, PropertyEditorFactory> = new Map();

    /// Register a factory for custom editor type.
    register(typename: string, factory: PropertyEditorFactory) {
        this.editors.set(typename, factory);
    }

    /// Lookup custom editor, or default to generic editor.
    getEditor(typename: string): PropertyEditorFactory | undefined {
        return this.editors.get(typename);
    }
}

// Example: custom editor for damage values (with damage type icons)
class DamagePropertyEditor implements PropertyEditor {
    constructor(private descriptor: PropertyDescriptor) {}

    render(): HTMLElement {
        const container = document.createElement('div');
        container.className = 'damage-editor';

        // Show damage type icon
        const typeIcon = document.createElement('img');
        typeIcon.src = `content/ui/damage_${this.descriptor.metadata.damage_type}.png`;
        container.appendChild(typeIcon);

        // Slider for damage amount
        const slider = document.createElement('input');
        slider.type = 'range';
        slider.value = String(this.descriptor.value);
        container.appendChild(slider);

        return container;
    }
}
```

## CSS Styling

```css
.property-group {
    margin: 8px 0;
    border: 1px solid #333;
    border-radius: 4px;
}

.group-header {
    padding: 8px 12px;
    background: #2a2a2a;
    font-weight: bold;
    cursor: pointer;
    user-select: none;
}

.group-header:hover {
    background: #353535;
}

.group-content {
    padding: 8px;
}

.property-row {
    display: grid;
    grid-template-columns: 150px 1fr;
    gap: 8px;
    align-items: center;
    padding: 4px 0;
    border-bottom: 1px solid #222;
}

.property-row:last-child {
    border-bottom: none;
}

.property-row label {
    font-size: 12px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.property-editor input[type="text"],
.property-editor input[type="number"],
.property-editor select {
    width: 100%;
    padding: 4px;
    background: #1e1e1e;
    border: 1px solid #444;
    color: #ccc;
    border-radius: 2px;
}

.slider-editor {
    display: flex;
    gap: 8px;
}

.slider-editor input[type="range"] {
    flex: 1;
}

.slider-editor input[type="number"] {
    width: 60px;
}

.vec3-editor {
    display: flex;
    gap: 4px;
}

.vec3-editor > div {
    flex: 1;
    display: flex;
    gap: 4px;
}

.vec3-editor label {
    width: 20px;
    text-align: center;
}
```

## Comparison

| Feature | UE5 (SDetailsView) | Godot (Inspector) | Unity | **UzEngine** |
|---|---|---|---|---|
| **Auto-generation** | From UClass properties | From @export vars | From Serializable fields | **From Property::Descriptor** |
| **Custom editors** | TPropertyTypeCustomizationBase | EditorProperty subclass | PropertyDrawer | **PropertyEditorFactory** |
| **Real-time updates** | Tick-based reflection | Notification system | OnValidate + Inspector update | **IPC callback** |
| **Groups/Categories** | EditAnywhere, Advanced | @export groups | [SerializeField] categories | **Property::category** |
| **Constraints** | Min/Max via FNumericProperty | Hints (@export_range) | Range attribute | **Property::constraints** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Basic property grid (Bool, Int, Float, String), category grouping, component headers |
| **Phase 3** | Color picker, Vec3 editor, Enum dropdown, Entity picker, Asset browser integration, custom property editors |
| **Phase 4** | Multi-select editing (edit properties of all selected entities), property presets, property inheritance (parent component overrides), diff view (compare two entities) |

