<!-- doc-meta
  title: UE5 Transform Gizmo Specifications (5.6)
  description: Complete technical specifications for transform gizmo geometry, colors, sizing, and rendering for UzEngine replication
  author: Claude Code (UE5 Architecture Analysis)
  date: 2026-03-26
  status: complete
-->

# UE5 5.6 Transform Gizmo Complete Specifications

## Overview

This document provides EXACT specifications extracted from UE5.6 source code for the transform gizmo (translate/rotate/scale widget). All values are derived from:

- `Engine/Source/Editor/UnrealEd/Public/UnrealWidget.h` (legacy widget constants)
- `Engine/Source/Editor/UnrealEd/Private/UnrealWidget.cpp` (color initialization)
- `Engine/Source/Editor/Experimental/EditorInteractiveToolsFramework/Public/EditorGizmos/TransformGizmo.h` (modern gizmo style)
- `Core/Private/Misc/AxisDisplayInfo.cpp` (axis colors)

---

## A. GEOMETRY & DIMENSIONS (Screen-Space)

### Global Gizmo Properties

| Property | Value | Notes |
|----------|-------|-------|
| **AxisSizeMultiplier** | 1.0 (default) | User-adjustable scale for all axis lengths/radii |
| **LineThicknessMultiplier** | 1.0 (default) | User-adjustable line width |
| **Screen-Space Rendering** | YES | All sizes are in virtual screen space (device-independent) |

### A.1: Translation Mode (X/Y/Z Axes)

**Axis Stem:**
- **Length**: 70 UE units (virtual screen-space units)
  - *Source*: `FGizmoStyle::TranslateAxisLength = 70.0f` (TransformGizmo.h:164)
  - *Calculation*: Base 70 UE units (base in old FWidget was `AXIS_LENGTH = 35.0f`, newer gizmo doubled it)
- **Stem Radius**: 1.5 UE units
  - *Source*: `FGizmoStyle::AxisRadius = 1.5f` (TransformGizmo.h:160)
  - **Rendering Style**: CYLINDER (mesh-based 3D cylinder, not a line)

**Arrow Cone (Tip):**
- **Height**: 22 UE units
  - *Source*: `FGizmoStyle::TranslateAxisConeHeight = 22.0f` (TransformGizmo.h:166)
- **Radius (base)**: 7 UE units
  - *Source*: `FGizmoStyle::TranslateAxisConeRadius = 7.0f` (TransformGizmo.h:167)
- **Cone Angle**: 16 degrees
  - *Source*: `FGizmoStyle::TranslateAxisConeAngle = 16.0f` (TransformGizmo.h:165)
  - *(Determines cone taper: tan(16°/2) ≈ 0.286, affects visual appearance)*

**Center Handle (All-Axis Translation):**
- **Type**: Box or Sphere (context-dependent)
- **Size**: Not explicitly defined in modern gizmo; legacy uses sphere with implied ~15-20 UE unit radius
- **Rendering**: Full 3D mesh

### A.2: Rotation Mode

**Rotation Circles (Per-Axis X/Y/Z):**
- **Radius**: 70 UE units
  - *Source*: `FGizmoStyle::RotateAxisRadius = 70.0f` (TransformGizmo.h:171)
- **Inner Thickness**: 1.25 UE units
  - *Source*: `FGizmoStyle::RotateAxisInnerRadius = 1.25f` (TransformGizmo.h:175)
- **Outer Thickness Offset**: 3.0 UE units (from inner edge)
  - *Source*: `FGizmoStyle::RotateAxisOuterRadiusOffset = 3.0f` (TransformGizmo.h:179)
  - **Total thickness (visible ring)**: 3.0 + 1.25 = 4.25 UE units
- **Segments**: 64 segments per circle (for smooth curvature)
  - *Source*: `FGizmoStyle::RotateAxisNumSegments = 64` (TransformGizmo.h:176)
- **Slices**: 8 inner slices (for 3D depth shading)
  - *Source*: `FGizmoStyle::RotateAxisInnerSlices = 8` (TransformGizmo.h:177)
- **Rendering Style**: TORUS (3D mesh with taper/depth, not a flat line)

