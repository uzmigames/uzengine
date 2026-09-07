# Sponza GI correction, 2026-09-06

## Diagnosis

The Sponza problem was not an exposure preset. The checked-in `mesh_sdf_bake_cs.hlsl` and `global_sdf_composite_cs.hlsl` each contained an empty compute entry point. Their C++ callers still dispatched, reported successful baking and exposed the output resources. Therefore a compiled pipeline and a successful startup log did not establish that the scene geometry existed in the GI representation.

Additional defects in the path compounded this:

- `Quat::FromEuler` reversed DirectXMath's multiplication convention. The authored Sun rotation `[-56,79,0]` produced a 9.1-degree solar elevation instead of 56 degrees. The renderer therefore illuminated the scene from the wrong direction despite reading the correct scene values.
- Sponza's imported triangle winding is opposite its vertex normals for 3,739,444 of 3,747,286 triangles. Signing from the raw geometric cross product classified free space as solid: the camera, 159 cm above the floor, received a negative floor distance. Min-compositing those fields blocked the probe rays throughout the interior.
- Correcting winding alone is insufficient: Sponza is split into open material partitions. An exact CPU audit still found 10–11 negative mesh fields at six known free points. The exterior stone partition responsible at Camera A has 42 boundary edges after welding positions. A nearest-face sign cannot define an interior for such a surface.
- The radiance cache wrote raw spherical harmonics into a buffer consumed as a cosine-convolved polynomial. A uniform radiance test returned approximately 3.55 instead of 1, with inverted directional response in two axes.
- Only the first 1024 of 4096 probes received first-write initialization. Later batches blended with unwritten storage. Moving the probe grid did not reset that history.
- Producer spacing, consumer interpolation and configured extent disagreed; grid coordinates were uploaded before recentering.
- World-to-local transform packing discarded translation.
- GDF refresh tracked camera movement but not newly loaded, removed or transformed meshes.
- Probe hit lighting used the negated ray as a normal, omitted a sun visibility test and omitted the Lambertian division by pi. Misses used the skylight tint as if it were actual sky radiance.
- A valid black GI value fell back to global sky light.
- An unconditional irradiance floor of 0.25 then masked low/zero GI even after the fallback was corrected. The floor now applies only to fallback sky illumination.
- The trace shader sampled a dynamic sampler descriptor that the runtime never created. It now uses the existing static linear-clamp sampler at s2.
- Sponza's graph loader discarded authored transparency and two-sided properties. Common draw metadata also omitted graph texture roles and constants, so the GI/shadow paths saw opaque glass and default white materials. The forward translucent shader ignored its emission inputs.
- Hit lighting had no feedback from previous probe irradiance, so light could not propagate through successive diffuse bounces.
- The sky cubemap divided already-integrated radiance by pi; the diffuse consumers already perform the Lambertian normalization. This attenuated sky transport a second time.
- Sponza fixed exposure at 2 with autoexposure disabled. Measured indirect lighting fell below the ACES Hill black threshold. Simply enabling autoexposure was insufficient because its default minimum metered luminance capped the exposure multiplier at 1.8.
- Diagnostic capture copied a whole 3D atmosphere volume into a one-slice readback footprint, causing an invalid DX12 command list.
- Upload buffers were destroyed under an incorrect assumption about triple-buffered frame fences. DRED reported a recently freed `TextureStagingBuffer`.

The current Sponza scene also did not enable the radiance cache or DDGI. The DXR DDGI path remains a separate incomplete implementation with fixed illumination inputs; it is not used to validate these software GI corrections.

## Changes

The mesh baker now evaluates triangle distances. Its padded cubic volume metadata agrees with its voxel centers. An explicit optional vertex-normal offset lets the application orient geometric face normals consistently with the imported surface normals; the baker does not guess a vertex layout. GDF composition initializes empty space, transforms and conservatively scales distances, updates on geometry changes, and batches metadata within the constant-buffer size limit.

The probe path now performs complete initial/reset writes, exposes resources only when valid, keeps its grid coordinates consistent, packs diffuse SH correctly, samples sky radiance and tests directional visibility at SDF hit points. Valid dark results stay dark.

GDF voxels now store distance and nearest-surface diffuse reflectance in RGBA16F. Reflectance comes from the material's coarsest texture mip, base tint and metallic factor. Material graphs export their authored texture roles, constants, blend mode and two-sided state to ordinary draw metadata; translucent geometry is excluded from solid GDF and shadow draws, and forward shading consumes authored emission.

Probe hit lighting includes previous-frame diffuse irradiance. Separate read/write SH buffers prevent races; partial updates preserve untouched probes. Normal-weighted interpolation rejects probes behind the surface and renormalizes the remaining weights. This enables temporal multiple-bounce transport without an artificial ambient floor.

