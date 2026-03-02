# Visual Shader Editor Spec

## 1. Overview & Goals

### Purpose

The Visual Shader Editor is a node-based, graph-driven tool for authoring PBR materials inside VSCode. It enables artists and technical artists to create complex surface shaders by connecting visual nodes rather than writing raw HLSL. The editor compiles the node graph into a `.uzmat` + `.hlsl` pair that slots directly into UzEngine's existing shader pipeline -- ShaderVariantKey system, DXC compilation, PSO cache, and hot-reload.

### Target Users

| Persona | Needs |
|---------|-------|
| **Environment Artist** | Drag textures, tweak blend weights, adjust roughness curves. Never sees HLSL. |
| **Technical Artist** | Builds reusable sub-graphs, writes Custom HLSL nodes, optimizes shader instructions. |
| **Programmer** | Exports HLSL for hand-tuning, debugs codegen output, extends built-in node library. |

### Design Philosophy

1. **HLSL is the truth.** The graph is a visual front-end. The generated `.hlsl` is always human-readable and can be hand-edited after export. A hand-edited HLSL severs the link to the graph (one-way export).
2. **No hidden magic.** Every node maps to a visible, documented HLSL function. The Output panel shows the compiled code at all times.
3. **AI-native.** The `shader_assist` MCP tool can read and modify `.uzshader` graph files. AI agents can generate sub-graphs from text descriptions and explain what any node does.
4. **Non-destructive iteration.** Graph changes hot-reload in < 100 ms via the existing `shader.reload` IPC path. The preview viewport updates in real time.
5. **Variant-aware.** The graph can contain conditional branches gated on ShaderVariantKey defines (SKINNED, INSTANCED, ALPHA_BLEND). The transpiler emits the correct `#ifdef` blocks.

### Integration Points

| System | Integration |
|--------|-------------|
| Shader Compiler | Generated `.hlsl` uses `#include "common.hlsli"`, `#include "brdf.hlsli"`, etc. |
| ShaderVariantKey | Graph outputs `[variants]` section in `.uzmat` for custom defines. |
| PSO Cache | Keyed by `{shader_path, variant_key, ...}` -- graph shaders participate identically. |
| Hot-Reload | `shader.reload` IPC triggers live preview refresh on graph save. |
| Material System | `.uzmat` `shader =` field points to the generated `.hlsl`. |
| GlobalParams b15 | Time, ScreenSize, Wind, Fog nodes read from `global_cbuffer.hlsli`. |
| Undo/Redo | All graph mutations go through `EditorCommand` pattern (TypeScript `UndoStack`). |

---

## 2. Editor Layout

### 2.1 Panel Architecture

The Visual Shader Editor is a single `WebviewPanel` registered as `uzengine.shaderEditor`. It occupies the main editor area (same region as text editors). Multiple shader graphs can be open simultaneously in separate tabs.

### 2.2 Full Layout

```
+-----------------------------------------------------------------------------------+
|  Toolbar                                                                          |
|  [New] [Open] [Save] [Revert] | [Compile] [Export HLSL] | [Undo] [Redo] | [?]   |
+----------+----------------------------------------------+-------------------------+
|          |                                              |                         |
|  Node    |                                              |  Properties             |
|  Palette |            NODE CANVAS                       |  Inspector              |
|          |          (main editing area)                  |                         |
|  --------+                                              |  ---------------------- |
|  Search  |                                              |  [Node: Lerp]          |
|  [______]|   +--------+     +--------+     +--------+  |  A: float3 [0,0,0]     |
|          |   | Albedo |---->|  Lerp  |---->| Master |  |  B: float3 [1,1,1]     |
|  Math    |   | Tex    |     |        |     | Output |  |  Alpha: 0.5            |
|   Add    |   +--------+     +--------+     +--------+  |                         |
|   Sub    |                                              |  ---------------------- |
|   Mul    |   +--------+                                 |                         |
|   Div    |   | Normal |---->            [MINIMAP]       |  Preview                |
|   ...    |   | Map    |     +---+---+---+---+           |  +-------------------+  |
|          |   +--------+     |   | . |   |   |           |  |                   |  |
|  Texture |                  +---+---+---+---+           |  |    (3D sphere     |  |
|   Sample |                                              |  |     with live     |  |
|   ...    |                                              |  |     material)     |  |
|          |                                              |  |                   |  |
|  Lighting|                                              |  +-------------------+  |
|   ...    |                                              |  [Sphere][Cube][Plane]  |
|          |                                              |  [Env: Studio] [Lit]    |
|  UV      |                                              |                         |
|   ...    +----------------------------------------------+-------------------------+
|          |  Output Panel                                                          |
|  Consts  |  [HLSL Code] [Errors] [Warnings] [Stats]                              |
|   ...    |  ```hlsl                                                               |
|          |  // Auto-generated by UzEngine Visual Shader Editor                    |
|  Custom  |  #include "common.hlsli"                                               |
|   ...    |  #include "brdf.hlsli"                                                 |
|          |  float4 PSMain(VSOut i) : SV_TARGET { ... }                            |
+----------+------------------------------------------------------------------------+
```

### 2.3 Node Canvas

The Node Canvas is the primary interaction area. It renders an infinite 2D grid where nodes are placed and connected.

**Navigation:**

| Input | Action |
|-------|--------|
| Middle-click drag | Pan the canvas |
| Scroll wheel | Zoom in/out (0.25x to 4.0x, default 1.0x) |
| Right-click (empty area) | Context menu: Add Node, Paste, Select All |
| Right-click (on node) | Context menu: Cut, Copy, Delete, Duplicate, Disconnect All, Collapse/Expand |
| Left-click drag (empty area) | Box select (rubber band) |
| Left-click (node) | Select node (Shift+click to add/remove from selection) |
| Left-click drag (node) | Move node(s) |
| Left-click drag (pin to pin) | Create connection |
| Double-click (connection wire) | Insert a reroute point |
| Delete / Backspace | Delete selected nodes and connections |
| Ctrl+A | Select all nodes |
| Ctrl+C / Ctrl+V | Copy / paste nodes (preserves internal connections) |
| Ctrl+D | Duplicate selection in-place (offset +20, +20 px) |
| F | Frame selection (zoom-to-fit selected nodes, or all if none selected) |
| Spacebar | Open node search overlay (search-to-add) |
| Q | Open node search overlay (alternative shortcut) |
| 1 | Align selected nodes horizontally |
| 2 | Align selected nodes vertically |
| C | Create comment box around selection |
| Ctrl+G | Group selected nodes into a sub-graph |

**Visual Features:**
- Grid snapping: nodes snap to 16-pixel grid (toggle with Ctrl+Shift+G)
- Connection wires are Bezier curves, color-coded by data type
- Active data flow is animated (pulse travels along wire during live preview)
- Disconnected/error nodes have a red outline with a warning icon
- Selected nodes have a blue highlight border

**Wire Color Coding:**

| Type | Color | Hex |
|------|-------|-----|
| `float` | Grey | `#A0A0A0` |
| `float2` | Green | `#70C070` |
| `float3` | Yellow | `#E0D050` |
| `float4` | Blue | `#5090E0` |
| `matrix` | Purple | `#B060D0` |
| `texture` | Red-Orange | `#E07030` |
| `sampler` | Pink | `#D060A0` |

### 2.4 Node Palette Sidebar

Left-side panel (240px default width, resizable). Contains categorized node listings.

**Categories:**

| Category | Icon | Description |
|----------|------|-------------|
| **Input** | Pin icon | UV, WorldPos, WorldNormal, VertexColor, ViewDir, ScreenPos, Time, DeltaTime |
| **Math** | +/- icon | Add, Subtract, Multiply, Divide, Power, Sqrt, Abs, Negate, Floor, Ceil, Frac, Step, SmoothStep, Clamp, Saturate, Min, Max, Dot, Cross, Normalize, Length, Distance, Reflect |
| **Texture** | Image icon | SampleTexture2D, SampleTextureCube, SampleNormalMap, Triplanar, Parallax, FlipBook |
| **Lighting** | Bulb icon | NdotL, NdotV, NdotH, HdotV, Fresnel, CookTorrance, Lambert |
| **UV** | Grid icon | TilingOffset, Rotate UV, Polar UV, Radial Shear, Spherize, Twirl, Voronoi UV |
| **Color** | Palette | ColorConstant, Blend, Contrast, Saturation, Hue Shift, Gradient, Remap |
| **Noise** | Wave icon | Perlin, Simplex, Voronoi, FBM, White Noise |
| **Constants** | # icon | Float, Float2, Float3, Float4, Matrix4x4, Boolean, IntConstant |
| **Utility** | Wrench | Lerp, InverseLerp, OneMinus, Split, Combine, Swizzle, Append, Branch, Comparison |
| **Channel** | Layers icon | ChannelMask, Desaturate, NormalBlend, NormalStrength, NormalUnpack |
| **Global** | Globe icon | Time, DeltaTime, ScreenSize, WindDirection, WindStrength, FogColor, FogDensity |
| **Custom** | Code icon | CustomHLSL, SubGraph, Comment |

Each category is expandable/collapsible. A search field at the top filters across all categories.

### 2.5 Properties Inspector

Right-side panel (280px default width, resizable). Shows editable parameters for the currently selected node.

```
Properties Inspector
====================

  Node: Lerp
  Category: Utility
  -----------------------------------------------
  Input A         [linked: Albedo Tex.rgb]
  Input B         [linked: --- (unlinked)]
                  Default: R[1.0] G[1.0] B[1.0]
  Alpha           [linked: --- (unlinked)]
                  Default: [====o=====] 0.50
                  Range: 0.0 ... 1.0
  -----------------------------------------------
  Output Type:    float3
  Instruction Cost: ~1 ALU
```

**Property Widget Types:**

| Node param type | Widget |
|-----------------|--------|
| `float` | Slider + number input (min, max, step configurable per node) |
| `float2` | Two-field row (X, Y) |
| `float3` | Three-field row (R/G/B or X/Y/Z) + optional color picker |
| `float4` | Four-field row + optional color picker (with alpha) |
| `bool` | Toggle switch |
| `enum` | Dropdown |
| `texture` | File picker + thumbnail preview |
| `string` | Text input |

When a pin is connected, its parameter field is greyed out and shows the source node/pin name. Disconnecting restores the default value editor.

### 2.6 Preview Viewport

Embedded within the right panel, below the Properties Inspector. Renders the current shader on a 3D mesh in real time.

**Controls:**

| Feature | Detail |
|---------|--------|
| Preview Mesh | Sphere (default), Cube, Plane, Cylinder, Torus, Custom (file pick) |
| Environment | Studio (neutral grey), Outdoor (IBL sky), Dark, Custom HDRI |
| Lighting | Toggle: 1 directional, 3-point rig, IBL-only |
| Camera | Orbit by left-drag, zoom by scroll |
| Channels | View: Final, Albedo only, Normal only, Roughness only, Metallic only, Emissive only, AO only |
| Background | Checkerboard (transparency), Solid black, Solid white, Environment |
| Wireframe | Toggle wireframe overlay |
| Before/After | Split-screen slider comparing current graph vs. last saved version |

The preview is rendered by the engine via a dedicated `shader_preview.render` IPC call that renders a single mesh with the compiled material. The result is streamed as JPEG to the webview, same as the main viewport pipeline.

### 2.7 Output Panel

Bottom panel, tabbed:

**Tab: HLSL Code**
Shows the generated HLSL in a read-only Monaco editor (syntax highlighted). Updates on every graph change. Line numbers correlate to error diagnostics.

**Tab: Errors**
DXC compilation errors and warnings. Each entry is clickable -- clicking an error highlights the responsible node in the canvas with a red outline and scrolls to the relevant generated HLSL line.

**Tab: Warnings**
Non-fatal issues: unused nodes, disconnected pins, potential performance issues (e.g., "Perlin noise sampled at full resolution -- consider adding a Multiply node to reduce UV scale").

**Tab: Stats**
Shader statistics from DXC reflection: instruction count, register usage, texture samples, estimated GPU cycles, target shader model.

