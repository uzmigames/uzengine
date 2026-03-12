# Tool Modes — Editing Modes and Transform Tools

> **Status**: Planned (Phase 2–4)
> **Source**: `extension/editor/tool_modes.ts`, `engine/editor/tool_system.h`
> **References**: Scene Editing (§20), Viewport (§02), Gizmo System (§41), UE5 Transform Tools, Godot Tools, Blender Mode System

## Purpose

Tool Modes provide different interaction paradigms for editing the scene. Rather than a single "edit" mode, UzEngine separates concerns: Select mode for picking, Transform modes (Move/Rotate/Scale) for spatial editing, Landscape mode for terrain, Foliage mode for vegetation. Each mode has its own toolbar, gizmos, keyboard shortcuts, and cursor. Modes are switchable via toolbar buttons or hotkeys.

## Architecture

```
Tool Mode System (TypeScript)
    │
    ├─ Mode Manager (tracks active mode)
    │
    ├─ Shared systems:
    │  ├─ Gizmo renderer (handles mode-specific gizmos)
    │  ├─ Raycast picker (Select mode uses this)
    │  └─ Viewport overlay (mode feedback/status)
    │
    └─ Mode implementations:
       ├─ Select mode (raycast pick, box/lasso select, multi-select)
       ├─ Translate mode (Move gizmo, axis constraints, snap)
       ├─ Rotate mode (Rotate gizmo, angle display)
       ├─ Scale mode (Scale gizmo, uniform/per-axis)
       ├─ Paint mode (Landscape painting)
       ├─ Sculpt mode (Landscape sculpting)
       └─ Foliage mode (Instance placement/erasing)
            │
            └─ All modes call IPC to update transforms, terrain, etc.
```

## Tool Mode API (C++)

```cpp
/// Base interface for all tool modes.
class IToolMode {
public:
    virtual ~IToolMode() = default;

    /// Called when mode becomes active.
    virtual void OnActivate() {}

    /// Called when mode becomes inactive.
    virtual void OnDeactivate() {}

    /// Mouse down event (viewport space).
    virtual void OnMouseDown(const ViewportInput& input) {}

    /// Mouse move event.
    virtual void OnMouseMove(const ViewportInput& input) {}

    /// Mouse up event.
    virtual void OnMouseUp(const ViewportInput& input) {}

    /// Keyboard event.
    virtual void OnKeyDown(uint32_t key_code) {}

    /// Frame update (for continuous operations like dragging).
    virtual void OnUpdate(float delta_time) {}

    /// Render mode-specific overlays/gizmos.
    virtual void OnRender(class ImmediateContext& ctx) {}

    /// Get toolbar options (displayed in UI).
    virtual std::vector<ToolOption> GetToolOptions() const { return {}; }

    /// Get help text (displayed in status bar).
    virtual std::string GetHelpText() const { return ""; }
};

/// Tool mode enumeration.
enum class ToolMode : uint8_t {
    Select = 0,
    Translate = 1,
    Rotate = 2,
    Scale = 3,
    Paint = 4,      // Landscape
    Sculpt = 5,     // Landscape
    Foliage = 6,
};

/// Tool mode manager (singleton).
class ToolModeManager {
public:
    void SetToolMode(ToolMode mode);
    ToolMode GetToolMode() const { return current_mode_; }
    IToolMode* GetCurrentMode() { return modes_[current_mode_].get(); }

    // Dispatch input to current mode
    void OnMouseDown(const ViewportInput& input) {
        GetCurrentMode()->OnMouseDown(input);
    }
    void OnMouseMove(const ViewportInput& input) {
        GetCurrentMode()->OnMouseMove(input);
    }
    // ... etc
};
```

## Select Mode

The primary mode for picking and selecting entities.