The scene explicitly selects two-sided unsigned mesh distances using `r.GI.MeshSdf.TwoSided=1`; this setting remains disabled by default for consumers of closed signed volumes. The API carries the choice without inferring a mesh's topology from its name or filesystem path. Composition adds half a local voxel plus half a global voxel of surface coverage for two-sided fields so filtering does not erase a thin wall. This is a resolution-dependent approximation, not physical wall thickness; at the current 32-cubed mesh / 64-cubed global resolution it can thicken silhouettes and occlusion.

The 3D readback explicitly copies Z=0. Deferred upload-buffer destruction waits for GPU completion. This synchronization is intentionally limited to frames with pending buffer destruction; its performance cost remains observable during loading.

Euler construction now applies Roll, then Pitch, then Yaw as documented. This affects all combined Euler rotations, including other scenes and Lua callers; pure-axis rotations retain their behavior. The general quaternion multiplication operator is unchanged. The sky capture preserves integrated radiance, and the Sponza scene enables automatic exposure with a neutral manual multiplier and a minimum metered luminance of 0.001. This permits interior adaptation; GI intensity remains 1.

## Evidence

- `scripts/test_radiance_cache_sh.ps1`: extracts the actual production HLSL projection/packing functions into a native arithmetic test. Before: 0/90. After: 90/90. Tests constant, axis-linear and quadratic radiance against analytic diffuse convolution.
- `scripts/test_sdf_gpu.ps1`: runs the production mesh bake on a GTX 1660 SUPER. Empty original entry point fails with 32768 untouched sentinel voxels and 4096 wrong signs. Corrected shader passes: 32768 finite voxels, zero wrong signs, zero sentinels, range [-0.937500, 1.623047], maximum box-distance error 0.000960.
- Mesh bake, GDF composition and probe tracing compile with DXC shader model 6.6.
- Reversed-winding GPU regression uses the actual production C++ normal code and production HLSL: 32768 wrong signs without the normal offset, zero wrong signs with explicit outward vertex normals. Maximum distance error remains 0.000960.
- GDF GPU regression verifies translated, rotated and nonuniformly scaled geometry, plus an empty scene initialized to 65504. Probe GPU regression starts from NaNs and verifies finite SH and constant-sky irradiance with maximum relative error 2.4672% at 32 rays.
- Two-sided open-plane regression checks unsigned mesh values and ray hits from both sides after filtered global composition. Nearest-surface composition keeps the matching material RGB when a farther, differently colored surface is processed first.
- Enclosed-box feedback regression: zero-history negative control fails, first-write ignores NaN history and produces finite zero, and seeded diffuse history reflects the expected material RGB with maximum six-axis error 2.3452%.
- `scripts/sponza-lighting-baseline.log`: pass-dump failure at the atmosphere volume.
- `scripts/sponza-lighting-rc-before.log`: DRED report identifying freed staging storage.
- `docs/analysis/rendering/sponza-sdf-orientation-evidence.md`: actual mesh counts, exact distance queries and open-boundary topology evidence, including the failure remaining after orientation alone.
- `scripts/test_quaternion.ps1`: compiles actual quaternion/vector/matrix sources and two existing GoogleTest suites independently. Before: 35/38; after: 38/38. New regressions cover the Sponza sun direction, zenith with yaw, and combined-axis Euler round trips. See `docs/analysis/rendering/sponza-sun-rotation-evidence.md`.
- `sponza-lighting-exposure50.png` and `sponza-lighting-metering.png`: diagnostic controls establish that finite interior light reaches the display when exposure can adapt. These intermediate captures precede the solar-direction correction.

Integrated Sponza runs complete screenshots and pass dumps after the readback/lifetime fixes. The first integrated images remained nearly identical with GI off, on and intensity 10; these were rejected as success evidence. Disassembly of the loaded DXIL ruled out a stale consumer shader. Actual SH readback in `scripts/sponza-lighting-sign-before.log` confirmed all eight central probes were zero despite valid buffer data elsewhere. That early check verified CPU/GPU agreement, but the agreed solar direction was itself wrong because of Euler construction.

`scripts/sponza-lighting-solar-corrected.png/.log` shows the authored sunlight entering the atrium and indirect illumination in covered regions. The 642-frame diagnostic reports finite SH and no target failures (existing zero HiZ/TAA warnings remain). Live sun direction is (0.106699,0.548919,0.829038). Central above-floor probe DC rises from approximately 0.002 to 0.027; this is measured radiance-cache data, not an exposure multiplier.

