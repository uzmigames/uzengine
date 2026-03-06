# Editor Gizmos Spec

## Overview

Gizmos are 3D visual handles rendered directly inside the viewport that let users translate, rotate, and scale selected scene nodes by dragging with the mouse. They only appear when the engine is running in `--editor` mode. All gizmo interaction logic lives in the TypeScript extension (viewport webview); the engine handles rendering the gizmo geometry and applying the resulting transforms via IPC.

---

## 1. Gizmo Types

| Mode | Visual | Hotkey |
|------|--------|--------|
| **Translate** | Three axis arrows + three axis-plane quads | W |
| **Rotate** | Three arcs (one per axis) + free-rotate outer ring | E |
| **Scale** | Three axis lines with box handles + uniform center cube | R |

The active gizmo mode is stored in the extension as `GizmoMode = 'translate' | 'rotate' | 'scale' | 'none'` and displayed in the viewport toolbar:

```
[Move W]  [Rotate E]  [Scale R]          [Local]  [World]
```

---

## 2. Rendering

### 2.1 Engine Render Pass

Gizmos are rendered by the engine in a **dedicated editor-only pass** executed after the main scene pass:

- Compile condition: `#ifdef UZ_EDITOR` — gizmo geometry and shaders are excluded from shipping builds.
- **No depth test against scene geometry** — gizmos are always visible on top of scene objects.
- **Own depth buffer** — gizmos do depth-test against each other so the back-facing arc or arrow is correctly occluded by the front-facing one.
- Rendered directly into the JPEG framebuffer before capture, so they appear in the streamed image.

### 2.2 IPC: Updating Gizmo State

The extension tells the engine which node to show the gizmo for, and what mode to use:

```jsonc
{ "fn": "editor.setGizmo", "args": { "nodeId": 5, "mode": "translate" } }
{ "fn": "editor.setGizmo", "args": { "nodeId": null, "mode": "none" } }
```

The engine renders the gizmo at the node's world position each frame. No per-frame IPC is needed for gizmo rendering — the engine keeps the state until told otherwise. The gizmo is rendered in a dedicated editor-only pass using a no-depth pipeline state, ensuring it always appears on top of scene geometry.

### 2.3 Color Scheme

| Axis / Part | Default color | Hovered color |
|-------------|--------------|---------------|
| X (right) | `#CC2222` red | `#FFFFFF` white |
| Y (up) | `#22CC22` green | `#FFFFFF` white |
| Z (forward) | `#2222CC` blue | `#FFFFFF` white |
| XY plane quad | `#CC2222` + `#22CC22` mixed | `#FFFFFF` white |
| XZ plane quad | `#CC2222` + `#2222CC` mixed | `#FFFFFF` white |
| YZ plane quad | `#22CC22` + `#2222CC` mixed | `#FFFFFF` white |
| Center / uniform | `#CCCC22` yellow | `#FFFFFF` white |
| Outer free-rotate ring | `#CCCCCC` grey | `#FFFFFF` white |

Hover state is computed on the TypeScript side (hit-test) and communicated via:

```jsonc
{ "fn": "editor.setGizmoHover", "args": { "axis": "x" } }   // "x"|"y"|"z"|"xy"|"xz"|"yz"|"center"|null
```

---

## 3. Mouse Interaction — Coordinate Math

### 3.1 Unproject: Screen Pixel → World Ray

All gizmo interaction begins by converting a mouse canvas position `(px, py)` into a world-space ray. This runs in the TypeScript viewport webview using the cached view and projection matrices received from the engine.

```typescript
function screenToWorldRay(
  px: number, py: number,
  canvasWidth: number, canvasHeight: number,
  viewMatrix: Mat4, projMatrix: Mat4
): Ray {
  // NDC coords in [-1, 1]
  const ndcX = (px / canvasWidth)  * 2 - 1;
  const ndcY = 1 - (py / canvasHeight) * 2;   // flip Y

  const invVP = mat4Invert(mat4Multiply(projMatrix, viewMatrix));

  const nearPoint = mat4TransformPoint(invVP, [ndcX, ndcY, -1]);
  const farPoint  = mat4TransformPoint(invVP, [ndcX, ndcY,  1]);

  const direction = vec3Normalize(vec3Subtract(farPoint, nearPoint));
  return { origin: nearPoint, direction };
}
```