```typescript
/// Select mode: pick entities via raycast, support multi-select and box/lasso select.
export class SelectMode implements IToolMode {
    private hoveredEntity: uint32_t | null = null;
    private boxSelectStart: [number, number] | null = null;
    private selectionBox: HTMLElement | null = null;

    onMouseDown(input: ViewportInput) {
        if (input.button === 'left') {
            // Start box select
            this.boxSelectStart = [input.x, input.y];
            this.selectionBox = document.createElement('div');
            this.selectionBox.className = 'selection-box';
            this.selectionBox.style.left = input.x + 'px';
            this.selectionBox.style.top = input.y + 'px';
            this.viewport.appendChild(this.selectionBox);
        }
    }

    onMouseMove(input: ViewportInput) {
        if (this.boxSelectStart) {
            // Update box select rect
            const [x0, y0] = this.boxSelectStart;
            const x = Math.min(x0, input.x);
            const y = Math.min(y0, input.y);
            const w = Math.abs(input.x - x0);
            const h = Math.abs(input.y - y0);

            this.selectionBox!.style.left = x + 'px';
            this.selectionBox!.style.top = y + 'px';
            this.selectionBox!.style.width = w + 'px';
            this.selectionBox!.style.height = h + 'px';
        } else {
            // Hover raycast
            this.raycastAndHoverEntity(input.x, input.y);
        }
    }

    onMouseUp(input: ViewportInput) {
        if (this.boxSelectStart) {
            // Perform box select query
            const [x0, y0] = this.boxSelectStart;
            const x = Math.min(x0, input.x);
            const y = Math.min(y0, input.y);
            const w = Math.abs(input.x - x0);
            const h = Math.abs(input.y - y0);

            if (w > 5 && h > 5) {  // Require minimum box size
                this.boxSelectQuery([x, y, w, h], input.ctrlKey);
            } else if (w <= 5 && h <= 5) {
                // Treat as single click (raycast)
                this.raycaSelectQuery(input.x, input.y, input.ctrlKey);
            }

            this.selectionBox!.remove();
            this.selectionBox = null;
            this.boxSelectStart = null;
        }
    }

    private async raycastSelectQuery(x: number, y: number, additive: boolean) {
        const ray = this.viewport.screenToRay(x, y);
        const hit = await this.ipc.call("editor.raycast", [ray]);

        if (hit && hit.entity_id) {
            if (additive) {
                // Ctrl held: add to selection
                await this.ipc.call("editor.add_to_selection", [hit.entity_id]);
            } else {
                // Replace selection
                await this.ipc.call("editor.set_selection", [[hit.entity_id]]);
            }
        } else if (!additive) {
            // Empty space: clear selection
            await this.ipc.call("editor.set_selection", [[]]);
        }
    }

    private async boxSelectQuery(rect: [number, number, number, number], additive: boolean) {
        const frustum = this.viewport.screenRectToFrustum(rect);
        const results = await this.ipc.call("editor.frustum_query", [frustum]);

        if (additive) {
            await this.ipc.call("editor.add_to_selection", results.entity_ids);
        } else {
            await this.ipc.call("editor.set_selection", [results.entity_ids]);
        }
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'toggle', label: 'Paint Select', value: false, onChange: (v) => {
                // Paint select: drag to select multiple (like brush)
            }},
            { type: 'toggle', label: 'Select Through', value: false, onChange: (v) => {
                // Select through transparent objects
            }},
        ];
    }

    getHelpText(): string {
        return 'Click to select | Ctrl+Click to multi-select | Drag to box select';
    }
}
```

## Transform Modes

Translate, Rotate, and Scale modes use gizmos (visual handles) for precise control.