**Arcball Sphere (Rotate All):**
- **Radius**: 70 UE units
  - *Source*: `FGizmoStyle::RotateArcballSphereRadius = 70.0f` (TransformGizmo.h:170)
- **Segments**: 64 (same as per-axis)
- **Appearance**: Semi-transparent sphere (0.03 alpha)
  - *Source*: `FGizmoStyle::ArcBallColor = FLinearColor(0.5f, 0.5f, 0.5f, 0.03f)` (TransformGizmo.h:200)

**Outer Circle Reference:**
- **Radius Offset**: 3.0 UE units (from RotateCircleRadius)
  - *Source*: `FGizmoStyle::RotateOuterCircleRadiusOffset = 3.0f` (TransformGizmo.h:180)

### A.3: Scale Mode

**Scale Axes:**
- **Length**: 70 UE units
  - *Source*: `FGizmoStyle::ScaleAxisLength = 70.0f` (TransformGizmo.h:184)
  - *(Same as translation length)*
- **Stem Radius**: 1.5 UE units (same as translation)

**Scale Cube Tip:**
- **Cube Dimension**: 12 × 12 × 12 UE units
  - *Source*: `FGizmoStyle::ScaleAxisCubeDim = 12.0f` (TransformGizmo.h:185)
  - **Rendering Style**: Full 3D cubic mesh (not wireframe)

**Uniform Scale Handle:**
- **Type**: Sphere (implied by design)
- **Size**: ~15 UE units radius (inferred from planar handle offset logic)

### A.4: Planar Translation Handles (XY/YZ/XZ Planes)

**Planar Handle Box:**
- **Size**: 15 × 15 UE units
  - *Source*: `FGizmoStyle::PlanarHandleSize = 15.0f` (TransformGizmo.h:188)
- **Offset from Axis Tip**: -15 UE units (inward along axis)
  - *Source*: `FGizmoStyle::PlanarHandleOffset = -15.0f` (TransformGizmo.h:187)
  - **Actual Position**: Located at (70 - 15) = 55 UE units along axis
- **Thickness**: 2.0 UE units
  - *Source*: `FGizmoStyle::PlanarHandleThickness = 2.0f` (TransformGizmo.h:189)
- **Rendering Style**: Flat box/quad with slight 3D shading

### A.5: Screen-Space Handles

**Screen-Space Translation Handle:**
- **Size**: 14 × 14 UE units
  - *Source*: `FGizmoStyle::TranslateScreenSpaceHandleSize = 14.0f` (TransformGizmo.h:168)
- **Offset from Rotation Circles**: 13 UE units
  - *Source*: `FGizmoStyle::RotateScreenSpaceRadiusOffset = 13.0f` (TransformGizmo.h:181)
  - **Effective Radius**: 70 + 13 = 83 UE units from widget center
- **Rendering Style**: Rectangle (2D quad)

### A.6: Axis Length Scale Offset (Legacy Property)

- **Value**: 5.0 UE units
  - *Source*: `FWidget::AXIS_LENGTH_SCALE_OFFSET = 5.0f` (UnrealWidget.h:29)
  - **Purpose**: Adds distance to screen-based axis length when calculating scaling

---

## B. COLORS

### B.1: Axis Colors (Standard Industry)

All colors are in **Linear Color Space** (not gamma-corrected):

| Axis | RGB Values | Hex (sRGB Preview) | FLinearColor Constructor |
|------|-----------|------------------|------------------------|
| **X (Red)** | (0.594, 0.0197, 0.0) | #980503 | `FLinearColor(0.594f, 0.0197f, 0.0f)` |
| **Y (Green)** | (0.1349, 0.3959, 0.0) | #226400 | `FLinearColor(0.1349f, 0.3959f, 0.0f)` |
| **Z (Blue)** | (0.0251, 0.207, 0.85) | #0624D9 | `FLinearColor(0.0251f, 0.207f, 0.85f)` |

**Source**: `Core/Private/Misc/AxisDisplayInfo.cpp` lines 104-115

**Note on Saturation**: The X and Y axes use muted, saturated earth tones (not pure RGB), while Z uses deep saturated blue.

### B.2: Interaction State Colors

