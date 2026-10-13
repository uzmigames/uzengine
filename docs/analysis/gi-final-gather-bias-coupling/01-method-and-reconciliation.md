# Final gather: resumed measurements (2026-10-13)

## Scope and reconciliation

The parent owns the defect and correction decisions. This task measures the
current build without changing the shipping parity-locked rotation (H-36).
Fresh artifacts are rooted at
`.screenshots/gi-final-gather-20261013-resume/`; prior study directories are not
reused.

`git diff --stat` showed a shared worktree with 72 modified files. Inspection of
`frame_gi_passes.cpp` and `radiance_cache.*` found no stray diagnostic or
experimental production hunk to remove. The frame changes are the registry
migration, SSR exposure ratio fixed to 1, `IsGIOccluder`, and conditional,
graph-resolved visible seeds. The last change is explicitly documented in
`docs/analysis/rc-probe-support-followups/01-readback.md` as the H-71 work.
The radiance-cache changes are the state-registry migration. All were preserved.

The prior worker also left the untracked
`scripts/rendering/run_gather_coupling_confinement.py`. Its capture checks,
reference analysis, repeat floors and live/frozen-probe comparison were read
and retained as intended measurement work.

## Preregistered measurements

The existing bias study captures W/shadow_wall and A/right_shadow for frames
237-364, shipping rotation 0 and independent rotation 2, at 4, 8 and 32 rays.
Its P1/P2/P3 gates concern count dependence, not a defect verdict. In particular,
failure of P2's 2% independent count-flatness gate invalidates the independent
32-ray mean as evidence of an unbiased infinite-sample reference.

The reference extension captures independent rotation 2 at 32 rays for 512
frames starting at 237. Temporal convergence requires the 256- and 512-frame
cumulative means to differ by at most 2%, and the normal 95% interval from
32 temporal blocks of 16 frames to have relative half-width at most 2%.
This describes temporal uncertainty in two fixed ROIs. It does not establish
spatial uncertainty, independence of the blocks, systematic accuracy, or the
infinite-ray limit.

Confinement varies only the final-gather rotation (0/2), with live RC probes
and probes frozen after frame 229. Each condition has three 32-frame repeats
starting at 237. Raw reflection differences must be no greater than twice
the same-condition repeatability floor (minimum 1e-12). Composite specular,
consumed gather, snapped probe read, AO, direct light and geometry are recorded.
A composite change with invariant raw reflection confines the observed change
downstream of reflection-hit shading.

H-28 uses the existing preregistered revalidation script unchanged: three
repeats each of baseline, early freeze after 229 and midway freeze after 301,
128 captured frames from 237. A frozen gather must stay active as SDF mode 1,
stop changing at the predicted frame, and keep the non-target controls within
their measured repeat floors. An isolation breach invalidates H-28's
attribution even when TAA oscillation falls.

## Mechanism inspected

`runtime/shaders/runtime/gi/sdf_reflections.hlsli:196-209` shades reflection
hits using sun visibility and `SdfReflectionIrradiance`, which reads the RC
probe SH buffer. It does not read the final-gather texture.

There is a separate direct route in
`runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1161-1167`:
the consumed final gather replaces `cosineIrradiance`.
At `:1234-1235`, composite specular is
`radiance * singleScatter + cosineIrradiance * multiScatter`.
Changing diffuse gathered irradiance can therefore change the recorded
specular component without changing `ShadeSdfReflectionHit` or raw reflection
radiance. Whether that shared multi-scatter input is the desired contract is
reserved for the parent.

AO recorded by the lighting pass comes from G-buffer AO and GTAO
(`deferred_lighting_ps.hlsl:493-516`), not the gather's AO output. An AO breach
cannot be attributed to the multi-scatter equation.

## Resource ownership inspected

`SSGIRenderer::CreateTextures` creates its result, AO and histories directly
through the RHI, not the render-graph transient pool. `DispatchUpsample`
transitions its persistent result and AO textures from SRV to UAV before the
dispatch and back to SRV afterward. The frozen path skips registration only
after a completed gather frame, leaving the last result in SRV state.

`RgBuilder::AcquirePhysicalResources` excludes imported resources, and
`ReleasePhysicalResources` excludes them from return to the pool. Transient
resources are acquired together and released after execution. These inspected
paths do not expose the gather result to transient-pool reuse. This is a source
ownership finding, not GPU validation or proof against all descriptor,
constant-buffer, or backend allocation errors.

The DX12 backend uses `D3D12MA::Allocator::CreateResource` for each texture
(`runtime/engine/rhi/dx12/dx12_texture_creation.cpp:171-182`), retaining its
own allocation and resource. This path is not an explicit overlapping-resource
alias creation. Backend allocation correctness is not inferred from that alone.

## Test scope

No production behavior or CPU seam was changed in this measurement task.
Consequently no new gtest was added: the GPU studies cover the measured
behavior, and the existing owned runtime suite covers the current shared
build. The three known `VfxEditorSlate.*` failures are reported separately.
Normal GPU captures are used; DX12 GPU validation is unavailable on this
workstation and is not claimed as evidence.