```typescript
/// Translate mode: Move selected entities using transform gizmo.
export class TranslateMode implements IToolMode {
    private gizmoState: GizmoState = { axis: null, startPos: [0, 0, 0], isDragging: false };
    private transformSpace: 'local' | 'world' = 'world';

    onMouseDown(input: ViewportInput) {
        // Check which gizmo axis was clicked
        const axis = this.hitTestGizmoAxis(input.x, input.y);
        if (axis) {
            this.gizmoState.axis = axis;
            this.gizmoState.startPos = this.getSelectedEntityPosition();
            this.gizmoState.isDragging = true;
        }
    }

    onMouseMove(input: ViewportInput) {
        if (this.gizmoState.isDragging && this.gizmoState.axis) {
            // Calculate delta along gizmo axis
            const delta = this.calculateGizmoDelta(this.gizmoState.axis, input.x, input.y);

            // Apply snap if enabled
            const snappedDelta = this.applySnap(delta);

            // Update all selected entities
            this.ipc.call("editor.translate_selection", [snappedDelta, this.transformSpace]);
        }
    }

    onMouseUp(input: ViewportInput) {
        if (this.gizmoState.isDragging) {
            // Finalize transform (push to undo stack)
            this.ipc.call("editor.finalize_transform", []);
            this.gizmoState.isDragging = false;
            this.gizmoState.axis = null;
        }
    }

    onKeyDown(key: uint32_t) {
        if (key === 'X') this.gizmoState.axis = 'x';  // Constrain to X axis
        if (key === 'Y') this.gizmoState.axis = 'y';  // Constrain to Y axis
        if (key === 'Z') this.gizmoState.axis = 'z';  // Constrain to Z axis
        if (key === 'L') this.transformSpace = 'local';
        if (key === 'W') this.transformSpace = 'world';
    }

    onRender(ctx: ImmediateContext) {
        // Render move gizmo (red/green/blue arrows)
        for (const entityId of this.selectedIds) {
            const pos = this.getEntityPosition(entityId);
            this.drawMoveGizmo(ctx, pos, this.gizmoState.axis);
        }
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'radio', label: 'Space', options: ['Local', 'World'], value: 'World', onChange: (v) => {
                this.transformSpace = v.toLowerCase();
            }},
            { type: 'float', label: 'Snap (m)', min: 0, max: 10, value: 0.5, onChange: (v) => {
                this.snapDistance = v;
            }},
        ];
    }

    getHelpText(): string {
        return 'Drag gizmo to move | X/Y/Z to constrain axis | L/W to toggle space';
    }
}

/// Rotate mode: Rotate selected entities using rotation gizmo.
export class RotateMode implements IToolMode {
    private gizmoState: { axis: 'x' | 'y' | 'z' | null, startAngle: number, isDragging: boolean } = {
        axis: null, startAngle: 0, isDragging: false
    };

    onMouseDown(input: ViewportInput) {
        const axis = this.hitTestRotationGizmo(input.x, input.y);
        if (axis) {
            this.gizmoState.axis = axis;
            this.gizmoState.startAngle = this.getAngleFromMouse(input.x, input.y);
            this.gizmoState.isDragging = true;
        }
    }

    onMouseMove(input: ViewportInput) {
        if (this.gizmoState.isDragging && this.gizmoState.axis) {
            const currentAngle = this.getAngleFromMouse(input.x, input.y);
            const deltaAngle = currentAngle - this.gizmoState.startAngle;
            const snappedAngle = this.applySnapAngle(deltaAngle);

            this.ipc.call("editor.rotate_selection", [
                this.gizmoState.axis,
                snappedAngle,
                'world'
            ]);
        }
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'float', label: 'Snap (deg)', min: 0, max: 45, value: 15, onChange: (v) => {
                this.snapAngle = v;
            }},
        ];
    }
}

/// Scale mode: Scale selected entities.
export class ScaleMode implements IToolMode {
    private scaleMode: 'uniform' | 'per-axis' = 'uniform';
    private gizmoState: { axis: 'xyz' | 'x' | 'y' | 'z' | null, isDragging: boolean } = {
        axis: null, isDragging: false
    };

    onMouseDown(input: ViewportInput) {
        const axis = this.hitTestScaleGizmo(input.x, input.y);
        if (axis) {
            this.gizmoState.axis = axis;
            this.gizmoState.isDragging = true;
        }
    }

    onMouseMove(input: ViewportInput) {
        if (this.gizmoState.isDragging && this.gizmoState.axis) {
            const delta = this.getScaleDelta(input.x, input.y);
            const snappedScale = this.applyScaleSnap(delta);

            this.ipc.call("editor.scale_selection", [
                this.gizmoState.axis,
                snappedScale
            ]);
        }
    }

    onKeyDown(key: uint32_t) {
        if (key === 'U') this.scaleMode = 'uniform';
        if (key === 'A') this.scaleMode = 'per-axis';
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'radio', label: 'Scale Mode', options: ['Uniform', 'Per-Axis'], onChange: (v) => {
                this.scaleMode = v.toLowerCase().includes('uniform') ? 'uniform' : 'per-axis';
            }},
        ];
    }
}
```

## Landscape Mode

For terrain painting and sculpting (future Phase 3+).