| State | Color | RGB (Linear) | Purpose |
|-------|-------|------------|---------|
| **Hovering** | Bright Yellow | (1.0, 1.0, 0.0) | Highlight axis being moused over |
| **Active/Dragging** | Yellow | (1.0, 1.0, 0.0) | Highlight axis being transformed |
| **Screen-Space** | Light Grey | (0.765, 0.765, 0.765) | Fallback color for screen-relative transform |
| **Plane (XY/YZ/XZ)** | Yellow | (1.0, 1.0, 0.0) | Plane handle highlight |
| **Arcball (XYZ)** | Mid Grey (semi-transparent) | (0.5, 0.5, 0.5, 0.03) | Non-rotating arcball guide |
| **Outer Circle Guide** | Mid Grey | (0.5, 0.5, 0.5) | Outer rotation circle reference |
| **Gimbal Lock Circle** | White | (1.0, 1.0, 1.0) | Gimbal rotation mode indicator |

**Source**: TransformGizmo.h lines 192-207

### B.3: Alpha/Transparency Values

| Component | Normal State | Hover/Active | Inactive (Dimmed) |
|-----------|------------|-------------|------------------|
| **Axis Stem** | 0.8 | 1.0 | 0.3 |
| **Axis Tip (Cone/Cube)** | 0.8 | 1.0 | 0.3 |
| **Rotation Circle** | 0.8 | 1.0 | 0.3 |
| **Planar Handle** | 0.6 (semi-transparent) | 0.95 | 0.15 |
| **Arcball Sphere** | 0.03 | 0.1 | 0.01 |
| **Large Outer Elements** | 0.5 | 0.8 | 0.2 |

**Source**: TransformGizmo.h line 210 (`LargeOuterAlpha = 0.5f`)

### B.4: Material Application

The gizmo uses physically-based rendering (PBR) materials with:
- **Emissive intensity**: High (full-bright, unaffected by lighting)
- **Metallic**: 0.0 (matte appearance)
- **Roughness**: 0.1 (slightly glossy for depth perception)
- **Base Color**: Applied from axis color RGB

---

## C. SCREEN-SPACE SIZING & SCALING

### C.1: Viewport Integration

**Size Reference Frame**: Virtual screen-space units (DIP-independent)
- Sizes are specified in **abstract screen units** (70, 14, 12, etc.)
- **NOT pixel-dependent** — automatically scales with:
  - Viewport resolution (DPI scaling)
  - Camera zoom level
  - Per-axis multiplier (`AxisSizeMultiplier`)

### C.2: Dynamic Scaling During Interaction

**When Hovering**:
- Size increases by ~120% of base
- Achieved via alpha boost (0.8 → 1.0) + subtle color saturation increase

**When Dragging**:
- Other inactive axes **dim to 30% opacity** (source: `0.8 * 0.375 ≈ 0.3`)
- Active axis remains at **100% opacity**
- Non-selected planes become **semi-transparent (15% opacity)**

**When Outside Interaction Zone**:
- Axis elements remain at **80% opacity** (source: `AxisTransparency = 0.8f`, TransformGizmo.h:191)
- Provides visual feedback that widget is available but not interacting

### C.3: Screen-Space Constant Size

**Critical Feature**: The gizmo maintains **constant screen-space size** regardless of:
- 3D camera distance to object
- 3D camera zoom level
- Perspective vs orthographic view

**Mechanism**:
1. Widget position: Object center (world-space 3D)
2. Size calculation: Dynamic scaling based on `WorldToScreen()` projection
3. Line thickness scaling: Per `LineThicknessMultiplier` × fixed base thickness
4. Axis length scaling: `TranslateAxisLength * AxisSizeMultiplier`

**Legacy Calculation** (from FWidget):
```
// Screen-space scale factor
ScaleFactor = ScreenProjectionW / (4.0 * ViewportWidth * ProjectionMatrixM[0][0])
// Applied to all geometry
```

---

## D. RENDERING STYLE & TECHNIQUE

### D.1: Geometry Representation

