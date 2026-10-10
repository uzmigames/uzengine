# 05. Performance and maintainability

Return to the [index](README.md). Performance impact has not been profiled in
this audit; acceptance requires measurement rather than an assumed speedup.

## F-018 — Instance batches are built while instanced drawing is disabled

**Priority:** P2. **Confidence:** high for the disabled path; cost unmeasured.

**Evidence:** [frame_sync.cpp](../../../runtime/engine/renderer/frame/frame_sync.cpp),
lines 31–45, builds instance batches before forcing `ctx.useInstancing = false`.
Comments record invisible geometry and incomplete mesh batching in the instanced
path. These are existing code comments, not a reproduction performed here.

**Impact:** batching preparation can consume CPU time without enabling instanced
draws; the intended draw-call reduction is unavailable.

**Correction and acceptance:** measure preparation cost and draw counts. Avoid
unused preparation when safe, and diagnose the pipeline before enabling it.
Single-mesh and mixed-mesh/material scenes must preserve visible geometry,
transforms, material selection, shadows and culling. Only then compare CPU/GPU
timings and draw counts to the validated per-draw path.

## F-019 — Loose-file reads serialize on a shared VFS lock

**Priority:** P2. **Confidence:** high for lock scope; throughput impact unmeasured.

**Evidence:** [vfs.cpp](../../../runtime/engine/asset/vfs.cpp), around line 48,
holds the loose-file VFS mutex across the whole `FileReadAll` operation.

**Impact:** concurrent requests through that instance cannot overlap file reads.
This can limit asynchronous loading, but the operating system/cache/storage
behavior must be measured. The Pak path has different reader ownership and
must not be assumed to share the same problem.

**Correction and acceptance:** identify mutable state protected by the lock and
keep only necessary state access inside it. Benchmark serial and parallel reads
using representative asset sizes, warm/cold cache conditions and realistic
storage. Validate mount/lifetime safety and data integrity under concurrency;
publish results even if the bottleneck turns out to be elsewhere.

## F-020 — Editor streaming and request queues lack complete bounds

**Priority:** P2. **Confidence:** high for unbounded structures; memory growth and
latency under saturation require stress measurement.

**Evidence:** [render_thread.cpp](../../../napi/src/render_thread.cpp), around
line 70, creates a thread-safe function with queue size zero (unlimited).
[runtime-client.ts](../../../extension/src/runtime-client.ts), lines 495–515,
stores pending requests without a timeout.
[runtime-worker.ts](../../../extension/src/runtime-worker.ts), lines 304–336,
sends encoded frames without a complete producer-pressure policy.

**Impact:** a stalled editor or failed worker can accumulate frames and unresolved
requests, increasing memory or leaving operations pending indefinitely.

**Correction and acceptance:** establish bounded queues, observable pressure,
request deadlines and shutdown/crash cancellation. Test a slow consumer, worker
exit and viewport disposal. Memory/queue depth must remain bounded and callers
must receive completion or a specific failure. H.264 frames cannot be dropped
arbitrarily without handling dependencies and keyframe recovery; define that
policy rather than applying a generic latest-frame rule.

## F-021 — DLSS discovery embeds an absolute Windows filesystem path

**Priority:** P2. **Confidence:** high, explicit project-rule violation.

**Evidence:** [dlss_upscaler.cpp](../../../runtime/engine/renderer/passes/postprocess/dlss_upscaler.cpp),
around line 159, embeds `C:\Windows\System32\DriverStore\FileRepository`.
The project override requires path-agnostic runtime code.

**Impact:** discovery assumes a Windows installation location and drive. Runtime
deployment should use OS/API discovery or application-supplied paths, not this
machine-specific assumption.

**Correction and acceptance:** use the appropriate supported discovery API or
explicit application configuration, with clear unavailable-capability behavior.
Exercise an installation outside the assumed location and run the prescribed
runtime path scan before committing code. This fix must not introduce another
absolute path or project-specific content fallback.

## F-022 — Large modules, duplicated data layouts and stale capability claims weaken review

**Priority:** P3. **Confidence:** high for observed structure/document drift;
recommendation rather than a standalone reproduced defect.

**Evidence:** the review counted approximately 4,623 lines in
`vfx_editor_overlay.cpp`, 1,778 in
[scene_renderer_init.cpp](../../../runtime/engine/renderer/scene/scene_renderer_init.cpp)
and 1,548 in
[deferred_lighting_ps.hlsl](../../../runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl).
CPU/HLSL contracts are maintained in separate declarations. Existing
[ARCHITECTURE.md](../../../docs/ARCHITECTURE.md) describes an older layout;
the [March renderer audit](../rendering-arch/overview/uzengine-audit.md), lines
7–15, makes a production-ready claim that is not evidence for this source tree.
Current worker/streaming implementation must also be reflected in editor docs.

**Impact:** a localized change can cross initialization, lifetime, binding and
rendering responsibilities without a clear review boundary. Stale documentation
can make implemented, integrated and validated features appear interchangeable.
File length by itself does not prove poor runtime behavior.

**Correction and acceptance:** first stabilize interfaces, then split modules by
ownership and lifetime where a concrete review benefit exists. Add generated
layout data or reflection/offset checks for shared CPU/HLSL contracts. Maintain a
capability matrix that records implementation, active integration, validation,
backend, tested revision and known limits. Preserve dated historical reports;
do not silently reinterpret their claims as present-day acceptance. A refactor
must retain the established behavior and measurements, not become a rewrite of
unrelated subsystems.