```typescript
/// Paint mode: Paint materials onto landscape.
export class PaintMode implements IToolMode {
    private brushRadius: number = 5;
    private brushStrength: number = 1;
    private selectedMaterial: number = 0;

    onMouseMove(input: ViewportInput) {
        // Show brush preview circle
        this.drawBrushPreview(input.x, input.y, this.brushRadius);
    }

    onMouseDown(input: ViewportInput) {
        if (input.button === 'left') {
            const ray = this.viewport.screenToRay(input.x, input.y);
            this.ipc.call("editor.paint_landscape", [
                ray,
                this.brushRadius,
                this.brushStrength,
                this.selectedMaterial
            ]);
        }
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'float', label: 'Brush Radius', min: 1, max: 100, value: this.brushRadius, onChange: (v) => {
                this.brushRadius = v;
            }},
            { type: 'float', label: 'Strength', min: 0.1, max: 2, value: this.brushStrength, onChange: (v) => {
                this.brushStrength = v;
            }},
            { type: 'int', label: 'Material Layer', min: 0, max: 8, value: this.selectedMaterial, onChange: (v) => {
                this.selectedMaterial = v;
            }},
        ];
    }

    getHelpText(): string {
        return 'Click and drag to paint | Scroll to adjust brush size | Shift for erase';
    }
}

/// Sculpt mode: Deform landscape heights.
export class SculptMode implements IToolMode {
    private brushRadius: number = 5;
    private brushStrength: number = 0.5;
    private sculptMode: 'raise' | 'lower' | 'smooth' | 'flatten' | 'erosion' = 'raise';

    onMouseMove(input: ViewportInput) {
        this.drawBrushPreview(input.x, input.y, this.brushRadius);
    }

    onMouseDown(input: ViewportInput) {
        const ray = this.viewport.screenToRay(input.x, input.y);
        this.ipc.call("editor.sculpt_landscape", [
            ray,
            this.brushRadius,
            this.brushStrength,
            this.sculptMode
        ]);
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'radio', label: 'Sculpt Type', options: ['Raise', 'Lower', 'Smooth', 'Flatten', 'Erosion'],
              onChange: (v) => {
                  this.sculptMode = v.toLowerCase() as any;
              }},
            { type: 'float', label: 'Brush Radius', min: 1, max: 100, value: this.brushRadius, onChange: (v) => {
                this.brushRadius = v;
            }},
            { type: 'float', label: 'Strength', min: 0, max: 2, value: this.brushStrength, onChange: (v) => {
                this.brushStrength = v;
            }},
        ];
    }
}
```

## Foliage Mode

For placing and erasing vegetation instances.

```typescript
/// Foliage mode: Paint instances (trees, bushes) onto landscape.
export class FoliageMode implements IToolMode {
    private foliageType: string = '';  // Mesh to place
    private brushRadius: number = 10;
    private paintDensity: number = 1;

    onMouseMove(input: ViewportInput) {
        this.drawBrushPreview(input.x, input.y, this.brushRadius);
    }

    onMouseDown(input: ViewportInput) {
        const ray = this.viewport.screenToRay(input.x, input.y);

        if (input.button === 'left') {
            this.ipc.call("editor.paint_foliage", [
                ray,
                this.brushRadius,
                this.paintDensity,
                this.foliageType
            ]);
        } else if (input.button === 'right') {
            // Erase foliage
            this.ipc.call("editor.erase_foliage", [ray, this.brushRadius]);
        }
    }

    onKeyDown(key: uint32_t) {
        // Hold Shift to temporarily switch to erase mode
        if (key === 'Shift') this.eraseMode = true;
    }

    getToolOptions(): ToolOption[] {
        return [
            { type: 'asset-picker', label: 'Foliage Mesh', filter: '*.gltf', onChange: (path) => {
                this.foliageType = path;
            }},
            { type: 'float', label: 'Brush Radius', min: 1, max: 200, value: this.brushRadius, onChange: (v) => {
                this.brushRadius = v;
            }},
            { type: 'float', label: 'Paint Density', min: 0.1, max: 5, value: this.paintDensity, onChange: (v) => {
                this.paintDensity = v;
            }},
        ];
    }

    getHelpText(): string {
        return 'Left-click to paint foliage | Right-click to erase | Scroll to adjust brush';
    }
}
```

## Tool Mode Toolbar