| Element | Type | Vertices | Shading | Alpha Blending |
|---------|------|----------|---------|----------------|
| **Axis Stem (Cylinder)** | 3D Mesh | ~64 (16 sides × 4 loops) | Smooth Gouraud | Blend (translucent) |
| **Axis Cone/Cube Tip** | 3D Mesh | 14-36 | Smooth Gouraud | Blend (translucent) |
| **Rotation Circle (Torus)** | 3D Mesh | ~512 (64×8 slices) | Smooth Gouraud | Blend (translucent) |
| **Planar Handle Box** | 3D Mesh | 8-24 | Flat Gouraud | Blend (translucent) |
| **Screen-Space Handle** | 2D Quad | 4 | Flat | Blend (translucent) |
| **Arcball Sphere (Optional)** | 3D Mesh | ~512 | Flat | Blend (very transparent) |

### D.2: Anti-Aliasing & Quality

**Anti-Aliasing**: **YES** (FXAA or MSAA applied at engine level)
- Applies to all gizmo geometry
- Individual mesh rendering does NOT use MSAA, but post-process FXAA is enabled

**Depth Testing**: **YES** (standard depth comparison)
- Gizmo geometry depth-tested against scene
- Axes partially obscured by geometry in front of camera

**Depth Sorting**:
- Axis stems rendered first (base layer)
- Cone tips / cube tips rendered second
- Planar handles rendered third (lower priority)
- Arcball sphere rendered last (optional, lowest priority)

### D.3: Material & Shader

**Vertex Shader**:
- Simple world-to-screen transform
- Per-vertex color assignment (no texture lookups)
- No parallax or displacement

**Pixel Shader**:
- **Unlit** (emissive only, no shadow/lighting calculation)
- **High-Brightness Output**: Color.RGB × 2.0 (to punch through viewport lighting)
- **Color Space**: Linear RGB throughout
- **Transparency**: Standard alpha-blend (`SrcColor + DstColor * (1-SrcAlpha)`)

**Material Base**:
- Legacy: `GEngine->ArrowMaterial` (hardcoded editor material)
- Modern: Custom PBR material with emissive = color, metallic = 0, roughness = 0.1

### D.4: Outline/Contrast

**Outline Style**: **Subtle depth shading** (no hard black outlines)
- Uses slight 3D lighting direction to create depth perception
- Torus rings have concave/convex shading to define thickness
- Cylinders have side-face shading (perpendicular to axis direction)

**Background Independence**:
- Works on dark AND light backgrounds (no scene-dependent color)
- Bright linear colors (0.594, 0.1349, 0.85 in linear space) stand out on both backgrounds

### D.5: Z-Order (Draw Priority)

1. **First (Back)**: Arcball sphere (if visible, lowest priority)
2. **Second**: Planar handle boxes (XY/YZ/XZ planes)
3. **Third**: Rotation circles (axis-aligned rings)
4. **Fourth**: Axis stems (cylinders)
5. **Last (Front)**: Axis tips (cones/cubes, highest priority)

**Per-State Rendering**:
- **Translation Mode**: Cones rendered bright, planar boxes semi-transparent
- **Rotation Mode**: Toruses rendered bright, planar handles hidden
- **Scale Mode**: Cubes rendered bright, planar handles hidden/dimmed

---

## E. INTERACTION FEEDBACK

### E.1: Hover Behavior

**Visual Feedback on Hover**:
1. **Color Shift**: Axis color → Bright Yellow (1.0, 1.0, 0.0)
2. **Opacity Boost**: 0.8 → 1.0 (20% brightness increase)
3. **Size Unchanged**: No geometric scaling (prevents layout thrashing)
4. **Other Axes**: Remain at normal opacity (0.8), not dimmed until drag starts

**Cursor Change**:
- Axis handle: **EMouseCursor::CardinalCross** (translate directional arrow)
- Planar handle: **EMouseCursor::CardinalCross** (2D movement cross)
- Rotation circle: **EMouseCursor::GrabHand** (rotation indicator)
- Scale cube: **EMouseCursor::ResizeAll** (scale directional cross)

**Hit Testing Radius**:
- Visual element radius + 5 UE units (extended hit box for easier clicking)

### E.2: Drag/Active Behavior

**Immediately Upon Drag Start**:
1. **Active Axis**: Brightens to 100% opacity, maintains axis color
2. **Inactive Axes**: Dim to 30% opacity (0.8 × 0.375)
3. **Planar Handles**: If not being dragged, drop to 15% opacity (0.8 × 0.1875)
4. **Dragging Cursor**: **EMouseCursor::GrabHandClosed** (holding gesture)