The engine sends updated matrices every frame via the WebSocket frame header (appended as JSON after the JPEG blob, or via a parallel IPC call):

```jsonc
{ "fn": "editor.getCamera", "args": {} }
// Response: { "view": [16 floats], "proj": [16 floats], "position": [x,y,z] }
```

### 3.2 Axis Hit-Test

To determine which gizmo axis (if any) is under the cursor, a nearest-point-between-two-lines test is performed for each axis:

```typescript
function hitTestAxis(
  ray: Ray,
  gizmoOrigin: Vec3,
  axisDir: Vec3,     // normalized: [1,0,0], [0,1,0], or [0,0,1]
  hitRadius = 0.08   // world units
): boolean {
  // Parametric: P_ray = ray.origin + t * ray.dir
  //             P_axis = gizmoOrigin + s * axisDir
  // Minimize |P_ray - P_axis|
  const w0 = vec3Subtract(ray.origin, gizmoOrigin);
  const a = vec3Dot(ray.direction, ray.direction);
  const b = vec3Dot(ray.direction, axisDir);
  const c = vec3Dot(axisDir, axisDir);          // == 1
  const d = vec3Dot(ray.direction, w0);
  const e = vec3Dot(axisDir, w0);

  const denom = a * c - b * b;
  if (Math.abs(denom) < 1e-7) return false;     // parallel

  const t = (b * e - c * d) / denom;
  const s = (a * e - b * d) / denom;

  const p1 = vec3Add(ray.origin, vec3Scale(ray.direction, t));
  const p2 = vec3Add(gizmoOrigin, vec3Scale(axisDir, s));

  return vec3Length(vec3Subtract(p1, p2)) < hitRadius;
}
```

Axis-plane quads use a ray–plane intersection test (`dot(planeNormal, P - planePoint) = 0`).

### 3.3 Gizmo Scale with Distance

To keep the gizmo visually the same size regardless of camera distance, the engine scales gizmo geometry by:

```
gizmoScale = distance(cameraPos, gizmoOrigin) * 0.1
```

The TypeScript side uses the same factor when computing hit-test radii.

---

## 4. Drag Mechanics

### 4.1 Translate

```
mousedown → record dragStart ray, node's current world position
mousemove → compute new ray
         → project both rays onto the constrained axis/plane
         → delta = newProjection - startProjection
         → newPosition = startPosition + delta  (+ optional grid snap)
         → IPC: editor.setNodeTransformLive({nodeId, position: newPosition})
mouseup   → IPC: editor.commitTransform({nodeId}) → engine creates undo entry snapshot
         → Extension creates TransformCommand and pushes to UndoStack
```

Axis projection — translate along X axis:

```typescript
function projectOntoAxis(ray: Ray, axisOrigin: Vec3, axisDir: Vec3): number {
  // Closest point on axis to the ray (reuse hitTest math, return s parameter)
  ...
  return s;  // signed distance along axis from gizmoOrigin
}
```

### 4.2 Rotate (Axis-Constrained)

Rotation uses **plane-projected signed angle** (Blender-style): the mouse ray is intersected with the rotation plane (normal = constrained axis), and the signed angle between the initial and current direction vectors determines the rotation amount. This reproduces Blender's exact rotation behavior.

```
mousedown → intersect mouse ray with rotation plane (normal = worldAxis)
         → vPrev = normalize(hitPoint - pivot)
         → totalAngle = 0
mousemove → intersect current ray with rotation plane
         → vCur = normalize(hitPoint - pivot)
         → if |dot(rayDir, planeNormal)| < 0.01: freeze (skip frame)
         → frameDelta = signedAngleOnPlane(vPrev, vCur, axis)
         → if Shift held: frameDelta *= 0.1 (precision mode)
         → totalAngle += frameDelta; vPrev = vCur
         → appliedAngle = totalAngle
         → if Ctrl held: appliedAngle = snap(totalAngle, 5°)
         → if Ctrl+Shift: appliedAngle = snap(totalAngle, 1°)
         → newRotation = quatNorm(quatMul(quatFromAxisAngle(axis, appliedAngle), startRot))
         → IPC: editor.setNodeTransformLive({nodeId, rotation: newRotation})
mouseup   → IPC: editor.commitTransform({nodeId})
Escape    → revert to dragStartRotation, cancel drag
```