```typescript
/// Toolbar for switching between tool modes.
export class ToolModeToolbar {
    private modeButtons: Map<ToolMode, HTMLButtonElement> = new Map();

    constructor(private container: HTMLElement, private ipc: IPC) {
        this.setupToolbar();
    }

    private setupToolbar() {
        const modes = [
            { mode: ToolMode.Select, label: '↪', hotkey: 'Q', tooltip: 'Select (Q)' },
            { mode: ToolMode.Translate, label: '→', hotkey: 'W', tooltip: 'Move (W)' },
            { mode: ToolMode.Rotate, label: '⟲', hotkey: 'E', tooltip: 'Rotate (E)' },
            { mode: ToolMode.Scale, label: '⬚', hotkey: 'R', tooltip: 'Scale (R)' },
            // Phase 3+
            { mode: ToolMode.Paint, label: '🖌', hotkey: 'P', tooltip: 'Paint Landscape (P)' },
            { mode: ToolMode.Sculpt, label: '⛏', hotkey: 'S', tooltip: 'Sculpt (S)' },
            { mode: ToolMode.Foliage, label: '🌳', hotkey: 'F', tooltip: 'Foliage (F)' },
        ];

        for (const m of modes) {
            const btn = document.createElement('button');
            btn.className = 'tool-mode-btn';
            btn.textContent = m.label;
            btn.title = m.tooltip;
            btn.onclick = () => this.setMode(m.mode);

            // Set up keyboard shortcut
            document.addEventListener('keydown', (e) => {
                if (e.key.toLowerCase() === m.hotkey.toLowerCase()) {
                    this.setMode(m.mode);
                }
            });

            this.container.appendChild(btn);
            this.modeButtons.set(m.mode, btn);
        }

        // Initially select mode
        this.setMode(ToolMode.Select);
    }

    private async setMode(mode: ToolMode) {
        // Deselect all buttons
        for (const btn of this.modeButtons.values()) {
            btn.classList.remove('active');
        }

        // Highlight active button
        this.modeButtons.get(mode)!.classList.add('active');

        // Notify runtime
        await this.ipc.call("editor.set_tool_mode", [mode]);
    }
}
```

## CSS Styling

```css
.tool-mode-toolbar {
    display: flex;
    gap: 4px;
    padding: 4px 8px;
    background: #2d2d30;
    border: 1px solid #464647;
}

.tool-mode-btn {
    width: 32px;
    height: 32px;
    padding: 0;
    border: 1px solid #464647;
    background: #3e3e42;
    color: #cccccc;
    cursor: pointer;
    border-radius: 2px;
    font-size: 14px;
    transition: all 0.2s;
}

.tool-mode-btn:hover {
    background: #464647;
}

.tool-mode-btn.active {
    background: #094771;
    border-color: #0e639c;
}

.tool-options-panel {
    display: flex;
    gap: 8px;
    padding: 8px;
    background: #2d2d30;
    border: 1px solid #464647;
    flex-wrap: wrap;
    align-items: center;
}

.tool-option {
    display: flex;
    gap: 6px;
    align-items: center;
}

.tool-option label {
    font-size: 12px;
    white-space: nowrap;
}

.tool-option input[type="range"],
.tool-option input[type="number"],
.tool-option select {
    padding: 4px;
    background: #3e3e42;
    border: 1px solid #444;
    color: #cccccc;
    border-radius: 2px;
}

.brush-preview {
    position: absolute;
    border: 2px solid rgba(255, 255, 255, 0.5);
    border-radius: 50%;
    pointer-events: none;
}

.selection-box {
    position: absolute;
    border: 2px solid #0e639c;
    background: rgba(14, 99, 156, 0.1);
    pointer-events: none;
}

.status-text {
    padding: 4px 8px;
    background: #2d2d30;
    border-top: 1px solid #464647;
    font-size: 12px;
    color: #969696;
}
```

## Comparison

| Feature | UE5 | Godot | Blender | **UzEngine** |
|---|---|---|---|---|
| **Mode switching** | Toolbar + hotkeys | Left panel + hotkeys | Tab key + hotkey | **Toolbar + hotkeys (Q/W/E/R/P/S/F)** |
| **Transform gizmos** | ✓ Move/Rotate/Scale | ✓ Move/Rotate/Scale | ✓ Move/Rotate/Scale | **✓ All three modes** |
| **Landscape sculpting** | ✓ (separate plugin) | ✓ Built-in | ✓ Built-in | **✓ Phase 3+** |
| **Foliage painting** | ✓ (separate mode) | ✓ Built-in | ✓ (weight paint) | **✓ Phase 3+ Foliage mode** |
| **Space toggle** | ✓ Local/World | ✓ Local/Global | ✓ Local/Global/Normal | **✓ Local/World** |
| **Axis constraint** | ✓ X/Y/Z keys | ✓ X/Y/Z keys | ✓ X/Y/Z keys | **✓ X/Y/Z keys** |
| **Tool options panel** | ✓ Details panel | ✓ Tool Options | ✓ Tool Properties | **✓ Toolbar options** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | Select (raycast + box select), Translate, Rotate, Scale modes with gizmos, local/world space toggle, axis constraint (X/Y/Z), snap options |
| **Phase 3** | Paint & Sculpt modes (landscape), Foliage mode, brush preview overlay, per-mode toolbar options, status bar help text, keyboard shortcuts all modes |
| **Phase 4** | Geometry mode (vertex/edge/face edit), advanced sculpting (erosion, smooth falloff), foliage density/placement settings, custom tool mode registration |