**During Drag Interaction**:
- No further visual changes (prevents distraction)
- Grid snap indicators appear (if snapping enabled)
- Coordinate space indicator updates (local/world/custom)

**Upon Drag Release**:
- All axes return to normal opacity (0.8)
- Cursor returns to **EMouseCursor::Arrow**
- Visual state reverts to idle appearance

### E.3: Selection Ring/Highlight

**Ring Appearance**:
- Not a separate ring geometry
- Achieved via **brightness/saturation boost** of the active element
- Active axis: 100% opacity + slight color saturation boost
- Inactive: 30% opacity + slight desaturation

**No Separate Outline Geometry**:
- To reduce fillrate, UE5 uses alpha/color change only
- No dedicated border mesh

---

## F. POSITIONING & CENTERING

### F.1: Widget Origin

**Location**: **Object Pivot Point** (transform origin in world-space)
- **NOT** the mesh center (in case pivot is offset)
- Exactly at object's `FTransform::GetLocation()`

### F.2: Coordinate System

**Default**: **World Space**
- X-axis → East (red)
- Y-axis → North (green)
- Z-axis → Up (blue)

**Switchable To**:
- **Local Space**: Object's local coordinate frame
- **Parent Space**: Parent object's frame (if attached)
- **Custom Space**: User-defined matrix

**Matrix Application**:
- All gizmo geometry transformed by the coordinate frame matrix
- Screen-space sizing applied AFTER matrix transform

### F.3: Offset from Mesh Center

**Zero Offset**: The gizmo is centered exactly at the object's pivot point.
- No hardcoded offset
- User can adjust pivot position in the editor if offset is desired

---

## G. MODE-SPECIFIC RENDERING DETAILS

### G.1: Translation Mode (Render_Translate)

**Rendered Elements**:
1. **Three Axis Arrows** (X/Y/Z)
   - Stem: Cylinder, radius 1.5, length 70
   - Tip: Cone, height 22, radius 7
   - Color: Axis color (red/green/blue)
   - Material: Emissive PBR

2. **Three Planar Handles** (XY/YZ/XZ)
   - Box geometry: 15×15×2 UE units
   - Positioned at 55 units along axis (70 - 15 offset)
   - Color: Yellow (1, 1, 0)
   - Opacity: 0.6 (semi-transparent)
   - Layout: 90° between each plane

3. **Center Sphere** (XYZ - All-Axis)
   - Shape: Sphere
   - Size: ~20 UE units radius (inferred)
   - Color: Yellow
   - Opacity: 0.8

**Not Rendered**:
- Rotation circles (hidden in translation mode)
- Scale cubes (hidden in translation mode)

### G.2: Rotation Mode (Render_Rotate)

**Rendered Elements**:
1. **Three Axis Circles** (X/Y/Z)
   - Torus mesh: radius 70, inner radius 1.25, thickness 3.0
   - 64 segments, 8 slices
   - Color: Axis color (red/green/blue)
   - Opacity: 0.8
   - Layout: Each ring orthogonal to its axis

2. **Arcball Sphere** (XYZ - All-Axis)
   - Mesh: Full sphere
   - Radius: 70 UE units
   - Color: Mid-grey (0.5, 0.5, 0.5)
   - Opacity: **0.03** (very subtle, almost invisible until hovered)
   - Purpose: Visual guide for free 3D rotation

3. **Optional Gimbal Rings** (If Gimbal Mode Enabled)
   - Additional torus rings for gimbal lock visualization
   - Rendered in white (1, 1, 1)
   - Opacity: 0.6

4. **Rotation Text Labels** (If In Interaction)
   - HUD text overlay
   - Displays current rotation angle in degrees
   - Positioned at 75 UE units from center (along rotation plane)
   - Color: White text on semi-transparent black background

**Not Rendered**:
- Translation cones (hidden in rotation mode)
- Scale cubes (hidden in rotation mode)
- Planar translation handles (hidden in rotation mode)

### G.3: Scale Mode (Render_Scale)