### 2.8 Toolbar

```
[New] [Open] [Save (Ctrl+S)] [Revert] | [Compile (F5)] [Export HLSL (Ctrl+E)] | [Undo (Ctrl+Z)] [Redo (Ctrl+Shift+Z)] | [Settings] [Help]
```

| Button | Action |
|--------|--------|
| New | Create blank shader graph from template (PBR Surface, Unlit, Post-Process) |
| Open | File picker filtered to `.uzshader` |
| Save | Write `.uzshader` graph file + recompile + hot-reload |
| Revert | Reload from disk, discard unsaved changes (confirmation dialog) |
| Compile | Force recompilation via DXC, refresh preview |
| Export HLSL | Write the generated `.hlsl` to a chosen path (severs graph link) |
| Settings | Editor preferences: grid snap, wire style, auto-compile on change |
| Help | Opens `docs/guides/shader-authoring.md` in a new tab |

---

## 3. Node System

### 3.1 Node Anatomy

Every node in the graph is a rectangular box with typed input pins on the left edge and typed output pins on the right edge.

```
+--[Node Title]------+
|  [icon] Category   |
+--------------------+
o A        Out  o----+----> (connection wire)
o B              |
o Alpha          |
+--------------------+
```

- **Title bar**: Node name, collapsible
- **Input pins** (left, circles): Data flows in. Can be connected or have a default value.
- **Output pins** (right, circles): Data flows out. Can be connected to multiple inputs.
- **Body**: Shows embedded controls when applicable (e.g., color swatch for ColorConstant, texture thumbnail for SampleTexture2D)

### 3.2 Pin Types

| Type ID | HLSL Type | Pin Shape | Color |
|---------|-----------|-----------|-------|
| `FLOAT` | `float` | Circle | Grey `#A0A0A0` |
| `FLOAT2` | `float2` | Circle | Green `#70C070` |
| `FLOAT3` | `float3` | Circle | Yellow `#E0D050` |
| `FLOAT4` | `float4` | Circle | Blue `#5090E0` |
| `MATRIX` | `float4x4` | Diamond | Purple `#B060D0` |
| `TEXTURE2D` | `Texture2D` | Square | Red-Orange `#E07030` |
| `TEXTURECUBE` | `TextureCube` | Square | Red-Orange `#E07030` |
| `SAMPLER` | `SamplerState` | Triangle | Pink `#D060A0` |
| `BOOL` | `bool` | Circle | White `#FFFFFF` |
| `INT` | `int` | Circle | Cyan `#60D0D0` |

### 3.3 Type Coercion Rules

When connecting pins of different types, implicit coercion is applied if possible. If coercion is not possible, the connection is rejected (wire turns red momentarily and snaps back).

| Source | Destination | Coercion | HLSL Code |
|--------|-------------|----------|-----------|
| `float` | `float2` | Splat | `float2(x, x)` |
| `float` | `float3` | Splat | `float3(x, x, x)` |
| `float` | `float4` | Splat | `float4(x, x, x, x)` |
| `float2` | `float3` | Append 0 | `float3(v.xy, 0.0)` |
| `float2` | `float4` | Append 0, 1 | `float4(v.xy, 0.0, 1.0)` |
| `float3` | `float4` | Append 1 | `float4(v.xyz, 1.0)` |
| `float4` | `float3` | Truncate | `v.xyz` |
| `float4` | `float2` | Truncate | `v.xy` |
| `float4` | `float` | Take X | `v.x` |
| `float3` | `float2` | Truncate | `v.xy` |
| `float3` | `float` | Take X | `v.x` |
| `float2` | `float` | Take X | `v.x` |
| `int` | `float` | Cast | `(float)x` |
| `float` | `int` | Cast | `(int)x` |
| `bool` | `float` | Cast | `x ? 1.0 : 0.0` |
| `TEXTURE2D` | any scalar | **Rejected** | -- |
| any scalar | `TEXTURE2D` | **Rejected** | -- |
| `SAMPLER` | any non-sampler | **Rejected** | -- |

Coercion is applied at the destination pin. The generated HLSL inserts the appropriate cast/constructor call.

### 3.4 Node Catalog

#### 3.4.1 Input Nodes

These nodes expose per-vertex or per-pixel data from the vertex shader interpolants and constant buffers.

| Node | Outputs | Description |
|------|---------|-------------|
| **UV** | `float2 UV` | Texture coordinates (TEXCOORD0). Optional: channel selector for TEXCOORD0..3. |
| **World Position** | `float3 WorldPos` | Fragment world-space position |
| **World Normal** | `float3 Normal` | Interpolated world-space normal (normalized) |
| **Tangent** | `float3 Tangent` | World-space tangent vector |
| **Bitangent** | `float3 Bitangent` | World-space bitangent vector |
| **Vertex Color** | `float4 Color` | Per-vertex color (if available) |
| **View Direction** | `float3 ViewDir` | Normalized camera-to-fragment vector |
| **Screen Position** | `float4 ScreenPos` | Clip-space position (SV_POSITION) |
| **Depth** | `float Depth` | Linear depth from camera |
| **Camera Position** | `float3 CamPos` | World-space camera position (from PerFrame CBV) |

#### 3.4.2 Math Nodes

All math nodes operate component-wise unless otherwise noted. Each has standard input/output pins.

| Node | Inputs | Outputs | HLSL |
|------|--------|---------|------|
| **Add** | A, B | Result | `A + B` |
| **Subtract** | A, B | Result | `A - B` |
| **Multiply** | A, B | Result | `A * B` |
| **Divide** | A, B | Result | `A / (B + EPSILON)` |
| **Power** | Base, Exp | Result | `pow(abs(Base), Exp)` |
| **Sqrt** | In | Result | `sqrt(max(In, 0.0))` |
| **Abs** | In | Result | `abs(In)` |
| **Negate** | In | Result | `-In` |
| **Floor** | In | Result | `floor(In)` |
| **Ceil** | In | Result | `ceil(In)` |
| **Frac** | In | Result | `frac(In)` |
| **Round** | In | Result | `round(In)` |
| **Sign** | In | Result | `sign(In)` |
| **Step** | Edge, In | Result | `step(Edge, In)` |
| **SmoothStep** | Min, Max, In | Result | `smoothstep(Min, Max, In)` |
| **Clamp** | In, Min, Max | Result | `clamp(In, Min, Max)` |
| **Saturate** | In | Result | `saturate(In)` |
| **Min** | A, B | Result | `min(A, B)` |
| **Max** | A, B | Result | `max(A, B)` |
| **Dot** | A (vec), B (vec) | Result (float) | `dot(A, B)` |
| **Cross** | A (float3), B (float3) | Result (float3) | `cross(A, B)` |
| **Normalize** | In (vec) | Result (vec) | `normalize(In)` |
| **Length** | In (vec) | Result (float) | `length(In)` |
| **Distance** | A (vec), B (vec) | Result (float) | `distance(A, B)` |
| **Reflect** | I (vec), N (vec) | Result (vec) | `reflect(I, N)` |
| **Refract** | I, N, IOR | Result | `refract(I, N, IOR)` |
| **OneMinus** | In | Result | `1.0 - In` |
| **Remap** | In, InMin, InMax, OutMin, OutMax | Result | `lerp(OutMin, OutMax, saturate((In-InMin)/(InMax-InMin)))` |

**Safety notes**: Division guards against zero with EPSILON. Power uses `abs(Base)` to avoid NaN from negative bases. Sqrt clamps negative inputs.

#### 3.4.3 Texture Nodes

| Node | Inputs | Outputs | Description |
|------|--------|---------|-------------|
| **SampleTexture2D** | Texture (TEXTURE2D), UV (float2), Sampler (SAMPLER) | RGBA (float4), R, G, B, A (float each) | Standard 2D texture sample |
| **SampleTextureCube** | Texture (TEXTURECUBE), Dir (float3), Sampler (SAMPLER) | RGBA (float4) | Cubemap sample |
| **SampleNormalMap** | Texture (TEXTURE2D), UV (float2), Sampler (SAMPLER), Strength (float) | Normal (float3) | Unpack normal from tangent-space normal map; applies strength scaling |
| **Triplanar** | Texture (TEXTURE2D), WorldPos (float3), Normal (float3), Sharpness (float) | Color (float3) | Triplanar mapping projection |
| **Parallax** | HeightMap (TEXTURE2D), UV (float2), ViewDirTS (float3), Scale (float), Steps (int) | UV (float2) | Parallax occlusion mapping, outputs displaced UV |
| **FlipBook** | Texture (TEXTURE2D), UV (float2), Rows (int), Cols (int), Frame (float) | RGBA (float4) | Animated sprite sheet sampling |
| **TextureSize** | Texture (TEXTURE2D) | Size (float2), InvSize (float2) | Returns texture dimensions and 1/dimensions |

**Texture slot allocation**: Each SampleTexture2D node is assigned a texture register `t0`..`t7` during compilation. The compiler tracks usage and reports an error if > 8 texture slots are consumed.

#### 3.4.4 Lighting Nodes

These nodes provide PBR building blocks. They map directly to functions in `brdf.hlsli` and `lighting.hlsli`.

| Node | Inputs | Outputs | Maps to |
|------|--------|---------|---------|
| **NdotL** | Normal (float3), LightDir (float3) | Result (float) | `saturate(dot(N, L))` |
| **NdotV** | Normal (float3), ViewDir (float3) | Result (float) | `max(dot(N, V), EPSILON)` |
| **NdotH** | Normal (float3), HalfVec (float3) | Result (float) | `saturate(dot(N, H))` |
| **HdotV** | HalfVec (float3), ViewDir (float3) | Result (float) | `saturate(dot(H, V))` |
| **Fresnel** | NdotV (float), F0 (float3) | Result (float3) | `F_Schlick(NdotV, F0)` from `brdf.hlsli` |
| **FresnelEffect** | Normal (float3), ViewDir (float3), Power (float) | Result (float) | `pow(1.0 - saturate(dot(N, V)), Power)` |
| **CookTorrance** | N, V, L, Albedo, Roughness, Metallic | Specular+Diffuse (float3) | `CookTorrance()` from `brdf.hlsli` |
| **Lambert** | Normal (float3), LightDir (float3) | Result (float) | `max(dot(N, L), 0.0) * INV_PI` |

#### 3.4.5 Noise Nodes

| Node | Inputs | Outputs | Maps to |
|------|--------|---------|---------|
| **PerlinNoise** | Position (float3) | Value (float) | `perlin_noise()` from `noise.hlsli` |
| **SimplexNoise** | Position (float3) | Value (float) | `simplex_noise()` from `noise.hlsli` |
| **FBM** | Position (float3), Octaves (int), Lacunarity (float), Gain (float) | Value (float) | `fbm()` from `noise.hlsli` |
| **VoronoiNoise** | UV (float2), CellDensity (float) | Cells (float), Distance (float) | Custom Voronoi implementation |
| **WhiteNoise** | UV (float2) | Value (float) | `hash21()` from `noise.hlsli` |

#### 3.4.6 UV Nodes

| Node | Inputs | Outputs | Description |
|------|--------|---------|-------------|
| **TilingOffset** | UV (float2), Tiling (float2), Offset (float2) | UV (float2) | `UV * Tiling + Offset` |
| **RotateUV** | UV (float2), Center (float2), Angle (float) | UV (float2) | 2D rotation around center |
| **PolarCoords** | UV (float2), Center (float2), RadialScale (float), LengthScale (float) | UV (float2) | Cartesian to polar |
| **RadialShear** | UV (float2), Center (float2), Strength (float2) | UV (float2) | Radial distortion |
| **Spherize** | UV (float2), Center (float2), Strength (float2) | UV (float2) | Spherical distortion |
| **Twirl** | UV (float2), Center (float2), Strength (float), Offset (float2) | UV (float2) | Swirl distortion |

#### 3.4.7 Constant Nodes