`scripts/sponza-lighting-gi-contribution-on.png` and `sponza-lighting-gi-contribution-near-zero.png` isolate diffuse GI with the same executable, camera, manual exposure 8, and GI intensity 1 versus 0.000001. A positive near-zero value avoids the legacy zero-intensity fallback to sky. Mean absolute channel difference is 2.366/255, with 284518 changed pixels out of 518400. The modest image difference also exposes an important remaining limitation: the independent global specular cubemap can dominate shaded surfaces. These controls demonstrate contribution, not reference parity.

Waiting for those control processes exposed a shutdown assertion that detached launches had not recorded: `VfxEditorOverlay::Shutdown` omitted destruction of its 131072-byte FontAtlas. The owner now releases the font through its device; allocator assertions remain enabled.

### Final manual QA

- `scripts/sponza-lighting-verified.png/.log/.exit`: main Sponza project, Camera A, 1920x1080, 642 rendered frames, screenshot requested at frame 640. Process exit 0, empty stderr, no allocator leak or assertion. The sun patch enters the courtyard; covered stone surfaces retain indirect illumination and the darker floor remains distinct.
- `scripts/sponza-lighting-camera-c.png/.log/.exit`: Camera C, 960x540, same lighting settings, 642 rendered frames. Process exit 0, empty stderr, no diagnostic target failures. The view into the covered end of the atrium remains visible without direct sun on the inspected center pixel.
- The second view uses an isolated QA scene/project (`sample/content/scenes/sponza-gi-camera-c.qa.uzscene`, `scripts/sponza-lighting-camera-c.uzproject`) because this executable's CLI does not route the Application class's camera override flags. Only the active camera differs from the main scene.
- Both runs use the software GI path (`r.RayTracing=0`) on GTX 1660 SUPER. Existing HiZ/TAA zero-target warnings remain; the pass report is a sampled diagnostic and does not establish full image accuracy.
- `scripts/sponza-lighting-verified-sha256.txt` records the final executable and principal shader/math source hashes. Targeted diff whitespace checks and the added-runtime-line path scan pass.

The default runtime build builds the engine executable and library but exits 1 because the pre-existing `test_runtime` target fails with 23 compilation errors, including GoogleTest character-type incompatibilities and Metal/shader compiler test declarations. The two stale GI layout assertions were corrected for the new metadata layout. See `scripts/sponza-lighting-build-verified.log`. The standalone production arithmetic/GPU regressions execute independently of that broken target.

## Remaining algorithmic limits

This repairs the existing software GI pipeline; it is not an implementation of full Lumen. The cache uses a coarse per-mesh average reflectance and uniform probes. It lacks a UV-resolved surface radiance cache, probe depth-moment visibility and Lumen's screen-probe final gather. Arbitrary material-graph expressions are not evaluated at SDF hit points, and translucent emission does not become an indirect-light source. Normal weights alone cannot prevent all light leaks. Signed mesh fields still use nearest-face orientation, which is not robust for arbitrary open or inconsistently wound geometry; Sponza explicitly selects unsigned fields. Thin geometry remains limited by SDF resolution.

Specular environment lighting still comes from a global cubemap without local SDF visibility when GI is active. This can over-light covered surfaces; it is not a local reflection solution. The procedural cubemap also retains a scalar solar-intensity approximation, rather than the RGB input used by the visible atmosphere pass. These limits are not fixed by the diffuse transport corrections.

Falcor at the configured `E:/Falcor` is absent on this host. No PSNR>=35dB or Lumen parity claim is made.

## References

- Wright, 2015, [Dynamic Occlusion with Signed Distance Fields](https://www.realtimerendering.com/advances/s2015/DynamicOcclusionWithSignedDistanceFields.pdf).
- Majercik et al., 2019, [Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields](https://jcgt.org/published/0008/02/01/).
- Epic Games, 2022, [Lumen: Real-time Global Illumination in Unreal Engine 5](https://advances.realtimerendering.com/s2022/index.html).
- Microsoft, [ID3D12GraphicsCommandList::CopyTextureRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion).
- Kramer, 2023, [Real-Time Sparse Distance Fields for Games](https://gpuopen.com/download/GDC-2023-Sparse-Distance-Fields-For-Games.pdf), signed versus unsigned fields on slide 6.
- Epic Games, [Mesh Distance Fields](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-distance-fields-in-unreal-engine), two-sided generation and thin-surface resolution limits.
- Microsoft, [XMQuaternionMultiply](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmquaternionmultiply), argument order and returned Q2*Q1 product.
- Hillaire, 2020, [A Scalable and Production Ready Sky and Atmosphere Rendering Technique, reference implementation](https://github.com/sebh/UnrealEngineSkyAtmosphere/blob/master/Resources/RenderSkyRayMarching.hlsl), integrated luminance and ground Lambertian normalization.