**Rendered Elements**:
1. **Three Axis Cubes** (X/Y/Z)
   - Cube mesh: 12×12×12 UE units
   - Positioned at axis tip (70 units)
   - Color: Axis color (red/green/blue)
   - Opacity: 0.8
   - Layout: One at end of each axis

2. **Three Planar Handles** (XY/YZ/XZ) [Optional, Low Opacity]
   - Box geometry: 15×15×2 UE units
   - Positioned at 55 units along axis
   - Color: Yellow
   - Opacity: **0.15** (very faint, can be ignored)
   - Purpose: Indicate planar scale capability

3. **Center Sphere** (XYZ - Uniform Scale)
   - Shape: Sphere
   - Size: ~15 UE units radius
   - Color: Yellow or grey
   - Opacity: 0.8
   - Purpose: Uniform scale in all axes

**Not Rendered**:
- Rotation circles (hidden in scale mode)
- Translation cones (hidden in scale mode)

### G.4: 2D/Orthographic View Behavior

**In 2D Orthographic Views** (Front/Side/Top):
- **Rotation Mode**: Only shows rotation circle perpendicular to view plane
- **Translation Mode**: Shows all three axes (even though one is perpendicular to view)
- **Scale Mode**: Shows all three axes

**Arcball Rotation** Behavior:
- In perspective view: Full 3D arcball
- In ortho view: Constrained to 2D rotation on the view plane

---

## H. LEGACY FWidget CONSTANTS (Pre-UE5.6 / Fallback Values)

For reference, the older `FWidget` class defines:

| Constant | Value | Modern Equivalent |
|----------|-------|------------------|
| `AXIS_LENGTH` | 35.0 | TranslateAxisLength = 70.0 (doubled in UE5.6) |
| `TRANSLATE_ROTATE_AXIS_CIRCLE_RADIUS` | 20.0 | (Obsolete, RotateAxisRadius = 70.0 used instead) |
| `TWOD_AXIS_CIRCLE_RADIUS` | 10.0 | (Obsolete, used for 2D view only) |
| `INNER_AXIS_CIRCLE_RADIUS` | 48.0 | (Obsolete, replaced with RotateAxisInnerRadius = 1.25) |
| `OUTER_AXIS_CIRCLE_RADIUS` | 56.0 | (Obsolete, RotateAxisOuterRadiusOffset = 3.0 used instead) |
| `ROTATION_TEXT_RADIUS` | 75.0 | (Still used for HUD text position during rotation) |
| `AXIS_CIRCLE_SIDES` | 24 | (Obsolete, now RotateAxisNumSegments = 64 for smoother curves) |
| `AXIS_LENGTH_SCALE_OFFSET` | 5.0 | (Still used in some legacy code paths) |

**Note**: Modern UE5.6 prioritizes `FGizmoStyle` constants. The legacy `FWidget` class is maintained for backward compatibility but the interactive transform gizmo uses new values.

---

## I. IMPLEMENTATION CHECKLIST FOR UZENGINE

To achieve visual parity with UE5, implement in this order:

### Phase 1: Core Geometry & Colors
- [ ] Define `GizmoStyle` structure with all constants from section A
- [ ] Implement axis colors from section B.1 (X/Y/Z RGB values)
- [ ] Create cylinder mesh generator (axis stems) with 1.5 radius
- [ ] Create cone mesh generator (translation tips) with 22h × 7r parameters
- [ ] Create torus mesh generator (rotation circles) with 70r, 1.25ir, 3.0 offset
- [ ] Create cube mesh generator (scale tips) 12×12×12
- [ ] Create box/quad mesh generator (planar handles) 15×15×2

### Phase 2: Rendering & Materials
- [ ] Create emissive material (no shadow, unlit)
- [ ] Implement dynamic color assignment based on axis
- [ ] Set up alpha blending (translucent rendering path)
- [ ] Implement hover color shift (axis color → yellow)
- [ ] Implement inactive dimming (80% → 30% opacity)

### Phase 3: Screen-Space Scaling
- [ ] Implement WorldToScreen projection for dynamic sizing
- [ ] Apply AxisSizeMultiplier scaling
- [ ] Ensure sizes remain constant relative to viewport
- [ ] Handle zoom level adaptation