| Node | Parameter | Output |
|------|-----------|--------|
| **Float** | Value (editable slider) | `float` |
| **Float2** | X, Y | `float2` |
| **Float3** | X, Y, Z (or R, G, B with color picker) | `float3` |
| **Float4** | X, Y, Z, W (or RGBA with color picker) | `float4` |
| **Boolean** | Toggle | `bool` (emitted as `1.0` or `0.0`) |
| **Integer** | Value (editable) | `int` |
| **PI** | (none) | `3.14159265` |
| **Tau** | (none) | `6.28318530` |

#### 3.4.8 Utility Nodes

| Node | Inputs | Outputs | Description |
|------|--------|---------|-------------|
| **Lerp** | A, B, Alpha (float) | Result | `lerp(A, B, Alpha)` |
| **InverseLerp** | A, B, Value | Result (float) | `saturate((Value - A) / (B - A + EPSILON))` |
| **Split** | In (float4) | R, G, B, A (float each) | Decompose vector into components |
| **Combine** | R, G, B, A (float each) | RGBA (float4) | Assemble components into vector |
| **Swizzle** | In (float4), Mask (dropdown: e.g., "xyzw", "rgb", "rg", etc.) | Result | Arbitrary swizzle |
| **Append** | A (floatN), B (floatM) | Result (floatN+M) | Concatenate vectors (max float4) |
| **Branch** | Condition (bool), True, False | Result | `Condition ? True : False` |
| **Comparison** | A, B, Op (dropdown: ==, !=, <, >, <=, >=) | Result (bool) | Comparison operator |
| **Reroute** | In | Out | Passthrough for wire organization (no codegen) |
| **Comment** | (none) | (none) | Annotation box; no codegen |

#### 3.4.9 Global Nodes

These read from the `GlobalParams` cbuffer at register `b15` (see `global_cbuffer.hlsli`).

| Node | Output | HLSL |
|------|--------|------|
| **Time** | `float` | `g_Time` |
| **DeltaTime** | `float` | `g_DeltaTime` |
| **ScreenSize** | `float2` | `g_ScreenSize` |
| **WindDirection** | `float3` | `g_WindDirection` |
| **WindStrength** | `float` | `g_WindStrength` |
| **FogColor** | `float3` | `g_FogColor.rgb` |
| **FogDensity** | `float` | `g_FogDensity` |

#### 3.4.10 Custom HLSL Node

A special node that allows embedding raw HLSL code. The user defines input/output pins and writes the function body.

```
+--[Custom HLSL]----------------------------+
|  Inputs:                                   |
|  o color (float3)                          |
|  o threshold (float)                       |
|                                            |
|  Outputs:                                  |
|  o result (float3)              result o---+-->
|                                            |
|  Code:                                     |
|  +-----------------------------------------+
|  | float3 result;                          |
|  | float lum = dot(color, float3(          |
|  |     0.2126, 0.7152, 0.0722));           |
|  | result = lum > threshold ?              |
|  |     color : float3(0,0,0);              |
|  +-----------------------------------------+
+--------------------------------------------+
```

The Properties Inspector shows:
- Pin list editor (add/remove/rename input and output pins, set types)
- Code editor (Monaco editor, syntax highlighted HLSL)
- "Validate" button that attempts DXC compilation of the custom function in isolation

#### 3.4.11 Master Output Node

Every graph has exactly one Master Output node. It cannot be deleted. It defines the final surface outputs that map to the engine's GBuffer/forward pass.

```
+--[PBR Surface Output]-----+
|                            |
o  Albedo (float3)           |     GBuffer0.rgb
o  Alpha (float)             |     Blend factor / alpha test
o  Normal (float3)           |     GBuffer1 (oct-encoded)
o  Metallic (float)          |     GBuffer2.g (or forward)
o  Roughness (float)         |     GBuffer2.r (or forward)
o  Emissive (float3)         |     GBuffer2 additive pass / forward
o  AO (float)                |     GBuffer0.a
o  Opacity (float)           |     Alpha blend mode
o  AlphaCutoff (float)       |     Alpha test threshold (MASK mode)
o  WorldPositionOffset (f3)  |     Vertex displacement (VS)
o  CustomData0 (float4)      |     User-defined passthrough
|                            |
|  [Surface Type: PBR    v]  |
|  [Blend Mode: Opaque   v]  |
|  [Two Sided: [ ]       ]   |
|  [Depth Write: [x]     ]   |
+----------------------------+
```

**Surface Types (dropdown on Master Output):**

| Surface Type | GBuffer writes | Notes |
|-------------|---------------|-------|
| **PBR Surface** | Albedo, Normal, Metallic, Roughness, AO, Emissive | Standard Cook-Torrance PBR. Default. |
| **Unlit** | Emissive only | No lighting. Direct color output. |
| **Post-Process** | N/A (fullscreen quad) | Reads scene color texture as input. Outputs to screen. |

**Blend Modes (dropdown on Master Output):**

| Blend Mode | ShaderVariantKey define | Render pass |
|-----------|-------------------------|-------------|
| Opaque | `ALPHA_MODE_OPAQUE` | GBuffer pass (deferred) |
| Alpha Mask | `ALPHA_MODE_MASK` | GBuffer pass with `clip(alpha - cutoff)` |
| Alpha Blend | `ALPHA_MODE_BLEND` | Forward Transparent pass (sorted back-to-front) |
| Additive | `ALPHA_MODE_ADDITIVE` | Forward Transparent pass (additive blend) |

### 3.5 Sub-Graphs

A sub-graph is a reusable node graph saved as a separate `.uzshader` file and referenced by other graphs. It appears as a single node in the parent graph.

**Creating a sub-graph:**
1. Select a group of nodes.
2. Press Ctrl+G or right-click "Create Sub-Graph."
3. The editor extracts the selection, identifies unconnected input pins as sub-graph inputs and unconnected output pins as sub-graph outputs.
4. The sub-graph is saved as a `.uzshader` file in `shaders/graphs/`.
5. In the parent graph, the selection is replaced by a single SubGraph node referencing the file.

Sub-graphs are inlined during HLSL transpilation (no function call overhead -- the HLSL compiler would optimize this anyway, but inlining makes the generated code more readable and debuggable).

---

## 4. PBR Material Integration

### 4.1 Compilation Output

When the user saves a `.uzshader` graph, the compiler produces two files:

```
shaders/graphs/brick_wall.uzshader      <-- the graph file (source of truth in editor)
shaders/generated/brick_wall.hlsl       <-- generated HLSL (auto-generated, do not hand-edit)
materials/brick_wall.uzmat              <-- material definition (references the HLSL)
```

### 4.2 Generated .uzmat

```ini
[uzmaterial version=1]
; Auto-generated by Visual Shader Editor — do not edit manually
; Source graph: res://shaders/graphs/brick_wall.uzshader
shader = "res://shaders/generated/brick_wall.hlsl"

; Texture assignments (from SampleTexture2D nodes)
albedo_texture   = "res://textures/brick_albedo.png"
normal_texture   = "res://textures/brick_normal.png"
roughness_texture = "res://textures/brick_roughness.png"

; Scalar parameters (from exposed Float/Color nodes marked "Material Parameter")
tiling_scale     = 2.0
color_tint       = [1.0, 0.9, 0.85]
roughness_boost  = 0.1

; Alpha mode (from Master Output node)
alpha_mode = OPAQUE

; Auto-detected variant defines
[variants]
HAS_ALBEDO_TEX = 1
HAS_NORMAL_TEX = 1
HAS_ROUGHNESS_TEX = 1
```

### 4.3 Generated .hlsl

The transpiler emits a complete, self-contained HLSL file:

```hlsl
// ============================================================================
// Auto-generated by UzEngine Visual Shader Editor
// Source: res://shaders/graphs/brick_wall.uzshader
// Generated: 2026-03-02T14:30:00Z
// DO NOT EDIT — changes will be overwritten on next graph save
// ============================================================================

#include "common.hlsli"
#include "brdf.hlsli"
#include "lighting.hlsli"
#include "shadow.hlsli"
#include "global_cbuffer.hlsli"

#ifdef SKINNED
#include "skinning.hlsli"
#endif

// ---------------------------------------------------------------------------
// Constant buffers
// ---------------------------------------------------------------------------

cbuffer PerObject : register(b0) {
    float4x4 model;
    float4x4 view_proj;
#ifdef SKINNED
    float4x4 prev_model;
#endif
};

cbuffer PerFrame : register(b1) {
    float3   camera_pos;
    float    _pad0;
    float4x4 prev_view_proj;
    float2   jitter_offset;
    float2   _pad1;
    // Light data
    DirLight dir_light;
    uint     num_point_lights;
    uint     num_spot_lights;
    float2   _pad2;
};

// Material parameters (from graph exposed nodes)
cbuffer MaterialParams : register(b2) {
    float    mat_tiling_scale;       // default: 2.0
    float3   mat_color_tint;         // default: [1.0, 0.9, 0.85]
    float    mat_roughness_boost;    // default: 0.1
    float3   _mat_pad;
};

// ---------------------------------------------------------------------------
// Textures & samplers
// ---------------------------------------------------------------------------

Texture2D    tex_albedo    : register(t0);
Texture2D    tex_normal    : register(t1);
Texture2D    tex_roughness : register(t2);

SamplerState sampler_linear : register(s0);

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------

struct VSIn {
    float3 pos     : POSITION;
    float3 normal  : NORMAL;
    float3 tangent : TANGENT;
    float2 uv      : TEXCOORD0;
#ifdef SKINNED
    uint4  bone_ids     : BLENDINDICES;
    float4 bone_weights : BLENDWEIGHT;
#endif
#ifdef INSTANCED
    uint   instance_id  : SV_InstanceID;
#endif
};

struct VSOut {
    float4 pos      : SV_POSITION;
    float3 world    : TEXCOORD0;
    float3 normal   : TEXCOORD1;
    float3 tangent  : TEXCOORD2;
    float2 uv       : TEXCOORD3;
};

#ifdef INSTANCED
StructuredBuffer<float4x4> instance_transforms : register(t7);
#endif

VSOut VSMain(VSIn v) {
    VSOut o;

    float4x4 world_mtx = model;
#ifdef INSTANCED
    world_mtx = instance_transforms[v.instance_id];
#endif

#ifdef SKINNED
    // Apply bone transforms
    StructuredBuffer<float4x4> bone_palette = /* bound at t6 */;
    SkinVertex sv = skin_vertex(v.pos, v.normal, v.tangent,
                                v.bone_ids, v.bone_weights, bone_palette);
    float3 local_pos    = sv.position;
    float3 local_normal = sv.normal;
    float3 local_tan    = sv.tangent;
#else
    float3 local_pos    = v.pos;
    float3 local_normal = v.normal;
    float3 local_tan    = v.tangent;
#endif

    // -- WorldPositionOffset from graph (if connected) --
    // float3 wpo = <graph_node_chain>;
    // local_pos += wpo;

    float4 world_pos = mul(world_mtx, float4(local_pos, 1.0));
    o.pos    = mul(view_proj, world_pos);
    o.world  = world_pos.xyz;
    o.normal = normalize(mul((float3x3)world_mtx, local_normal));
    o.tangent = normalize(mul((float3x3)world_mtx, local_tan));
    o.uv     = v.uv;
    return o;
}

// ---------------------------------------------------------------------------
// Pixel shader — node graph evaluation
// ---------------------------------------------------------------------------

// Node evaluation functions are inlined below.
// Variable naming: node_<id>_<output_name>

float4 PSMain(VSOut i) : SV_TARGET {
    // === Input nodes ===
    float2 node_1_uv = i.uv;
    float3 node_2_normal = normalize(i.normal);
    float3 node_3_viewdir = normalize(camera_pos - i.world);

    // === Node 4: TilingOffset ===
    float2 node_4_uv = node_1_uv * float2(mat_tiling_scale, mat_tiling_scale)
                      + float2(0.0, 0.0);

    // === Node 5: SampleTexture2D (Albedo) ===
    float4 node_5_rgba = tex_albedo.Sample(sampler_linear, node_4_uv);
    float3 node_5_rgb  = node_5_rgba.rgb;

    // === Node 6: Multiply (Albedo * Tint) ===
    float3 node_6_result = node_5_rgb * mat_color_tint;

    // === Node 7: SampleNormalMap ===
    float3 node_7_normal_ts = tex_normal.Sample(sampler_linear, node_4_uv).xyz * 2.0 - 1.0;
    // Construct TBN matrix
    float3 T = normalize(i.tangent);
    float3 N = normalize(i.normal);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 node_7_normal = normalize(mul(node_7_normal_ts, TBN));

    // === Node 8: SampleTexture2D (Roughness) ===
    float node_8_roughness = tex_roughness.Sample(sampler_linear, node_4_uv).r;

    // === Node 9: Add (Roughness + Boost) ===
    float node_9_result = saturate(node_8_roughness + mat_roughness_boost);

    // === Master Output ===
    float3 out_albedo    = node_6_result;
    float3 out_normal    = node_7_normal;
    float  out_metallic  = 0.0;   // default (unconnected pin)
    float  out_roughness = node_9_result;
    float3 out_emissive  = float3(0, 0, 0);
    float  out_ao        = 1.0;

#ifdef ALPHA_MODE_MASK
    clip(node_5_rgba.a - 0.5);
#endif

    // --- GBuffer packing (deferred path) ---
    // GBuffer0: Albedo.rgb + AO
    // GBuffer1: Normal (oct-encoded) + Roughness + Metallic
    // GBuffer2: Emissive.rgb

    return float4(out_albedo, out_ao);
    // Note: full GBuffer MRT output is handled by the engine's
    // GBuffer pass wrapper, which calls this PSMain and distributes
    // outputs across render targets.
}
```

