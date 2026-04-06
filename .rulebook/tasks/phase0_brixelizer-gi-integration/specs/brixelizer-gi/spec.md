# Brixelizer GI Integration -- Spec Delta

## ADDED Requirements

### Requirement: FFX SDK Compilation
The build system MUST compile AMD FidelityFX SDK 1.1.4 source files (`ffx_dx12.cpp`, `ffx_brixelizer.cpp`, `ffx_brixelizer_raw.cpp`, `ffx_brixelizergi.cpp`) as part of the runtime static library. All 50 Brixelizer HLSL compute shaders MUST be pre-compiled or runtime-compiled via DXC.

#### Scenario: Clean build with FFX SDK
Given the FFX SDK 1.1.4 is present at `runtime/thirdparty/amd-fidelityfx-1.1/`
When `zig build` is executed
Then the build completes without errors
And the FFX SDK symbols are linked into the runtime binary

### Requirement: BrixelizerGIPass Lifecycle
The renderer MUST create and manage `FfxBrixelizerContext` and `FfxBrixelizerGIContext` via a `BrixelizerGIPass` class in `engine/renderer/gi/`. The pass MUST be gated by the `r.BrixelizerGI` CVar (default false) and MUST cleanly create and destroy all GPU resources.

#### Scenario: Context creation with reversed-Z
Given `r.BrixelizerGI=true` and a DX12 device is available
When `BrixelizerGIPass::Init()` is called
Then an `FfxBrixelizerContext` is created with 4 cascades (voxel sizes 2/4/8/16 cm)
And an `FfxBrixelizerGIContext` is created with `FFX_BRIXELIZER_GI_FLAG_DEPTH_INVERTED`
And the display size matches the swapchain resolution

#### Scenario: Clean shutdown
Given an active BrixelizerGIPass with both contexts
When `BrixelizerGIPass::Shutdown()` is called
Then both FFX contexts are destroyed via `ffxBrixelizerContextDestroy` and `ffxBrixelizerGIContextDestroy`
And all scratch memory is freed
And no GPU resource leaks are reported by the DX12 debug layer

#### Scenario: Feature disabled
Given `r.BrixelizerGI=false` (default)
When the renderer initializes
Then no Brixelizer contexts are created
And no Brixelizer GPU resources are allocated
And the deferred lighting shader receives sentinel SRV indices (0xFFFFFFFF)

### Requirement: Geometry Registration
The BrixelizerGIPass MUST register all visible mesh instances from the DrawList as Brixelizer instances each frame. Each instance MUST provide vertex buffer SRV, index buffer SRV, world transform, and axis-aligned bounding box.

#### Scenario: Sponza mesh registration
Given the Sponza scene with 138 mesh nodes loaded
When `BrixelizerGIPass::Execute()` runs the first frame
Then all 138 mesh instances are registered via `ffxBrixelizerCreateInstances`
And each instance has correct vertex stride, index format, and world transform

#### Scenario: Instance removal on scene change
Given 138 registered mesh instances
When a scene reload removes 50 mesh instances
Then `ffxBrixelizerDeleteInstances` is called for the 50 removed instances
And the remaining 88 instances continue to contribute to the SDF

### Requirement: Per-Frame SDF Bake and GI Dispatch
The BrixelizerGIPass MUST execute the Brixelizer SDF update and GI compute dispatch each frame. The SDF bake MUST produce a valid sparse distance field from registered geometry. The GI dispatch MUST produce diffuse and specular indirect lighting textures.

#### Scenario: SDF bake produces valid distance field
Given registered mesh instances covering the Sponza interior
When `ffxBrixelizerBakeUpdate` and `ffxBrixelizerUpdate` execute
Then the SDF atlas contains non-zero distance values near geometry surfaces
And the cascade AABB trees are populated

#### Scenario: GI dispatch produces indirect lighting
Given a valid SDF from the bake step
When `ffxBrixelizerGIContextDispatch` executes with depth, normals, roughness, and environment map
Then the `outputDiffuseGI` texture contains non-zero RGB values in interior areas
And the `outputSpecularGI` texture contains non-zero values near reflective surfaces
And both outputs are in linear HDR color space (RGBA16F)

### Requirement: Deferred Lighting Composite
The deferred lighting shader MUST sample Brixelizer GI diffuse and specular outputs and add them to the existing IBL ambient term. The composite MUST be gated by valid SRV indices and scaled by a configurable intensity CVar.

#### Scenario: GI adds indirect light to interior
Given Brixelizer GI produces valid diffuse output for the Sponza interior
When the deferred lighting shader evaluates ambient lighting
Then pixels in shadowed interior areas receive additional diffuse irradiance from Brixelizer GI
And the GI contribution is multiplied by `r.BrixelizerGI.Intensity` (default 1.0)

#### Scenario: Specular GI blended with IBL
Given Brixelizer GI produces valid specular output
When the deferred lighting shader evaluates specular ambient
Then the specular GI is blended with IBL specular based on roughness
And highly rough surfaces receive mostly diffuse GI
And smooth surfaces receive mostly specular GI + IBL

#### Scenario: GI disabled gracefully
Given `r.BrixelizerGI=false` or Brixelizer SRV indices are sentinel values
When the deferred lighting shader runs
Then no Brixelizer GI textures are sampled
And the lighting output is identical to the existing IBL-only path

### Requirement: GI Input Resources
The BrixelizerGIPass MUST provide all required input resources to the GI dispatch: depth buffer, world-space normals, roughness, motion vectors, environment map, previous frame lit output, and a blue noise texture for stochastic sampling.

#### Scenario: History buffer management
Given frame N has been rendered
When frame N+1 dispatches Brixelizer GI
Then the previous frame's depth, normals, and lit output are provided as history inputs
And the temporal accumulation produces stable, noise-free GI over multiple frames

### Requirement: Debug Visualization
The BrixelizerGIPass MUST support debug visualization via `--vis brixelizer.diffuse` and `--vis brixelizer.specular` CLI flags, and a `r.BrixelizerGI.Debug` CVar for radiance/irradiance cache visualization.

#### Scenario: Diffuse GI visualization
Given `r.BrixelizerGI=true` and the engine is running
When `--vis brixelizer.diffuse` is used
Then the diffuse GI output texture is displayed fullscreen
And lit areas show warm indirect bounce and shadowed areas show darker values