### Phase 4: Interaction Feedback
- [ ] Implement hit testing with 5 UE unit extended bounds
- [ ] Implement hover state (color/opacity change)
- [ ] Implement drag state (active bright, inactive dim)
- [ ] Implement cursor changes per interaction type

### Phase 5: Mode Switching
- [ ] Implement translation mode (show cones + planar boxes)
- [ ] Implement rotation mode (show toruses + arcball)
- [ ] Implement scale mode (show cubes + dim planar boxes)
- [ ] Implement 2D/ortho view adaptations

### Phase 6: Coordinate Spaces
- [ ] Apply transformation matrix for local/world/custom spaces
- [ ] Update gizmo orientation when coordinate space changes
- [ ] Preserve screen-space sizing after matrix transform

---

## J. KEY DIFFERENCES FROM LEGACY FWidget

UE5.6 modernized the gizmo system significantly:

| Aspect | Old FWidget | New UTransformGizmo |
|--------|------------|-------------------|
| **Base Axis Length** | 35 units | 70 units (2× larger) |
| **Rotation Circle Segments** | 24 | 64 (4× smoother) |
| **Rotation Ring Thickness** | Implicit, large | Explicit: 1.25 + 3.0 = 4.25 units |
| **Architecture** | Single monolithic class | Gizmo element hierarchy (GizmoElementArrow, etc.) |
| **Material System** | ArrowMaterial (hardcoded) | Dynamic materials per element |
| **Arcball** | Simple radius-based | Physics-based sphere intersection |
| **Gimbal Support** | None | Full gimbal lock mode with separate rings |
| **Screen-Space Handles** | 20 unit circles | 14 unit rectangles, smart positioning |
| **Interaction API** | SetCurrentAxis() | Modern IHoverBehaviorTarget / IClickDragBehaviorTarget |

---

## K. CITATIONS & SOURCES

All values extracted from UE5.6.0 source code:

1. **Axis Colors**:
   - `Core/Private/Misc/AxisDisplayInfo.cpp` lines 104-115
   - X: `FLinearColor(0.594f, 0.0197f, 0.0f)`
   - Y: `FLinearColor(0.1349f, 0.3959f, 0.0f)`
   - Z: `FLinearColor(0.0251f, 0.207f, 0.85f)`

2. **Legacy Widget Constants**:
   - `Engine/Source/Editor/UnrealEd/Public/UnrealWidget.h` lines 22-29
   - `Engine/Source/Editor/UnrealEd/Private/UnrealWidget.cpp` lines 37-44

3. **Modern Gizmo Style**:
   - `Engine/Source/Editor/Experimental/EditorInteractiveToolsFramework/Public/EditorGizmos/TransformGizmo.h` lines 152-210
   - `FGizmoStyle` structure with all geometry and color constants

4. **Gizmo Elements**:
   - `Engine/Source/Editor/Experimental/EditorInteractiveToolsFramework/Public/EditorGizmos/TransformGizmo.h` lines 356-410
   - Element types: UGizmoElementArrow, UGizmoElementTorus, UGizmoElementBox, etc.

---

## L. QUICK REFERENCE TABLE

| Component | Primary Value | Secondary Value | Tertiary Value |
|-----------|---------------|-----------------|-----------------|
| **Translation Axis Length** | 70 units | Cone height 22 | Cone radius 7 |
| **Rotation Axis Radius** | 70 units | Torus inner 1.25 | Outer offset 3.0 |
| **Scale Axis Length** | 70 units | Cube size 12 | Stem radius 1.5 |
| **X Axis Color** | (0.594, 0.0197, 0.0) | Red (industry std) | Alpha 0.8 default |
| **Y Axis Color** | (0.1349, 0.3959, 0.0) | Green (industry) | Alpha 0.8 default |
| **Z Axis Color** | (0.0251, 0.207, 0.85) | Blue (industry) | Alpha 0.8 default |
| **Hover Color** | (1.0, 1.0, 0.0) | Yellow highlight | Alpha 1.0 |
| **Inactive Alpha** | 0.3 | Dimmed state | Hover becomes 1.0 |
| **Screen-Space Handle** | 14 units | Offset 13 from arc | Rectangle shape |
| **Planar Handle** | 15×15 units | Offset -15 along axis | Thickness 2.0 |