Key properties:
- Uses **incremental delta** (vPrev → vCur each frame) for continuous rotation beyond 360°
- **Near-parallel ray freeze**: when ray is almost parallel to rotation plane, delta is skipped to avoid wild values
- The rotation axis is always **world-space** (never the object's local axis)
- Rotation continues smoothly even if cursor moves far from the ring
- **Escape** or right-click cancels drag and reverts to original rotation

### 4.2.1 Free Rotation (Trackball / Arcball)

An outer white circle (billboarded to face camera) provides **Shoemake arcball** free rotation:

```
mousedown → project pivot to screen, compute arcball start vector
         → map cursor to virtual sphere: z = sqrt(1 - x² - y²) inside, z=0 on rim
mousemove → compute current arcball vector
         → deltaQ = quatFromVectors(startVec, curVec) in camera space
         → transform deltaQ to world space via camera basis vectors
         → newRotation = quatNorm(quatMul(worldDeltaQ, startRotation))
```

- No gimbal lock — quaternion-based throughout
- Stable near center, controlled near edge of the sphere
- Axis rings have picking priority over the free ring

### 4.3 Scale

```
mousedown → record dragStartLength = signed projection of mouse-delta onto screen axis
mousemove → recompute projection
         → scaleFactor = 1 + (currentLength - startLength) * sensitivity
         → newScale = startScale * scaleFactor  (per-axis or uniform)
         → IPC: editor.setNodeTransformLive({nodeId, scale: newScale})
mouseup   → IPC: editor.commitTransform({nodeId})
         → Extension creates TransformCommand
```

Center cube drag produces uniform scale (all three axes multiplied by `scaleFactor`).

---

## 5. Grid Snap

- Default grid size: read from `project.uzproj` `[editor] grid_size = 0.25` (metres).
- **Enabled**: hold `Ctrl` during drag.
- Snap formula: `snapped = Math.round(value / gridSize) * gridSize`
- Snap applies per axis independently for translate; to nearest 5° for rotate (Ctrl), 1° for rotate (Ctrl+Shift); to nearest 0.1 for scale.
- **Shift** (no Ctrl): precision mode — reduces rotation sensitivity by 10x.
- **Ctrl+Shift**: snap to 1-degree increments with precision sensitivity.
- The viewport toolbar shows `[Snap: 0.25m]` when snap is active.

---

## 6. Axis Constraint Modes

| User action | Constraint |
|-------------|-----------|
| Click X arrow | Constrain to X axis |
| Click Y arrow | Constrain to Y axis |
| Click Z arrow | Constrain to Z axis |
| Click XY plane quad | Constrain to XY plane |
| Click XZ plane quad | Constrain to XZ plane |
| Click YZ plane quad | Constrain to YZ plane |
| Click center cube / ring | No constraint (uniform) |
| Press X/Y/Z key mid-drag | Switch axis constraint |

Constraint state: `type GizmoConstraint = 'x' | 'y' | 'z' | 'xy' | 'xz' | 'yz' | 'free'`

---

## 7. Local vs World Space

The toolbar `[Local] / [World]` toggle controls which coordinate frame the gizmo uses:

- **World space**: gizmo arrows always point along global X/Y/Z. Transform deltas are in world space.
- **Local space**: gizmo arrows align with the node's own rotation. Transform deltas are pre-multiplied by the node's local rotation matrix.

The engine is told via:

```jsonc
{ "fn": "editor.setGizmoSpace", "args": { "space": "local" } }   // "local" | "world"
```

---

## 8. IPC Summary

| IPC Call | Direction | Description |
|----------|-----------|-------------|
| `editor.setGizmo` | Extension → Engine | Set active node and mode |
| `editor.setGizmoHover` | Extension → Engine | Highlight hovered axis |
| `editor.setGizmoSpace` | Extension → Engine | Local vs world space |
| `editor.setNodeTransformLive` | Extension → Engine | Real-time drag update (no undo) |
| `editor.commitTransform` | Extension → Engine | Drag ended; engine confirms final state |
| `editor.getCamera` | Extension → Engine | Fetch current view + proj matrices |
| `editor.pickNode` | Extension → Engine | GPU pick at screen coords |
| `editor.boxPick` | Extension → Engine | GPU pick multiple nodes in rect |

### `editor.setNodeTransformLive`

```jsonc
{
  "fn": "editor.setNodeTransformLive",
  "args": {
    "nodeId": 5,
    "position": [1.0, 0.0, 0.5],   // optional — omit axes not being dragged
    "rotation": null,
    "scale": null
  }
}
```

### `editor.commitTransform`

```jsonc
{
  "fn": "editor.commitTransform",
  "args": { "nodeId": 5 }
}
// Response: { "transform": { "position": [...], "rotation": [...], "scale": [...] } }
```

The response returns the engine's authoritative final transform (after any server-side clamping or rounding), which the extension uses as the `newValue` when constructing the `TransformCommand` for the undo stack.

### `editor.pickNode`

```jsonc
{ "fn": "editor.pickNode", "args": { "screen_x": 512, "screen_y": 300 } }
// Response: { "nodeId": 5 }  or  { "nodeId": null }  (clicked empty space)
```

The engine reads the pick buffer at the requested screen coordinate and returns the encoded node ID.

### `editor.boxPick`

```jsonc
{ "fn": "editor.boxPick", "args": { "x": 100, "y": 50, "width": 200, "height": 150 } }
// Response: { "nodeIds": [5, 12, 8] }  or  { "nodeIds": [] }  (empty selection)
```

The engine reads all pixels within the screen-space rectangle and returns a list of unique node IDs found within the selection. Useful for rectangular multi-select / box-select operations.

---

## 9. Node Picking

### 9.1 Pick Buffer

The engine maintains a dedicated **pick buffer** for GPU-accelerated node picking:

- **Format**: R8G8B8A8_UNORM, 1/4 resolution (e.g., 320×180 for a 1280×720 viewport)
- **Content**: Each pixel encodes the node ID as RGB bytes. The node ID is a 24-bit value (max 16.7 million nodes per scene).
- **Update frequency**: Updated every frame to match current scene node positions
- **Compilation**: Guarded by `#ifdef UZ_EDITOR` — excluded from shipping builds

### 9.2 Picking Workflow

- **Left-click** on viewport: `editor.pickNode` reads the pick buffer at screen coordinates; returns the nodeId (or null if empty space)
- **Ctrl+left-click**: add/remove node from multi-selection (see multi-select spec)
- **Click empty space**: deselect all
- **Picking is independent** of the active gizmo mode — it works in all modes
- **Box-select (Ctrl+drag)**: Call `editor.boxPick` with screen rect to get all nodeIds within the selection rectangle

### 9.3 Public Methods on DX12Renderer

```cpp
// Pick a single node at screen coordinates
uint32_t pickNodeAtScreen(int x, int y) const;

// Pick multiple nodes within a screen-space rectangle
std::vector<uint32_t> boxPickNodes(int x, int y, int w, int h) const;
```

Both methods are thread-safe and use the most recent pick buffer frame.

---

## 10. Multi-Select Gizmo

When multiple nodes are selected:

- Gizmo origin is placed at the **centroid** of all selected nodes' world positions: `centroid = sum(positions) / count`.
- Translate drag moves all selected nodes by the same world-space delta.
- Rotate drag (axis-constrained) applies the same incremental signed-angle delta to each node individually around the world axis (each node's rotation is updated from its own starting quaternion).
- Rotate drag (free/arcball) applies the same world-space delta quaternion to each node.
- Scale is applied relative to the centroid.
- A single `BulkPropertyCommand` is created on mouseup (see undo-redo spec).

---

## 11. Performance Constraints

| Constraint | Value |
|-----------|-------|
| `editor.setNodeTransformLive` max rate | 60 calls/sec (one per frame, throttled by `requestAnimationFrame`) |
| No undo entry during drag | Only `editor.commitTransform` triggers undo stack push |
| Pick buffer resolution | 1/4 of viewport (e.g. 320×180 for a 1280×720 viewport) |
| Gizmo vertex count per axis | < 500 triangles per gizmo component (arrow cone + shaft) |

---

## 12. Unreal / Godot Comparison

| Feature | Unreal Transform Widget | Godot 3D Gizmo | UzEngine |
|---------|------------------------|----------------|---------|
| Rendering layer | Separate HUD pass | Immediate mode, on top | Separate editor DX12 pass |
| Interaction | C++ viewport client | GDScript `EditorPlugin` | TypeScript webview + IPC |
| Snap | Editor preferences | Project settings | `project.uzproj [editor]` |
| Multi-select | Centroid pivot | Centroid pivot | Centroid pivot |
| Local/World toggle | Toolbar | Toolbar | Toolbar |
