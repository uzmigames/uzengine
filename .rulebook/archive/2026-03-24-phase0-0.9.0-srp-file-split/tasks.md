## 1. Phase 1 — Header Splits (unblock compilation improvements)

- [x] 1.1 Split `engine/rhi/rhi.h` (1012 lines) into `rhi_fence.h`, `rhi_command_list.h`, `rhi_swapchain.h`, `rhi_device.h` + umbrella `rhi.h`. Umbrella includes rhi_device.h which transitively includes all.
- [x] 1.2 Split `engine/renderer/scene/scene_renderer.h` (1359→612 lines) into `scene_renderer.h` (public API), `scene_renderer_frame.h` (standalone FrameContext struct), `scene_renderer_impl.h` (private members fragment).
- [x] 1.3 Split `engine/renderer/core/pipeline/pipeline_library.h` (842→662 lines) into `pipeline_library.h` (public API), `pipeline_library_impl.h` (187 lines, private Ensure*/shader/pipeline handles fragment).
- [x] 1.4 Build all backends (zig) after header splits — zero errors. All 3 headers compile clean.

## 2. Phase 2 — RHI Backend Splits

- [x] 2.1 Split `engine/rhi/dx11/dx11_device.cpp` (1389→457 lines) + new `dx11_resource_creation.cpp` (956 lines). Format helpers moved with resources.
- [x] 2.2 Split `engine/rhi/metal/metal_device.cpp` (1265→447 lines) + new `metal_resource_creation.cpp` (850 lines). build.zig updated for explicit metal file list.
- [x] 2.3 Extract format/state conversion helpers from `engine/rhi/vulkan/vulkan_command_list.cpp` (1261→1222 lines) — removed duplicate ToVkFormat + IsDepthFormat, now uses shared vulkan_format_helpers.h.
- [x] 2.4 build.zig updated for metal. DX11/Vulkan use addCppDir (auto-discovery) — no changes needed.
- [x] 2.5 Build all backends after RHI splits — zero errors.

## 3. Phase 3 — Renderer Core Splits (largest files)

- [x] 3.1 Split `scene_lifecycle.cpp` (1789 lines, deleted) → 4 files: `scene_renderer_init.cpp` (987), `scene_temporal_resources.cpp` (233), `scene_postprocess_resources.cpp` (458), `scene_upscaler.cpp` (148). scene_render_targets.cpp skipped — GBuffer/LUT creation is inline in Init().
- [x] 3.2 Split `pipeline_postprocess.cpp` (1560→741 lines) + 3 new: `pipeline_temporal.cpp` (179), `pipeline_depth.cpp` (129), `pipeline_gi.cpp` (548). All 29 PSO families preserved.
- [x] 3.3 Split `frame_lighting.cpp` (1246→250 lines orchestrator) + 4 new: `frame_ao_passes.cpp` (459), `frame_gi_passes.cpp` (394), `frame_lighting_pass.cpp` (124), `frame_atmosphere_passes.cpp` (233). Added 10 private helpers to scene_renderer_impl.h.
- [x] 3.4 Split `frame_setup.cpp` (1011→301 lines) + 2 new: `frame_sync.cpp` (255), `frame_constants.cpp` (536). Added PopulatePerFrameConstants/SyncDrawListAndInstances to scene_renderer_impl.h.
- [x] 3.5 Build system uses addCppDir (auto-discovery) — no build.zig changes needed.
- [x] 3.6 Build all backends after renderer splits — zero errors.

## 4. Phase 4 — Asset & Upload Splits

- [x] 4.1 Split `texture_loader.cpp` (1173→632 lines) + `texture_loader_hdr.cpp` (414) + `texture_loader_ring.cpp` (200).
- [x] 4.2 Split `mesh.cpp` (1122, deleted) → `mesh_io.cpp` (168), `mesh_builder.cpp` (706), `mesh_tangents.cpp` (158). ComputeBounds kept in builder (static helper for CreateFromVertices).
- [x] 4.3 Split `scene_assets.cpp` (886, deleted) → `scene_mesh_assets.cpp` + `scene_texture_assets.cpp`.
- [x] 4.4 Build system uses addCppDir — no build.zig changes needed.
- [x] 4.5 Build all backends after asset splits — zero errors.

## 5. Phase 5 — Application & Tools Splits

- [x] 5.1 Split `application.cpp` (857→527 lines) + `application_platform.cpp` (211) + `application_args.cpp` (166).
- [x] 5.2 Split `demo_application.cpp` (839→340 lines) + `demo_application_init.cpp` (251) + `demo_application_args.cpp` (290). build_common.zig updated. GetScreenshotsDir promoted to private static member.
- [x] 5.3 Split `shader_pipeline.cpp` (848→397 lines) + `shader_pipeline_batch.cpp` (374) + `shader_pipeline_internal.h` (137 shared helpers/PIMPL). Fixed UZ_DECLARE_STAT namespace placement.
- [x] 5.4 build.zig auto-discovers engine .cpp. build_common.zig updated for demo files.
- [x] 5.5 Build all backends after application splits — zero errors.

## 6. Phase 6 — Debug Splits

- [x] 6.1 Split `inspector_pass.cpp` (841→199 lines) + `inspector_pass_validate.cpp` (245) + `inspector_pass_dump.cpp` (291) + `inspector_pass_utils.h` (175 shared inline helpers).
- [x] 6.2 Build system uses addCppDir — no build.zig changes needed.
- [x] 6.3 Build all backends after debug splits — zero errors.

## 7. Testing

- [x] 7.1 Full test suite: 3988 tests passed, 0 failures, 0 regressions.
- [x] 7.2 Built and ran `demos/pbr_test` — clean build, 8 frames rendered, clean shutdown. No crashes or rendering errors.
- [x] 7.3 Zero files in `runtime/engine/` or `runtime/demos/common/` exceed 1500 lines.
- [x] 7.4 All new .cpp files discovered by addCppDir or added to explicit file lists (metal build.zig, demo build_common.zig).
- [x] 7.5 Umbrella `rhi.h` works — all existing includes compile without changes (verified via full build).

## 8. Documentation

- [x] 8.1 `docs/engine/module-structure.md` does not exist — skipped (file layout is self-documenting via directory structure).
- [x] 8.2 All new files created by agents include `// ───` section headers following existing code style.