### 4.4 ShaderVariantKey Integration

The transpiler automatically generates `#ifdef` guards for variant-dependent features:

| Graph Feature | Emitted Define | Condition |
|---------------|---------------|-----------|
| SampleTexture2D assigned to Albedo | `HAS_ALBEDO_TEX` | Texture connected to Master.Albedo chain |
| SampleTexture2D assigned to Normal | `HAS_NORMAL_TEX` | Texture connected to Master.Normal chain |
| Master Output Alpha != Opaque | `ALPHA_MODE_MASK` or `ALPHA_MODE_BLEND` | Selected blend mode |
| Any Skinning input node | `SKINNED` | Skinning node present in graph |
| Any instancing-aware node | `INSTANCED` | Instancing checkbox in Master Output |

Custom defines can be added via the Master Output node's "Custom Defines" section, which maps directly to the `.uzmat` `[variants]` block.

### 4.5 Texture Slot Binding

| Register | Purpose | Assigned by |
|----------|---------|-------------|
| `t0` | Albedo texture | First SampleTexture2D connected to Albedo |
| `t1` | Normal map | First SampleTexture2D connected to Normal |
| `t2` | Roughness/Metallic packed | Roughness or metallic texture |
| `t3` | AO texture | AO texture |
| `t4` | Emissive texture | Emissive texture |
| `t5` | User texture A | Additional SampleTexture2D nodes (in graph order) |
| `t6` | User texture B / Bone palette | Skinning bone palette when SKINNED |
| `t7` | Instance transforms | Instancing StructuredBuffer when INSTANCED |

The compiler assigns registers sequentially to SampleTexture2D nodes in topological order. If a graph uses > 6 user textures (t0-t5 range), it emits a compilation error: "Shader graph exceeds maximum 6 texture slots (t0-t5). t6 and t7 are reserved."

---

## 5. Real-Time Preview

### 5.1 Preview Pipeline

The preview viewport renders independently from the main scene viewport. It uses a dedicated IPC endpoint:

```json
shader_preview.render {
    "shader_path": "res://shaders/generated/brick_wall.hlsl",
    "mesh": "sphere",
    "environment": "studio",
    "camera": { "distance": 2.5, "yaw": 45.0, "pitch": 20.0 },
    "lights": [
        { "type": "directional", "dir": [0.5, -1.0, 0.5], "color": [1,1,1], "intensity": 1.5 }
    ],
    "material_params": {
        "tiling_scale": 2.0,
        "color_tint": [1.0, 0.9, 0.85],
        "roughness_boost": 0.1
    },
    "textures": {
        "t0": "res://textures/brick_albedo.png",
        "t1": "res://textures/brick_normal.png",
        "t2": "res://textures/brick_roughness.png"
    },
    "resolution": [256, 256]
}
```

Response: JPEG binary (same streaming path as main viewport).

### 5.2 Preview Mesh Options

| Mesh | Description | Use Case |
|------|-------------|----------|
| **Sphere** (default) | UV sphere, 32x32 segments | General PBR, shows curvature |
| **Cube** | Unit cube | Tiling/repeating patterns |
| **Plane** | 2x2 flat quad | Terrain, decals, UI materials |
| **Cylinder** | Radius 0.5, height 1.0 | Cylindrical UV mapping |
| **Torus** | Major 0.7, minor 0.3 | Shows UV seams, curvature variation |
| **Custom** | User picks .glb file | Preview on actual game mesh |

### 5.3 Environment Options

| Environment | Description |
|-------------|-------------|
| **Studio** (default) | Neutral grey, single key light + fill + rim (3-point) |
| **Outdoor** | IBL sky dome, directional sun |
| **Dark** | Near-black ambient, single spot light |
| **Custom HDRI** | User loads `.hdr` or `.exr` environment map |

### 5.4 Light Rig Editor

The preview supports up to 3 lights, configurable from the Properties panel when the Preview section is expanded:

```
  Preview Lights
  ==============
  Light 1: [Directional v]
    Dir:    X [-0.5] Y [-1.0] Z [0.5]
    Color:  [## FFFFFF]
    Intensity: [===o======] 1.5

  Light 2: [Point v]
    Pos:    X [2.0] Y [1.0] Z [0.0]
    Color:  [## FFD080]
    Intensity: [===o======] 0.8
    Radius:    [=======o==] 5.0

  Light 3: [Off v]
```

### 5.5 Channel Viewing

The preview can isolate individual material channels for debugging:

| Channel View | Display |
|-------------|---------|
| **Final** (default) | Fully lit, composited PBR result |
| **Albedo** | `float3(albedo)` only, no lighting |
| **Normal** | World-space normal remapped to RGB `(N * 0.5 + 0.5)` |
| **Roughness** | Greyscale `float3(roughness)` |
| **Metallic** | Greyscale `float3(metallic)` |
| **Emissive** | Emissive contribution only |
| **AO** | Greyscale `float3(ao)` |
| **UV** | `float3(uv.x, uv.y, 0)` checker pattern |
| **Depth** | Linear depth visualization |

### 5.6 Before/After Comparison

A split-screen slider lets the user compare the current (dirty) graph against the last saved version. The engine maintains two compiled PSOs -- one for the saved state, one for the live-edit state. The user drags a vertical divider across the preview.

```
+---------------------------+
|         |                 |
|  SAVED  |>|  LIVE EDIT    |
|         |                 |
+---------------------------+
        ^ drag this
```

---

## 6. File Format (.uzshader)

### 6.1 Format Design

The `.uzshader` file uses an INI-like text format, consistent with other UzEngine file formats (`.uzscene`, `.uzmat`, `.uzfx`). It is designed to be human-readable, diff-friendly in version control, and parseable by AI agents.

### 6.2 File Structure

```ini
[uzshader version=1]
name = "Brick Wall PBR"
surface_type = PBR
blend_mode = OPAQUE
two_sided = false
depth_write = true

; ================================================================
; NODES
; ================================================================

[node.1]
type = Input_UV
position = [-400, 0]

[node.2]
type = Input_WorldNormal
position = [-400, 200]

[node.3]
type = Input_ViewDirection
position = [-400, 400]

[node.4]
type = TilingOffset
position = [-200, 0]
; Per-node parameter overrides (non-default values only)
param.Tiling = [2.0, 2.0]
param.Offset = [0.0, 0.0]
; Parameter exposure — makes this a material parameter editable in Inspector
expose.Tiling = { name="tiling_scale", type="float", default=2.0, min=0.1, max=10.0 }

[node.5]
type = SampleTexture2D
position = [0, -50]
param.Texture = "res://textures/brick_albedo.png"
label = "Albedo Tex"

[node.6]
type = Multiply
position = [200, 0]

[node.7]
type = SampleNormalMap
position = [0, 150]
param.Texture = "res://textures/brick_normal.png"
param.Strength = 1.0
label = "Normal Map"

[node.8]
type = SampleTexture2D
position = [0, 350]
param.Texture = "res://textures/brick_roughness.png"
label = "Roughness Tex"

[node.9]
type = Add
position = [200, 350]

[node.10]
type = Constant_Float
position = [0, 450]
param.Value = 0.1
expose.Value = { name="roughness_boost", type="float", default=0.1, min=0.0, max=1.0 }

[node.11]
type = Constant_Float3
position = [0, -200]
param.Value = [1.0, 0.9, 0.85]
expose.Value = { name="color_tint", type="float3", default=[1.0, 0.9, 0.85] }

[node.100]
type = MasterOutput_PBR
position = [500, 100]

; ================================================================
; CONNECTIONS
; ================================================================

[connections]
; Format: source_node.output_pin -> dest_node.input_pin
1.UV -> 4.UV
4.UV -> 5.UV
4.UV -> 7.UV
4.UV -> 8.UV
5.RGB -> 6.A
11.Value -> 6.B
6.Result -> 100.Albedo
7.Normal -> 100.Normal
8.R -> 9.A
10.Value -> 9.B
9.Result -> 100.Roughness

; ================================================================
; COMMENTS / GROUPS
; ================================================================

[comment.1]
text = "Albedo pipeline: sample + tint"
position = [-50, -250]
size = [500, 300]
color = [0.2, 0.3, 0.5, 0.15]

[comment.2]
text = "Surface roughness with artist boost"
position = [-50, 300]
size = [400, 250]
color = [0.5, 0.3, 0.2, 0.15]

; ================================================================
; PREVIEW SETTINGS (not part of material, editor-only)
; ================================================================

[preview]
mesh = sphere
environment = studio
camera_distance = 2.5
camera_yaw = 45.0
camera_pitch = 20.0
```

### 6.3 Node Type Registry

Each `type` string maps to a registered node definition. The format is:

```
Category_NodeName
```

Examples: `Input_UV`, `Math_Add`, `Texture_SampleTexture2D`, `Utility_Lerp`, `Custom_HLSL`, `MasterOutput_PBR`.

For the short form (as shown above), the category prefix can be omitted when unambiguous (e.g., `Add` instead of `Math_Add`).

### 6.4 Parameter Exposure

Any node parameter can be "exposed" to the material inspector using the `expose.*` syntax:

```ini
expose.Value = { name="roughness_boost", type="float", default=0.1, min=0.0, max=1.0 }
```

Exposed parameters:
- Appear in the material's `.uzmat` as editable properties
- Are bound in a `MaterialParams` cbuffer (register `b2`) in the generated HLSL
- Can be overridden per-material-instance without recompiling the shader

### 6.5 Compile Cache (.uzshader.cache)

After successful compilation, a binary cache file is written alongside the graph:

```
shaders/graphs/brick_wall.uzshader.cache
```

Contents (binary, not human-readable):
- Graph content hash (SHA-256 of the `.uzshader` text)
- Compiled DXIL bytecode for VS and PS
- DXC reflection data (inputs, cbuffers, texture slots)
- Timestamp of compilation

On next open, if the graph hash matches the cache, compilation is skipped. This provides instant load for unchanged graphs.

---

## 7. Compilation Pipeline

### 7.1 Graph-to-HLSL Transpilation Algorithm

The `ShaderGraphCompiler` performs the following steps:

```
Step 1: Parse .uzshader → in-memory NodeGraph
Step 2: Validate graph connectivity (Master Output reachable, no cycles)
Step 3: Topological sort (nodes ordered by dependency)
Step 4: Type inference (propagate types from outputs to inputs, apply coercion)
Step 5: Dead node elimination (remove nodes not reachable from Master Output)
Step 6: Texture slot assignment (allocate t0..t5 in topological order)
Step 7: Exposed parameter collection (gather all `expose.*` entries → MaterialParams cbuffer)
Step 8: Emit HLSL preamble (includes, cbuffers, structs)
Step 9: Emit VSMain (fixed template + WorldPositionOffset from graph if connected)
Step 10: Emit PSMain body (one variable per node output, in topological order)
Step 11: Emit Master Output assembly (pack into GBuffer or forward output)
Step 12: Write .hlsl file
Step 13: Write .uzmat file (if material params changed)
Step 14: Invoke DXC validation
Step 15: On success: update .uzshader.cache, trigger shader.reload IPC
Step 16: On failure: report errors, map DXC line numbers back to graph nodes
```

### 7.2 Topological Sort

The graph is a DAG (directed acyclic graph). Connections flow left-to-right, from source outputs to destination inputs. The topological sort produces an evaluation order where every node's inputs are computed before the node itself.

```
Algorithm: Kahn's algorithm (BFS-based)
1. Compute in-degree for each node (number of input connections).
2. Enqueue all nodes with in-degree 0 (Input nodes, Constants).
3. While queue is not empty:
   a. Dequeue node N.
   b. Append N to sorted list.
   c. For each output pin of N:
      - For each connection to downstream node M:
        - Decrement M's in-degree.
        - If M's in-degree reaches 0, enqueue M.
4. If sorted list length != node count: cycle detected → error.
```

### 7.3 Type Inference

After topological sort, types are inferred forward:

1. Input nodes have fixed output types (e.g., UV outputs `float2`).
2. Constant nodes have explicit types from their parameters.
3. Math nodes infer output type from the widest input type (e.g., `Add(float3, float)` outputs `float3`; the `float` input gets implicit splat coercion).
4. The Master Output node's pins have fixed expected types. If the connected output does not match, coercion is inserted (or an error is reported if coercion is impossible).

### 7.4 HLSL Codegen — Variable Naming

Each node's output is assigned a unique HLSL variable:

```
node_<nodeId>_<outputPinName>
```

Examples:
- Node 5, output "RGB": `node_5_rgb`
- Node 9, output "Result": `node_9_result`

This naming scheme:
- Ensures uniqueness
- Is human-readable in the Output panel
- Allows error messages to reference specific nodes by ID

### 7.5 DXC Integration

After writing the `.hlsl` file, the compiler invokes DXC for validation:

```
dxc.exe -T vs_6_0 -E VSMain -I shaders/include -D <variant_defines...> <path>.hlsl
dxc.exe -T ps_6_0 -E PSMain -I shaders/include -D <variant_defines...> <path>.hlsl
```

DXC is invoked via the existing `ShaderCompiler::compile_variant()` API (see `shader-variants.md`). The compile happens on a background thread; the editor receives results asynchronously.

### 7.6 Error Recovery and Node Highlighting

When DXC reports an error, the compiler maps the error line number back to the originating graph node:

1. During codegen, the compiler maintains a `line_to_node` map: `HashMap<uint32_t, uint32_t>` (HLSL line number to node ID).
2. When an error arrives at line L, the compiler looks up `line_to_node[L]` to find the responsible node.
3. The webview receives a message: `{ type: "error", nodeId: 7, line: 52, message: "..." }`.
4. The canvas highlights node 7 with a red border and a tooltip showing the error.
5. The Output panel's Errors tab shows the same information, and clicking the error scrolls the canvas to center on the offending node.

**Common error scenarios:**

| Scenario | Visual Feedback |
|---------|----------------|
| Type mismatch (coercion failed) | Red border on destination node, wire turns red |
| Texture slot overflow (>6 textures) | Red border on the 7th SampleTexture2D node |
| Custom HLSL syntax error | Red border on Custom HLSL node, error tooltip |
| Cycle detected | All nodes in the cycle get red borders |
| Disconnected Master Output pin (required) | Orange warning border on Master Output |

### 7.7 Hot-Reload Integration

On successful compilation:

1. The compiler writes the `.hlsl` file.
2. The compiler calls `shader.reload({ path: "res://shaders/generated/brick_wall.hlsl" })` via IPC.
3. The engine's file watcher detects the change (or responds to the IPC call directly).
4. The engine recompiles all active variants of this shader (see `shader-variants.md` hot-reload section).
5. PSOs are swapped atomically between frames.
6. The preview viewport and main viewport both update.

**Auto-compile behavior** (configurable in Settings):

| Setting | Behavior |
|---------|----------|
| `auto_compile = "on_change"` (default) | Recompile 500ms after last graph edit (debounced) |
| `auto_compile = "on_save"` | Recompile only when user presses Ctrl+S |
| `auto_compile = "manual"` | Recompile only when user clicks Compile or presses F5 |

---

## 8. UX & Accessibility

### 8.1 Complete Keyboard Shortcut Table

| Shortcut | Action |
|----------|--------|
| `Spacebar` or `Q` | Open search-to-add overlay |
| `F` | Frame selection (zoom to fit) |
| `Delete` / `Backspace` | Delete selected nodes/connections |
| `Ctrl+A` | Select all nodes |
| `Ctrl+C` | Copy selected nodes |
| `Ctrl+V` | Paste nodes at cursor position |
| `Ctrl+X` | Cut selected nodes |
| `Ctrl+D` | Duplicate selected nodes |
| `Ctrl+Z` | Undo |
| `Ctrl+Shift+Z` | Redo |
| `Ctrl+S` | Save graph + recompile |
| `Ctrl+E` | Export HLSL |
| `Ctrl+G` | Group selection into sub-graph |
| `Ctrl+Shift+G` | Toggle grid snapping |
| `F5` | Force recompile |
| `1` | Align selected nodes horizontally |
| `2` | Align selected nodes vertically |
| `C` | Create comment box around selection |
| `H` | Toggle minimap visibility |
| `Tab` | Switch focus: Canvas -> Inspector -> Preview (cycle) |
| `Escape` | Deselect all / close search overlay |
| `Home` | Zoom to fit entire graph |

### 8.2 Context Menus

**Canvas Context Menu (right-click empty area):**
```
  Add Node...        (opens search overlay)
  ────────────────
  Paste              Ctrl+V
  Select All         Ctrl+A
  ────────────────
  Frame All          Home
  Toggle Minimap     H
  Toggle Grid Snap   Ctrl+Shift+G
  ────────────────
  Add Comment Box    C
```

**Node Context Menu (right-click node):**
```
  Cut                Ctrl+X
  Copy               Ctrl+C
  Duplicate          Ctrl+D
  Delete             Del
  ────────────────
  Disconnect All Inputs
  Disconnect All Outputs
  Disconnect All
  ────────────────
  Collapse / Expand
  Preview This Node  (isolate this node's output in preview)
  ────────────────
  Create Sub-Graph from Selection   Ctrl+G
  Convert to Custom HLSL            (extracts function body)
  ────────────────
  AI: Explain Node        (calls shader_assist)
  AI: Suggest Connections  (calls shader_assist)
```

**Connection Context Menu (right-click wire):**
```
  Insert Reroute Point
  Delete Connection
```

### 8.3 Search-to-Add Overlay

Pressing `Spacebar` or `Q` opens a floating search panel over the canvas:

```
+--[Add Node]-----------+
| Search: [per______]   |
|                        |
|  > Perlin Noise        |
|    Perlin Noise (3D)   |
|  > Power               |
|  > Properties...       |
|                        |
| Recent:                |
|    Lerp                |
|    SampleTexture2D     |
|    Multiply            |
+------------------------+
```

- Fuzzy text matching (e.g., "smpltx" matches "SampleTexture2D")
- Shows category breadcrumb for each result
- Arrow keys to navigate, Enter to add at cursor position
- "Recent" section shows last 8 nodes added (persisted per-session)
- Results are ranked by relevance, then by frequency of use

### 8.4 Minimap

A small (200x150 px) overview of the entire graph, shown in the bottom-right corner of the canvas. Clicking on the minimap jumps to that area. The current viewport is shown as a rectangle overlay.

Toggle visibility with `H` key.

### 8.5 Comment Boxes and Grouping

**Comment Boxes:**
- Semi-transparent colored rectangles behind nodes
- Title text at the top
- Nodes inside a comment box move together when the box is dragged (optional: toggle with checkbox on the comment)
- Created by selecting nodes and pressing `C`, or from the context menu

**Node Groups (Sub-Graphs):**
- `Ctrl+G` converts a selection into a reusable sub-graph
- The sub-graph appears as a single node with auto-detected inputs/outputs
- Double-clicking a sub-graph node opens it in a new tab for editing
- Changes to a sub-graph propagate to all graphs that reference it

---

## 9. AI Integration

### 9.1 MCP Tool: `shader_graph_assist`

A new MCP tool extending the existing `shader_assist` for graph-level operations:

```typescript
{
  name: "shader_graph_assist",
  description: "AI-assisted operations on visual shader graphs: explain, generate, optimize",
  inputSchema: {
    type: "object",
    properties: {
      action: {
        type: "string",
        enum: ["explain_node", "explain_graph", "generate_subgraph",
               "optimize", "fix_errors", "suggest_connections"],
        description: "The AI action to perform"
      },
      graph_path: {
        type: "string",
        description: "res:// path to the .uzshader file"
      },
      node_id: {
        type: "number",
        description: "Target node ID (for explain_node, suggest_connections)"
      },
      description: {
        type: "string",
        description: "Natural language description (for generate_subgraph)"
      }
    },
    required: ["action", "graph_path"]
  }
}
```

### 9.2 AI Explain Node

When the user right-clicks a node and selects "AI: Explain Node":

1. The extension reads the node's type, connections, and parameter values from the `.uzshader`.
2. Calls `shader_graph_assist` with `action: "explain_node"`.
3. The MCP server sends the node context to the LLM with a system prompt containing the HLSL function the node maps to.
4. Returns a natural-language explanation displayed as a tooltip or side panel.

**Example output:**
```
This "SampleNormalMap" node reads a tangent-space normal map from the texture
"brick_normal.png" and transforms it into world-space using the TBN matrix
constructed from the interpolated vertex tangent and normal. The Strength
parameter (currently 1.0) scales the X and Y components of the normal before
transformation -- lower values flatten the bumps.
```

### 9.3 AI Generate Sub-Graph

The user can describe a desired effect in natural language, and the AI generates a sub-graph:

**Example:**
```
User: "Create a pulsing emissive effect that cycles between two colors based on time"
```

The MCP server:
1. Reads the full `.uzshader` format specification.
2. Asks the LLM to generate a `.uzshader` fragment (nodes + connections).
3. Validates the fragment.
4. Inserts the nodes into the current graph, connected to the Master Output's Emissive pin.

**Generated fragment:**
```ini
[node.200]
type = Global_Time
position = [-200, 600]

[node.201]
type = Math_Multiply
position = [0, 600]

[node.202]
type = Constant_Float
position = [-200, 700]
param.Value = 2.0
label = "Pulse Speed"

[node.203]
type = Math_Sin
position = [200, 600]

[node.204]
type = Utility_Remap
position = [400, 600]
param.InMin = -1.0
param.InMax = 1.0
param.OutMin = 0.0
param.OutMax = 1.0

[node.205]
type = Constant_Float3
position = [200, 700]
param.Value = [1.0, 0.2, 0.0]
label = "Color A"
expose.Value = { name="pulse_color_a", type="float3", default=[1.0,0.2,0.0] }

[node.206]
type = Constant_Float3
position = [200, 800]
param.Value = [0.0, 0.5, 1.0]
label = "Color B"
expose.Value = { name="pulse_color_b", type="float3", default=[0.0,0.5,1.0] }

[node.207]
type = Utility_Lerp
position = [600, 650]

; connections within fragment:
; 200.Time -> 201.A
; 202.Value -> 201.B
; 201.Result -> 203.In
; 203.Result -> 204.In
; 204.Result -> 207.Alpha
; 205.Value -> 207.A
; 206.Value -> 207.B
; 207.Result -> 100.Emissive   (Master Output)
```

