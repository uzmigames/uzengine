# 15. Finding to test inventory

Return to the [index](README.md). Maps each finding to the test files and
suites that cover it, as of 2026-10-17. Source inventory and landed revisions
are distinguished from this increment's uncommitted F-003 tests (see
[14](14-closed-findings-ledger.md)). Paths are relative to
`uzengine/`. "Negative control" means a case that must fail or reject when the
defect is present.

| Finding | Test file | Suites | Negative controls | Not covered |
|---|---|---|---|---|
| F-001 | `runtime/tests/engine/renderer/test_temporal_history_state.cpp` | TemporalHistoryState, TemporalUpscalerWrite (17) | `ExecuteFailed_SlotNotWritten_NextFrameIsFirst`, `TaaGap_ReadSlotStale_ContentInvalid_MatrixValid`, cut thresholds, `Fsr2StubExecute_ReturnsFalse` | GPU no-ghost sequence harness |
| F-002 | `runtime/tests/engine/renderer/test_velocity_pass.cpp`; `runtime/tests/engine/vfx/test_motion_vector_math.cpp`; `scripts/rendering/test_cbuffer_layouts.py` | PrevTransformCache, PreviousTransform, VelocityConvention, VelocityObjectCB, VelocityObjectPass (15, 8 real DX12); VfxMotionVector | `FirstSight_ReturnsCurrent`, `GapOfOneFrame_ReturnsCurrent_AndSweepEvicts`, `StaticTriangle_NotRedrawn`; layout before-fixtures | WPO deformation; DLSS/FSR2 SDK signs; full billboard image oracle |
| F-003 | `runtime/tests/engine/renderer/test_render_graph_states.cpp` (uncommitted, no SHA yet) plus the `a5ac6275` first-frame RG tests | RenderGraphResidual (9/9 passed): dependent UAV, read-only UAV, read/write declaration-order independence, RT/UAV/depth texture reuse, buffer decay, non-aliasing, production `AddLightingPass` LightCull Write -> DeferredLighting Read edge | omitted write-to-write/read barriers produce two ordering errors; omitted read-to-read/write barriers produce one for either declaration order; normal chains produce zero. First/reused transient before-states are checked against an independent truth list | Representative production clustered-light GBV is clean; the omission/transient cases are state-truth tests, not a claim of separate GBV captures for every synthetic case. Full outcomes in [16](16-audit-closure-2026-10-17.md) |
| F-004 | `runtime/tests/engine/material/test_material_compiler_extension.cpp`; `runtime/tests/engine/renderer/test_gbuffer.cpp`; `runtime/tests/engine/renderer/test_gpu_mesh.cpp` | MaterialCompilerExtension, MaterialTranslator; MaterialContextDraw; GPUMeshColorUpload | contextMask zero for texture-only graphs; `VertexColor_StreamDecoded_And_WhiteWithoutStream` and `ColorStream_UploadedOnlyWhenPresent` compare presence/absence. Real GPU draws cover `ScreenUV_TwoResolutions_Match`, time, camera and object perturbations | Broader scene image review beyond numeric GPU controls |
| F-005 | `runtime/tests/engine/renderer/test_upscaler_selection.cpp` | UpscalerSelection, DisplaySource, FSR2UpscalerGuard, DlssUpscalerGuard (20) | stub `Init_ReturnsFalse`/`Execute_ReturnsFalse`, `TaaInitFails_FallsToNone`, `ExecuteFailure_CurrentHdrAndThreeFrameFailover` | SDK builds; GPU no-frozen-frame harness |
| F-006, F-008 | `scripts/rendering/test_postprocess_chain.py` (historical inventory) | | existing chain negative controls | Documentation findings; no new test (no prose pinning) |
| F-007 | `runtime/tests/engine/renderer/test_glass_contract.cpp` | GlassContract, GlassDraw (9, 8 real DX12) | `ZeroTransmissionIsIdenticalToStandardTranslucentBranch`, `LegacyGlassDoesNotSampleTheSceneBehind` | Volumes, nested translucency, Sponza/Three.js parity |
| F-009 | `runtime/tests/engine/ecs/test_world.cpp`; `runtime/tests/engine/scripting/test_lua_scene_api.cpp` | WorldTest, WorldSubsystemTest; LuaSceneAPITest | stale handle rejected after destroy/reuse | None recorded |
| F-010 | `runtime/tests/engine/app/test_owner_command_queue.cpp`; `extension/src/tests/runtime-worker.test.ts` | OwnerCommandQueue (12); Vitest worker suite | `DrainOne_FromNonOwnerThread_RejectsAffinity` (debug only), Busy/Stopped errors | Live editor race / shutdown-UAF |
| F-011 | `runtime/tests/engine/ecs/test_scene_loader.cpp`; `runtime/tests/engine/scripting/test_lua_scene_io_api.cpp` | TransactionalSceneLoaderTest, SceneLoaderTest; LuaSceneIoAPITest | malformed/wrong-type input leaves live state unchanged | None recorded |
| F-012 | `runtime/tests/engine/ecs/test_component.cpp`; `runtime/tests/engine/scripting/test_lua_scene_extended.cpp` | SceneComponentTest; LuaSceneExtendedTest | self-parent, direct and deep cycle rejected | None |
| F-013 | `scripts/tools/test_build_manifest.py`, `scripts/tools/test_provision_native.py` | Python unittest | manifest mismatch rejection | None |
| F-014 | `extension/src/tests/*.test.ts`, `mcp/tests/**`, `scripts/tools/test_quality_scripts.py` | Vitest, Python unittest | empty-discovery failure | Optional real-search fixture remains separately reported when absent |
| F-015 | `scripts/falcor/test_falcor_reference_gate.py` | Python unittest (13) | missing, mismatched, below-35-dB evidence | Real renderer parity |
| F-016 | `scripts/tools/test_quality_scripts.py` | Python unittest | wrong root / paths with spaces | Real clang run |
| F-017 | no test; bootstrap logs in [08](08-native-bootstrap.md) | | | PATH Zig 0.16 contract undecided |
| F-018 | `runtime/tests/engine/renderer/test_instance_batcher.cpp` | InstanceBatcher, InstancingCharacterization, InstancingShaderCharacterization (10) | compiled DXC blobs lack instance ID/SRV; masked/opaque merge and 16,384 truncation recorded | Stage 2 GPU parity; stage 3 timing |
| F-019 | none (review only) | | | Concurrency benchmark |
| F-020 | `runtime/tests/engine/ipc/test_keyframe_gate.cpp`; `extension/src/tests/frame-gate.test.ts`, `nal-utils.test.ts`, `panels.test.ts`, `runtime-client.test.ts`, `runtime-worker.test.ts` | KeyframeGate, NalUtils (14); Vitest | deltas dropped until IDR; late replies after timeout ignored; pending cap 64; three-failure decoder stop | Real WebCodecs/webview, live IPC backpressure |
| F-021 | none | | | Needs NGX SDK + NVIDIA hardware |
| F-022 | `scripts/rendering/test_cbuffer_layouts.py` + `test_cbuffer_layouts_fixtures/*.before.hlsl`; `runtime/tests/engine/renderer/test_constant_buffers.cpp` | Tier A/B/C; PerFrameCB, PerObjectCB, InstanceData, CBRingAllocator | five before-fixtures must reject (`gbuffer_ps.perobject.before.hlsl` 4 mismatches, `gpu_cull_cs.before.hlsl` offset move) | Six `UNTRACKED_CONTRACTS`; module boundaries |

Cross-cutting gaps: no GPU integration harness for temporal sequences, no
SDK-enabled FSR2/DLSS/XeSS build, no WPO previous-position path, no matched
Falcor matrix (4.1), and no live editor/webview run.
