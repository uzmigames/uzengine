# vfx-demo-launch-pipeline

## ADDED Requirements

### Requirement: Runtime DLLs deploy to every demo + test binary

The `zig build` step SHALL copy the runtime-dependency DLL set
(`d3dcompiler_47.dll`, `swscale-9.dll`, and every other DLL the main
`uzengine.exe` binary already receives) to the `bin/` directory of
every `exe.addInstallArtifact` target, including `vfx_viewport_app`,
`test_runtime`, and all other demos.

#### Scenario: Test runner launches without swscale-9.dll error

Given a freshly built `test_runtime.exe` on a machine without the
DLLs installed system-wide
When the user runs `test_runtime.exe --gtest_list_tests`
Then the process exits 0 and prints at least one registered test,
with no "error while loading shared libraries" log.

#### Scenario: Demo launches without d3dcompiler_47.dll error

Given a freshly built `vfx_viewport_app.exe`
When the user runs it with `--headless --frames 5`
Then no `ShaderCompilerFXC: failed to load 'd3dcompiler_47.dll'`
error appears in the runtime log.

### Requirement: RT test shader compiles with modern DXC

`runtime/shaders/rt_uv_test_lib.hlsl` SHALL be updated so its
`RayPayload` struct carries the `[raypayload]` attribute + per-field
access qualifiers required by DXC 6.6+.

#### Scenario: Ray-tracing pass initialises cleanly

Given a runtime built against DXC 6.6+
When `RTUVTestPass::Init` runs during demo startup
Then `ShaderManager::CompileShader('rt_uv_test_lib.hlsl')` returns
success and the pass reports ready.

### Requirement: ParticleMesh PSOs succeed

The `ParticleMeshOpaque` and `ParticleMeshTransparent` graphics PSOs
SHALL be created successfully during engine initialisation with no
`hr=0x80070057` error.

#### Scenario: PSO creation succeeds on startup

Given a clean runtime startup
When the renderer builds its pipeline library
Then neither `ParticleMeshOpaque` nor `ParticleMeshTransparent`
appears in the "permanently failed" PSO log.

### Requirement: No D3D12MA leak on shutdown

The demo SHALL shut down without tripping the `D3D12MA` allocator's
leak assertion (`m_pMetadata->IsEmpty()`).

#### Scenario: Clean shutdown

Given a demo instance launched in headless mode for 60 frames
When the process exits via `Quit()` or frame-budget expiry
Then the process exits with status 0 and no assertion message is
printed.

### Requirement: Headless screenshot of the Niagara editor

After the above fixes, `vfx_viewport_app.exe --screenshot-stdout
--screenshot-frame 5 --frames 8` SHALL output a valid PNG on stdout
(preceded by the `SCREENSHOT_BASE64:` sentinel) that shows the
Niagara-mode editor chrome (menu bar, emitter tab bar, stack panel,
center panel, system overview, timeline scrubber).

#### Scenario: Screenshot captures Niagara UI

Given a built demo + a pre-existing `-- F11`-on state
When the screenshot CLI runs headless for 8 frames
Then the emitted PNG decodes to a 1280x720 image whose top strip
contains the "File / Edit / View / Presets / Help" menu labels and
whose left column contains at least one stack-header label.