### 9.4 AI-Powered Error Explanation

When DXC reports compilation errors, clicking "AI: Explain Error" on the error entry sends the error message, the generated HLSL context (surrounding lines), and the node graph context to the LLM. The response is a plain-language explanation and suggested fix.

**Example:**
```
Error: "error X3004: undeclared identifier 'node_5_rgb'" at line 78

AI Explanation:
Node 5 (SampleTexture2D "Albedo Tex") outputs a float4 named "RGBA",
but the connection to Node 6 (Multiply) expects an input named "RGB" which
would extract the .rgb swizzle. The transpiler appears to have a naming
mismatch. This is likely a compiler bug -- as a workaround, try inserting
a Split node between the texture sample and the Multiply, then connect
the RGB output.
```

### 9.5 AI Optimize Graph

The "optimize" action analyzes the graph for:
- Redundant nodes (e.g., `Multiply(x, 1.0)` can be eliminated)
- Expensive operations that could be simplified (e.g., `pow(x, 2.0)` to `x * x`)
- Texture samples that could be shared (same texture + same UV = merge)
- Nodes that could be computed in the vertex shader instead of pixel shader

The LLM returns a list of suggested optimizations, each with a "before" and "after" sub-graph.

---

## 10. Implementation Notes

### 10.1 Technology Stack

| Component | Technology | Location |
|-----------|-----------|----------|
| **Graph Editor Frontend** | TypeScript + HTML5 Canvas (custom rendering) | `vscode-extension/src/shader-editor/` |
| **Node Rendering** | HTML5 Canvas 2D API (not DOM elements -- performance) | `shader-editor/canvas-renderer.ts` |
| **Wire Rendering** | Bezier curves on Canvas 2D | `shader-editor/wire-renderer.ts` |
| **Monaco Code View** | Embedded Monaco editor (VSCode built-in) | `shader-editor/hlsl-view.ts` |
| **Graph Data Model** | TypeScript classes | `shader-editor/model/` |
| **Serializer** | TypeScript INI parser/writer | `shader-editor/serializer/` |
| **HLSL Transpiler** | C++ `ShaderGraphCompiler` | `src/renderer/shader_graph/` |
| **Preview Renderer** | DX12 (engine-side, dedicated render target) | `src/renderer/shader_preview.cpp` |
| **IPC** | WebSocket JSON (same as all other panels) | Existing IPC infrastructure |

### 10.2 C++ Backend Class Hierarchy

```
src/renderer/shader_graph/
    shader_graph.h              -- NodeGraph data structure
    shader_graph_node.h         -- Base ShaderGraphNode class
    shader_graph_pin.h          -- Pin definition (type, direction, name)
    shader_graph_connection.h   -- Connection (source pin -> dest pin)
    shader_graph_compiler.h     -- Graph -> HLSL transpiler
    shader_graph_serializer.h   -- .uzshader reader/writer
    shader_graph_validator.h    -- Cycle detection, type checking
    node_registry.h             -- Registered node types + metadata
    nodes/                      -- Built-in node implementations
        input_nodes.h
        math_nodes.h
        texture_nodes.h
        lighting_nodes.h
        uv_nodes.h
        noise_nodes.h
        utility_nodes.h
        constant_nodes.h
        global_nodes.h
        custom_hlsl_node.h
        master_output_node.h
```

### 10.3 Core Data Structures (C++)

```cpp
// src/renderer/shader_graph/shader_graph_pin.h

enum class PinType : uint8_t {
    FLOAT,
    FLOAT2,
    FLOAT3,
    FLOAT4,
    MATRIX,
    TEXTURE2D,
    TEXTURECUBE,
    SAMPLER,
    BOOL,
    INT
};

enum class PinDirection : uint8_t {
    INPUT,
    OUTPUT
};

struct PinDef {
    StringId         name;        // "UV", "Result", "Albedo", etc.
    PinType          type;
    PinDirection     direction;
    bool             required;    // If true, must be connected (Master Output pins)
    // Default value for unconnected input pins (stored as float4, interpreted by type)
    float            default_value[4];
};

// src/renderer/shader_graph/shader_graph_node.h

struct ShaderGraphNode {
    uint32_t         id;
    StringId         type_name;   // "SampleTexture2D", "Add", etc.
    float            position[2]; // Canvas position (x, y)
    Array<PinDef>    inputs;
    Array<PinDef>    outputs;
    // Parameter values (key-value pairs, types depend on node type)
    HashMap<StringId, Variant> params;
    // Exposed parameters (material-level overrides)
    HashMap<StringId, ExposedParam> exposed;
    // Optional user label
    const char*      label;
};

struct ExposedParam {
    StringId  material_name;    // Name in .uzmat and MaterialParams cbuffer
    PinType   type;
    float     default_value[4];
    float     min_value;
    float     max_value;
};

// src/renderer/shader_graph/shader_graph_connection.h

struct Connection {
    uint32_t  src_node_id;
    StringId  src_pin_name;
    uint32_t  dst_node_id;
    StringId  dst_pin_name;
};

// src/renderer/shader_graph/shader_graph.h

struct NodeGraph {
    StringId              name;
    uint32_t              version;        // File format version
    SurfaceType           surface_type;   // PBR, Unlit, PostProcess
    BlendMode             blend_mode;     // Opaque, Mask, Blend, Additive
    bool                  two_sided;
    bool                  depth_write;

    Array<ShaderGraphNode> nodes;
    Array<Connection>      connections;
    Array<CommentBox>      comments;

    // Derived during compilation
    Array<uint32_t>        topo_order;    // Node IDs in topological order
    uint32_t               master_output_id;

    // Find node by ID
    ShaderGraphNode* find_node(uint32_t id);
    // Get all connections to a specific input pin
    const Connection* find_input_connection(uint32_t node_id, StringId pin_name) const;
    // Get all connections from a specific output pin
    Array<const Connection*> find_output_connections(uint32_t node_id, StringId pin_name) const;
};
```

### 10.4 ShaderGraphCompiler (C++)

```cpp
// src/renderer/shader_graph/shader_graph_compiler.h

struct CompileOptions {
    bool   emit_comments;       // Include /* Node 5: SampleTexture2D */ comments
    bool   emit_debug_colors;   // Add #ifdef DEBUG_NODE_<id> blocks for isolated preview
    bool   optimize_constants;  // Fold constant expressions
    Arena* scratch;             // Scratch allocator for temp strings
};

struct CompileOutput {
    bool                  ok;
    String                hlsl_vs;        // Vertex shader source
    String                hlsl_ps;        // Pixel shader source
    String                uzmat;          // Generated .uzmat content
    Array<TextureSlot>    texture_slots;  // {register, texture_path}
    Array<MaterialParam>  material_params;// Exposed parameters
    Array<Diagnostic>     diagnostics;    // Errors and warnings
    HashMap<uint32_t, uint32_t> line_to_node; // HLSL line -> node ID
};

struct TextureSlot {
    uint32_t  register_index;   // 0..5
    StringId  texture_path;     // "res://textures/brick_albedo.png"
    StringId  node_label;       // "Albedo Tex" (for diagnostics)
};

struct MaterialParam {
    StringId  name;             // "tiling_scale"
    PinType   type;             // FLOAT, FLOAT3, etc.
    float     default_value[4];
    float     min_value;
    float     max_value;
};

class ShaderGraphCompiler {
public:
    // Main entry point: compile a .uzshader into HLSL + .uzmat
    CompileOutput compile(const NodeGraph& graph, const CompileOptions& options);

private:
    // Phase 1: Validate graph structure
    bool validate(const NodeGraph& graph, Array<Diagnostic>& out_diag);

    // Phase 2: Topological sort
    bool topological_sort(const NodeGraph& graph, Array<uint32_t>& out_order,
                          Array<Diagnostic>& out_diag);

    // Phase 3: Type inference and coercion insertion
    void infer_types(NodeGraph& graph, const Array<uint32_t>& topo_order);

    // Phase 4: Dead node elimination
    void eliminate_dead_nodes(NodeGraph& graph, uint32_t master_id);

    // Phase 5: Assign texture slots
    void assign_texture_slots(const NodeGraph& graph, const Array<uint32_t>& topo_order,
                              Array<TextureSlot>& out_slots, Array<Diagnostic>& out_diag);

    // Phase 6: Emit HLSL
    void emit_hlsl(const NodeGraph& graph, const Array<uint32_t>& topo_order,
                   const Array<TextureSlot>& slots, const Array<MaterialParam>& params,
                   const CompileOptions& options, String& out_vs, String& out_ps,
                   HashMap<uint32_t, uint32_t>& out_line_map);

    // Phase 7: Emit .uzmat
    void emit_uzmat(const NodeGraph& graph, const Array<TextureSlot>& slots,
                    const Array<MaterialParam>& params, String& out_uzmat);

    // Helper: emit HLSL for a single node
    void emit_node(const ShaderGraphNode& node, const NodeGraph& graph,
                   StringBuilder& sb, uint32_t& line_counter,
                   HashMap<uint32_t, uint32_t>& line_map);

    // Helper: get HLSL variable name for a node output
    String get_var_name(uint32_t node_id, StringId pin_name);

    // Helper: emit type coercion code
    String emit_coercion(PinType src_type, PinType dst_type, const String& expr);
};
```

### 10.5 Node Registry (C++)

```cpp
// src/renderer/shader_graph/node_registry.h

struct NodeTypeDef {
    StringId          type_name;    // "Add", "SampleTexture2D", etc.
    StringId          category;     // "Math", "Texture", etc.
    StringId          display_name; // "Add (A + B)"
    StringId          description;  // "Component-wise addition of two values"
    Array<PinDef>     default_inputs;
    Array<PinDef>     default_outputs;
    // HLSL template: placeholders are {IN_A}, {IN_B}, {OUT_RESULT}
    const char*       hlsl_template;
    // Whether this node has custom codegen (overrides hlsl_template)
    bool              custom_codegen;
    // Icon path for the palette
    const char*       icon;
};

class NodeRegistry {
public:
    void register_builtin_nodes();
    const NodeTypeDef* find(StringId type_name) const;
    Array<const NodeTypeDef*> get_by_category(StringId category) const;
    Array<const NodeTypeDef*> search(const char* query) const;

private:
    HashMap<StringId, NodeTypeDef> registry_;
};

// Example registration:
void NodeRegistry::register_builtin_nodes() {
    register_node({
        .type_name    = "Add"_sid,
        .category     = "Math"_sid,
        .display_name = "Add (A + B)"_sid,
        .description  = "Component-wise addition"_sid,
        .default_inputs = {
            { "A"_sid, PinType::FLOAT, PinDirection::INPUT, false, {0,0,0,0} },
            { "B"_sid, PinType::FLOAT, PinDirection::INPUT, false, {0,0,0,0} }
        },
        .default_outputs = {
            { "Result"_sid, PinType::FLOAT, PinDirection::OUTPUT, false, {0,0,0,0} }
        },
        .hlsl_template = "{OUT_Result} = {IN_A} + {IN_B};",
        .custom_codegen = false,
        .icon = "icons/math_add.svg"
    });
    // ... register all other nodes ...
}
```

### 10.6 TypeScript Frontend Architecture

