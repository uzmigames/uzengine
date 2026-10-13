# H-71 readback and visible position input

## Implementation

`DDGISystem::ReadDiagnosticResults` previously checked a literal four-frame delay
and then called `IRHIDevice::WaitIdle`. It now uses a named
`kReadbackFrameLag = 4` and maps the snapshot without draining any queue.
This mirrors `RadianceCache::ReadDiagnosticResults` and `HandleReachDump`.
The application and embedded runtime both reuse three frame allocators and wait
their corresponding frame fences before recording. At F + 4 the copy recorded
in F is complete. This change does not establish the cause of the intermittent
H-71 hang; it removes the remaining diagnostic queue drain.

The G-buffer position input was examined through GBuffer registration, graph
sorting, graph barriers, and the DX12 state mapping. The graph already orders
the GBuffer write before the cache's read, and ShaderResource includes both
pixel and non-pixel shader access. No missing compute barrier was found.
The cache pass now declares that read only for classification 3 with visible
seeds enabled and geometry present. The callback obtains the SRV from the
graph-resolved position texture, rather than the separately cached descriptor.
Otherwise it supplies the sentinel descriptor and zero dimensions, preventing
the seed dispatch from reading an unwritten or stale position target when
there is no GBuffer geometry.

Source locations on the shared worktree at the start of this task:
`runtime/engine/renderer/gi/ddgi_system.cpp:472-473` had the literal delay and
WaitIdle; after the change, the named lag is at `:146` and the guard/map path at
`:476-479`. `runtime/engine/renderer/frame/frame_gi_passes.cpp:285` declared an
unconditional read and `:317` supplied the cached SRV; after the change,
`:283-289` gates the read and `:321-324` resolves its SRV and supplies the sentinel
when inactive. The regression begins at
`runtime/tests/engine/renderer/test_render_graph_states.cpp:818`.

## Regression

`DDGIDiagnosticReadback.MapsOnlyAfterFourFramesWithoutWaitingIdle` exercises the
real DDGI dispatch, snapshot copy and CPU read using the existing RHI test seam
in `test_render_graph_states.cpp`. It verifies no copy before frame 120, one
complete-sized copy at 120, no map in frames 121-123, exactly one map at 124,
no repeat map/copy at 125, and no WaitIdle calls. Its synthetic diagnostic
records are finite zeros; it tests scheduling, not GPU transport or irradiance.

Build, test and exercised GPU-run evidence is recorded in the final results
document in this directory. The normal GPU path is used; the workstation's
unavailable DX12 debug layer is not a validation prerequisite.
