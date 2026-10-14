# Frames that do not depend on where resources are allocated

## Why

On 2026-10-09 the render-graph state harness (`scripts/rendering/test_rg_states_identity.py`) compared a build whose RT UV
test pass failed to initialise (a wrong payload qualifier, so its 1920x1080 UAV texture and two descriptors were
never allocated) with the build before it. The reflection, fog-shaft and path-tracer references moved by one
code value at 50, 997 and 1 pixels; the local-shadow, decal and HDRI references stayed bit-identical; with the
qualifier fixed every case was bit-identical again. No shader code changed (the recompiled shaders were
byte-identical to their cached compilations). So some pass's output depends on where later resources land in
memory or in the descriptor heap: an uninitialised read (a texture or buffer read before anything writes it,
whose contents then depend on what the allocator placed there before), or a path that depends on a
descriptor index.

Separately, after phase0_capture-preload-determinism made the pre-load length deterministic, processes of one
build and configuration still differ by up to one code value at a few pixels (Sponza pose W, frame 237), so
bit-identity across processes is not complete. The same mechanism is a candidate.

## What Changes

- Find the pass: shift the allocation layout on purpose (a diagnostic allocation of a known size before the
  scene's resources, or the RT UV test pass disabled) and diff the region dump's records per pass on the three
  references that moved, then locate the first record that differs.
- Fix the read (initialise the resource, or clear it before its first read), or remove the index dependence.

## Gate (to be registered before the fix is built)

With and without the diagnostic layout shift, the reflection, fog-shaft and path-tracer references are
bit-identical; the harness's other cases unchanged.

## Impact
- Affected specs: frame-layout-independence (new)
- Affected code: to be found (a pass reading uninitialised memory or a descriptor index)
- Breaking change: NO
- User benefit: frames that reproduce across builds whose allocations differ, and the last part of
  cross-process reproducibility.