```
vscode-extension/src/shader-editor/
    ShaderEditorProvider.ts         -- WebviewPanel lifecycle, registers commands
    ShaderEditorWebview.ts          -- HTML template, message passing
    model/
        NodeGraph.ts                -- In-memory graph model (mirrors C++ struct)
        Node.ts                     -- Node class with pins, params
        Pin.ts                      -- Pin definition
        Connection.ts               -- Connection between pins
    canvas/
        CanvasRenderer.ts           -- Main render loop (requestAnimationFrame)
        NodeRenderer.ts             -- Draw individual nodes on Canvas 2D
        WireRenderer.ts             -- Draw Bezier connection wires
        SelectionManager.ts         -- Box select, click select, multi-select
        PanZoomController.ts        -- Canvas pan/zoom via mouse/scroll
        MinimapRenderer.ts          -- Minimap overlay
        ContextMenu.ts              -- Right-click menus
        SearchOverlay.ts            -- Spacebar search-to-add UI
    inspector/
        PropertiesPanel.ts          -- Node property inspector
        PreviewPanel.ts             -- 3D preview viewport (JPEG from engine)
    output/
        HlslView.ts                 -- Monaco editor showing generated HLSL
        ErrorList.ts                -- Error/warning list with node linking
        StatsView.ts                -- Shader statistics display
    serializer/
        UzShaderParser.ts           -- Parse .uzshader INI format
        UzShaderWriter.ts           -- Serialize graph to .uzshader text
    commands/
        EditorCommands.ts           -- Undo/redo commands for graph mutations
        AddNodeCommand.ts
        DeleteNodeCommand.ts
        MoveNodeCommand.ts
        ConnectPinsCommand.ts
        DisconnectPinsCommand.ts
        SetParamCommand.ts
        GroupToSubGraphCommand.ts
    ipc/
        ShaderPreviewIpc.ts         -- IPC calls for preview rendering
        ShaderCompileIpc.ts         -- IPC calls for compilation
```

### 10.7 EditorCommand Integration (Undo/Redo)

All graph mutations go through the existing `UndoStack` from the undo-redo system (see `undo-redo.md`). Each command captures the before/after state for reversal.

```typescript
// vscode-extension/src/shader-editor/commands/AddNodeCommand.ts

class AddNodeCommand implements EditorCommand {
    readonly id = crypto.randomUUID();
    readonly label: string;
    private nodeSnapshot: SerializedNode;

    constructor(
        private readonly graph: NodeGraph,
        private readonly nodeType: string,
        private readonly position: [number, number]
    ) {
        this.label = `Add ${nodeType}`;
    }

    async execute(): Promise<void> {
        const node = this.graph.addNode(this.nodeType, this.position);
        this.nodeSnapshot = node.serialize();
        // No IPC call needed — graph is local to the editor.
        // Compilation happens on save, not on individual edits.
    }

    async undo(): Promise<void> {
        this.graph.removeNode(this.nodeSnapshot.id);
    }
}
```

```typescript
// vscode-extension/src/shader-editor/commands/ConnectPinsCommand.ts

class ConnectPinsCommand implements EditorCommand {
    readonly id = crypto.randomUUID();
    readonly label: string;
    private previousConnection: Connection | null = null;

    constructor(
        private readonly graph: NodeGraph,
        private readonly srcNodeId: number,
        private readonly srcPin: string,
        private readonly dstNodeId: number,
        private readonly dstPin: string
    ) {
        this.label = `Connect ${srcPin} -> ${dstPin}`;
    }

    async execute(): Promise<void> {
        // An input pin can only have one connection. If already connected, save for undo.
        this.previousConnection = this.graph.findInputConnection(this.dstNodeId, this.dstPin);
        if (this.previousConnection) {
            this.graph.disconnect(this.previousConnection);
        }
        this.graph.connect(this.srcNodeId, this.srcPin, this.dstNodeId, this.dstPin);
    }

    async undo(): Promise<void> {
        this.graph.disconnect({ srcNodeId: this.srcNodeId, srcPin: this.srcPin,
                                dstNodeId: this.dstNodeId, dstPin: this.dstPin });
        if (this.previousConnection) {
            this.graph.connect(
                this.previousConnection.srcNodeId, this.previousConnection.srcPin,
                this.previousConnection.dstNodeId, this.previousConnection.dstPin
            );
        }
    }
}
```

### 10.8 IPC Protocol Extensions

New IPC handlers required for the shader editor:

```json
shader_graph.compile {
    "path": "res://shaders/graphs/brick_wall.uzshader"
}
--> {
    "ok": true,
    "hlsl_path": "res://shaders/generated/brick_wall.hlsl",
    "uzmat_path": "res://materials/brick_wall.uzmat",
    "texture_slots": [
        { "register": 0, "path": "res://textures/brick_albedo.png" },
        { "register": 1, "path": "res://textures/brick_normal.png" },
        { "register": 2, "path": "res://textures/brick_roughness.png" }
    ],
    "material_params": [
        { "name": "tiling_scale", "type": "float", "value": 2.0 },
        { "name": "color_tint", "type": "float3", "value": [1.0, 0.9, 0.85] },
        { "name": "roughness_boost", "type": "float", "value": 0.1 }
    ],
    "stats": {
        "instruction_count_vs": 24,
        "instruction_count_ps": 67,
        "texture_samples": 3,
        "register_count": 12,
        "estimated_gpu_cycles": 42
    },
    "diagnostics": [],
    "elapsed_ms": 85
}

shader_graph.validate {
    "path": "res://shaders/graphs/brick_wall.uzshader"
}
--> {
    "ok": true,
    "warnings": [
        { "node_id": 3, "message": "Input_ViewDirection is not connected to any output" }
    ]
}

shader_preview.render {
    "shader_path": "res://shaders/generated/brick_wall.hlsl",
    "mesh": "sphere",
    "environment": "studio",
    "camera": { "distance": 2.5, "yaw": 45.0, "pitch": 20.0 },
    "lights": [ ... ],
    "material_params": { ... },
    "textures": { ... },
    "resolution": [256, 256],
    "channel": "final"
}
--> JPEG binary

shader_preview.compare {
    "shader_path_a": "res://shaders/generated/brick_wall.hlsl",
    "shader_path_b": "res://shaders/generated/brick_wall_saved.hlsl",
    "split_position": 0.5,
    ...
}
--> JPEG binary (split-screen composite)
```

### 10.9 Performance Considerations

| Concern | Mitigation |
|---------|-----------|
| Canvas rendering with many nodes | Frustum culling: only draw nodes visible in viewport. Nodes off-screen are skipped. |
| Wire rendering (many curves) | Batch Bezier curve rendering. Use simplified straight lines at zoom < 0.5x. |
| Auto-compile latency | 500ms debounce. Compile on background thread. Use cache for unchanged sub-graphs. |
| Preview frame rate | Preview is 256x256 by default (lower than main viewport). Throttle to 30 FPS when editor is not focused. |
| Large graphs (>200 nodes) | Minimap helps navigation. Sub-graphs reduce visual clutter. Dead node elimination keeps generated HLSL lean. |
| Memory | Node graph is lightweight (~100 bytes/node). 500-node graph = ~50 KB. Negligible. |
| HLSL codegen size | One variable per node output. 200-node graph produces ~500 lines of HLSL. DXC handles this in < 100ms. |

### 10.10 Accessibility

| Feature | Implementation |
|---------|---------------|
| Keyboard-only navigation | Tab cycles focus between Canvas, Inspector, Preview. Arrow keys move selected nodes. Enter adds node from search. |
| High contrast mode | Honors VSCode's high-contrast theme. Node colors use sufficient contrast ratios. Wire colors are distinguishable in all modes. |
| Screen reader | Node selection announces node type, connected pins, and parameter values via ARIA live regions in the webview. |
| Color-blind support | Wire types use both color AND pattern (solid, dashed, dotted) to distinguish types. Pin shapes (circle, square, diamond, triangle) provide non-color differentiation. |
| Zoom range | 0.25x to 4.0x covers low-vision needs. Ctrl+= and Ctrl+- for keyboard zoom. |

---

## Appendix A: Complete Node Type List

| # | Type ID | Category | Inputs | Outputs |
|---|---------|----------|--------|---------|
| 1 | `Input_UV` | Input | channel (int) | UV (float2) |
| 2 | `Input_WorldPosition` | Input | -- | WorldPos (float3) |
| 3 | `Input_WorldNormal` | Input | -- | Normal (float3) |
| 4 | `Input_Tangent` | Input | -- | Tangent (float3) |
| 5 | `Input_Bitangent` | Input | -- | Bitangent (float3) |
| 6 | `Input_VertexColor` | Input | -- | Color (float4) |
| 7 | `Input_ViewDirection` | Input | -- | ViewDir (float3) |
| 8 | `Input_ScreenPosition` | Input | -- | ScreenPos (float4) |
| 9 | `Input_Depth` | Input | -- | Depth (float) |
| 10 | `Input_CameraPosition` | Input | -- | CamPos (float3) |
| 11 | `Math_Add` | Math | A, B | Result |
| 12 | `Math_Subtract` | Math | A, B | Result |
| 13 | `Math_Multiply` | Math | A, B | Result |
| 14 | `Math_Divide` | Math | A, B | Result |
| 15 | `Math_Power` | Math | Base, Exp | Result |
| 16 | `Math_Sqrt` | Math | In | Result |
| 17 | `Math_Abs` | Math | In | Result |
| 18 | `Math_Negate` | Math | In | Result |
| 19 | `Math_Floor` | Math | In | Result |
| 20 | `Math_Ceil` | Math | In | Result |
| 21 | `Math_Frac` | Math | In | Result |
| 22 | `Math_Round` | Math | In | Result |
| 23 | `Math_Sign` | Math | In | Result |
| 24 | `Math_Step` | Math | Edge, In | Result |
| 25 | `Math_SmoothStep` | Math | Min, Max, In | Result |
| 26 | `Math_Clamp` | Math | In, Min, Max | Result |
| 27 | `Math_Saturate` | Math | In | Result |
| 28 | `Math_Min` | Math | A, B | Result |
| 29 | `Math_Max` | Math | A, B | Result |
| 30 | `Math_Dot` | Math | A (vec), B (vec) | Result (float) |
| 31 | `Math_Cross` | Math | A (float3), B (float3) | Result (float3) |
| 32 | `Math_Normalize` | Math | In (vec) | Result (vec) |
| 33 | `Math_Length` | Math | In (vec) | Result (float) |
| 34 | `Math_Distance` | Math | A (vec), B (vec) | Result (float) |
| 35 | `Math_Reflect` | Math | I (vec), N (vec) | Result (vec) |
| 36 | `Math_Refract` | Math | I, N, IOR | Result |
| 37 | `Math_OneMinus` | Math | In | Result |
| 38 | `Math_Remap` | Math | In, InMin, InMax, OutMin, OutMax | Result |
| 39 | `Math_Sin` | Math | In | Result |
| 40 | `Math_Cos` | Math | In | Result |
| 41 | `Math_Tan` | Math | In | Result |
| 42 | `Math_Atan2` | Math | Y, X | Result (float) |
| 43 | `Math_Lerp` | Math | A, B, Alpha | Result |
| 44 | `Texture_SampleTexture2D` | Texture | Texture, UV, Sampler | RGBA, R, G, B, A |
| 45 | `Texture_SampleTextureCube` | Texture | Texture, Dir, Sampler | RGBA |
| 46 | `Texture_SampleNormalMap` | Texture | Texture, UV, Sampler, Strength | Normal (float3) |
| 47 | `Texture_Triplanar` | Texture | Texture, WorldPos, Normal, Sharpness | Color (float3) |
| 48 | `Texture_Parallax` | Texture | HeightMap, UV, ViewDirTS, Scale, Steps | UV (float2) |
| 49 | `Texture_FlipBook` | Texture | Texture, UV, Rows, Cols, Frame | RGBA |
| 50 | `Texture_TextureSize` | Texture | Texture | Size (float2), InvSize (float2) |
| 51 | `Lighting_NdotL` | Lighting | Normal, LightDir | Result (float) |
| 52 | `Lighting_NdotV` | Lighting | Normal, ViewDir | Result (float) |
| 53 | `Lighting_NdotH` | Lighting | Normal, HalfVec | Result (float) |
| 54 | `Lighting_HdotV` | Lighting | HalfVec, ViewDir | Result (float) |
| 55 | `Lighting_Fresnel` | Lighting | NdotV, F0 | Result (float3) |
| 56 | `Lighting_FresnelEffect` | Lighting | Normal, ViewDir, Power | Result (float) |
| 57 | `Lighting_CookTorrance` | Lighting | N, V, L, Albedo, Roughness, Metallic | Result (float3) |
| 58 | `Lighting_Lambert` | Lighting | Normal, LightDir | Result (float) |
| 59 | `UV_TilingOffset` | UV | UV, Tiling, Offset | UV (float2) |
| 60 | `UV_RotateUV` | UV | UV, Center, Angle | UV (float2) |
| 61 | `UV_PolarCoords` | UV | UV, Center, RadialScale, LengthScale | UV (float2) |
| 62 | `UV_RadialShear` | UV | UV, Center, Strength | UV (float2) |
| 63 | `UV_Spherize` | UV | UV, Center, Strength | UV (float2) |
| 64 | `UV_Twirl` | UV | UV, Center, Strength, Offset | UV (float2) |
| 65 | `Noise_Perlin` | Noise | Position (float3) | Value (float) |
| 66 | `Noise_Simplex` | Noise | Position (float3) | Value (float) |
| 67 | `Noise_FBM` | Noise | Position, Octaves, Lacunarity, Gain | Value (float) |
| 68 | `Noise_Voronoi` | Noise | UV, CellDensity | Cells (float), Distance (float) |
| 69 | `Noise_WhiteNoise` | Noise | UV (float2) | Value (float) |
| 70 | `Constant_Float` | Constants | -- | Value (float) |
| 71 | `Constant_Float2` | Constants | -- | Value (float2) |
| 72 | `Constant_Float3` | Constants | -- | Value (float3) |
| 73 | `Constant_Float4` | Constants | -- | Value (float4) |
| 74 | `Constant_Boolean` | Constants | -- | Value (bool) |
| 75 | `Constant_Integer` | Constants | -- | Value (int) |
| 76 | `Constant_PI` | Constants | -- | Value (float) |
| 77 | `Constant_Tau` | Constants | -- | Value (float) |
| 78 | `Utility_Split` | Utility | In (float4) | R, G, B, A (float) |
| 79 | `Utility_Combine` | Utility | R, G, B, A (float) | RGBA (float4) |
| 80 | `Utility_Swizzle` | Utility | In (float4), Mask (string) | Result |
| 81 | `Utility_Append` | Utility | A, B | Result |
| 82 | `Utility_Branch` | Utility | Condition (bool), True, False | Result |
| 83 | `Utility_Comparison` | Utility | A, B, Op (enum) | Result (bool) |
| 84 | `Utility_Reroute` | Utility | In | Out |
| 85 | `Utility_InverseLerp` | Utility | A, B, Value | Result (float) |
| 86 | `Channel_ChannelMask` | Channel | In (float4), Mask (enum) | Result |
| 87 | `Channel_Desaturate` | Channel | Color (float3), Amount (float) | Result (float3) |
| 88 | `Channel_NormalBlend` | Channel | A (float3), B (float3) | Result (float3) |
| 89 | `Channel_NormalStrength` | Channel | Normal (float3), Strength (float) | Result (float3) |
| 90 | `Channel_NormalUnpack` | Channel | Packed (float4) | Normal (float3) |
| 91 | `Color_Blend` | Color | Base (float3), Blend (float3), Alpha, Mode (enum) | Result (float3) |
| 92 | `Color_Contrast` | Color | Color (float3), Contrast (float) | Result (float3) |
| 93 | `Color_Saturation` | Color | Color (float3), Saturation (float) | Result (float3) |
| 94 | `Color_HueShift` | Color | Color (float3), Offset (float) | Result (float3) |
| 95 | `Color_Gradient` | Color | T (float), Colors (array) | Result (float3) |
| 96 | `Global_Time` | Global | -- | Time (float) |
| 97 | `Global_DeltaTime` | Global | -- | DeltaTime (float) |
| 98 | `Global_ScreenSize` | Global | -- | Size (float2) |
| 99 | `Global_WindDirection` | Global | -- | Dir (float3) |
| 100 | `Global_WindStrength` | Global | -- | Strength (float) |
| 101 | `Global_FogColor` | Global | -- | Color (float3) |
| 102 | `Global_FogDensity` | Global | -- | Density (float) |
| 103 | `Custom_HLSL` | Custom | (user-defined) | (user-defined) |
| 104 | `Custom_SubGraph` | Custom | (from sub-graph) | (from sub-graph) |
| 105 | `Custom_Comment` | Custom | -- | -- |
| 106 | `MasterOutput_PBR` | Output | Albedo, Alpha, Normal, Metallic, Roughness, Emissive, AO, Opacity, AlphaCutoff, WorldPositionOffset, CustomData0 | -- |
| 107 | `MasterOutput_Unlit` | Output | Color, Alpha, Opacity, WorldPositionOffset | -- |
| 108 | `MasterOutput_PostProcess` | Output | Color | -- |

---

## Appendix B: Codegen Examples

### B.1 Pulsing Emissive Effect

**Graph:**
```
[Time] --> [Multiply(*2)] --> [Sin] --> [Remap(-1..1 to 0..1)] --> [Lerp(ColorA, ColorB)] --> Master.Emissive
```

**Generated HLSL (PSMain body excerpt):**
```hlsl
    // === Node 200: Global_Time ===
    float node_200_time = g_Time;

    // === Node 201: Math_Multiply (Time * PulseSpeed) ===
    float node_201_result = node_200_time * 2.0;

    // === Node 203: Math_Sin ===
    float node_203_result = sin(node_201_result);

    // === Node 204: Utility_Remap (-1..1 to 0..1) ===
    float node_204_result = saturate((node_203_result - (-1.0)) / (1.0 - (-1.0)));

    // === Node 207: Utility_Lerp (ColorA, ColorB, t) ===
    float3 node_207_result = lerp(
        float3(1.0, 0.2, 0.0),   // Color A (exposed: pulse_color_a)
        float3(0.0, 0.5, 1.0),   // Color B (exposed: pulse_color_b)
        node_204_result
    );

    // === Master Output: Emissive ===
    float3 out_emissive = node_207_result;
```

### B.2 Triplanar Blended Terrain

**Graph:**
```
[WorldPos] --> [Triplanar(GrassTex)] --\
                                        [Lerp(mask)] --> Master.Albedo
[WorldPos] --> [Triplanar(RockTex)]  --/
[WorldNormal] --> [Dot(up)] --> [SmoothStep(0.5, 0.8)] --> Lerp.Alpha
```

**Generated HLSL (PSMain body excerpt):**
```hlsl
    // === Node 1: Input_WorldPosition ===
    float3 node_1_worldpos = i.world;

    // === Node 2: Input_WorldNormal ===
    float3 node_2_normal = normalize(i.normal);

    // === Node 5: Texture_Triplanar (Grass) ===
    // Triplanar sampling with sharpness=4.0
    float3 node_5_blend = pow(abs(node_2_normal), 4.0);
    node_5_blend /= (node_5_blend.x + node_5_blend.y + node_5_blend.z + EPSILON);
    float3 node_5_color =
        tex_grass.Sample(sampler_linear, node_1_worldpos.yz).rgb * node_5_blend.x +
        tex_grass.Sample(sampler_linear, node_1_worldpos.xz).rgb * node_5_blend.y +
        tex_grass.Sample(sampler_linear, node_1_worldpos.xy).rgb * node_5_blend.z;

    // === Node 6: Texture_Triplanar (Rock) ===
    float3 node_6_color =
        tex_rock.Sample(sampler_linear, node_1_worldpos.yz).rgb * node_5_blend.x +
        tex_rock.Sample(sampler_linear, node_1_worldpos.xz).rgb * node_5_blend.y +
        tex_rock.Sample(sampler_linear, node_1_worldpos.xy).rgb * node_5_blend.z;

    // === Node 10: Math_Dot (Normal . Up) ===
    float node_10_result = dot(node_2_normal, float3(0.0, 1.0, 0.0));

    // === Node 11: Math_SmoothStep (slope mask) ===
    float node_11_result = smoothstep(0.5, 0.8, node_10_result);

    // === Node 12: Utility_Lerp (Grass/Rock blend) ===
    float3 node_12_result = lerp(node_6_color, node_5_color, node_11_result);

    // === Master Output: Albedo ===
    float3 out_albedo = node_12_result;
```

### B.3 Animated Water Surface

**Graph:**
```
[UV] --> [TilingOffset(4,4)] --+
                               +--> [Add] --> [SampleNormalMap(WaterNormal)] --> Master.Normal
[Time] --> [Multiply(0.1)] ---+

[UV] --> [TilingOffset(2,2)] --> [Add(time*0.05)] --> [PerlinNoise] --> [Remap(0.3..0.7)] --> Master.Roughness

[FresnelEffect(Normal, ViewDir, 3.0)] --> [Lerp(DeepColor, ShallowColor)] --> Master.Albedo
```

**Generated HLSL (PSMain body excerpt):**
```hlsl
    // === Animated UV for normal map ===
    float2 node_4_uv = node_1_uv * float2(4.0, 4.0) + float2(0.0, 0.0);
    float  node_5_time_offset = g_Time * 0.1;
    float2 node_6_animated_uv = node_4_uv + float2(node_5_time_offset, node_5_time_offset);

    // === Node 7: SampleNormalMap (Water surface) ===
    float3 node_7_normal_ts = tex_water_normal.Sample(sampler_linear, node_6_animated_uv).xyz * 2.0 - 1.0;
    float3 T = normalize(i.tangent);
    float3 N = normalize(i.normal);
    float3 B = cross(N, T);
    float3x3 TBN = float3x3(T, B, N);
    float3 node_7_normal = normalize(mul(node_7_normal_ts, TBN));

    // === Roughness from animated Perlin noise ===
    float2 node_10_uv = node_1_uv * float2(2.0, 2.0);
    float2 node_11_uv = node_10_uv + float2(g_Time * 0.05, g_Time * 0.05);
    float  node_12_noise = perlin_noise(float3(node_11_uv, 0.0));
    float  node_13_roughness = saturate((node_12_noise - 0.3) / (0.7 - 0.3));

    // === Fresnel-based albedo ===
    float  node_14_fresnel = pow(1.0 - saturate(dot(node_7_normal, node_3_viewdir)), 3.0);
    float3 node_15_albedo = lerp(
        float3(0.0, 0.05, 0.15),  // Deep water color
        float3(0.1, 0.3, 0.4),    // Shallow water color
        node_14_fresnel
    );

    // === Master Output ===
    float3 out_albedo    = node_15_albedo;
    float3 out_normal    = node_7_normal;
    float  out_roughness = node_13_roughness;
    float  out_metallic  = 0.0;
```

---

## Appendix C: Comparison with Industry Tools

| Feature | UzEngine Visual Shader | Unreal Material Editor | Unity Shader Graph | Godot Visual Shader |
|---------|----------------------|----------------------|-------------------|-------------------|
| Backend | DX12 HLSL (DXC) | HLSL (UE shader compiler) | HLSL/GLSL (SRP) | GLSL (Godot renderer) |
| File Format | Plain-text INI (.uzshader) | Binary .uasset | Binary .shadergraph | Resource (.tres) |
| AI Integration | Native (MCP tools) | None | None | None |
| Hot-Reload | < 100ms via IPC | ~1-5s (recompile + cook) | ~1-3s | ~500ms |
| VCS Friendly | Yes (plain text, diff-friendly) | No (binary) | Partially (JSON) | Partially (text resource) |
| Custom HLSL Node | Yes | Custom Expression node | Custom Function node | Expression node |
| Sub-Graphs | Yes (.uzshader reference) | Material Functions | Sub Graphs | Not supported |
| Preview Isolation | Per-channel, split-screen | Per-channel | Per-channel | Per-output |
| Variant Support | ShaderVariantKey #ifdef | Material Switches | Shader Keywords | Limited |
| Editor | VSCode WebviewPanel | Unreal Editor tab | Unity Editor window | Godot editor tab |
| Max Textures | 6 user + 2 reserved | Varies by feature level | Varies | 16 |
| Export to Code | One-way HLSL export | Not supported | Not supported | Partial |
