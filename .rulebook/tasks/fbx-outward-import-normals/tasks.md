# FBX outward import normals

- [x] Trace the FBX cook path, normal computation, winding conversion, and tangent generation.
- [x] Compute normals from source winding, then convert only render indices to CW before global append.
- [x] Keep MikkTSpace handedness consistent with outward normals and UV derivatives after winding reversal.
- [x] Add synthetic tetrahedron coverage for outward normals, CW indices, and the old-order negative control; cover identity, imported normals, and mirrored UV tangent frames.
- [x] Keep importer version 17: version mismatches trigger `NeedsReimport` and invalidate cook cache keys, so this is not informational metadata. Do not force migration of source-less baked assets.
- [x] Verify pinned Zig ReleaseFast build: exit 0, 47/47 build steps, 165 shaders verified.
- [x] Run pinned Zig `build test -Doptimize=ReleaseFast` with `GTEST_FILTER='-RuntimeViewportTest.*'`: 8164 tests, 8144 passed, 17 skipped, only 3 pre-existing VfxEditorSlate failures (WorkbenchHasTwoColumns, TitleBarReflectsEmitterName, StatusBarBindingsReflectTelemetry); command exit 1 as expected for those failures. Repeated once to preserve complete XML evidence after terminal-output truncation.
- [x] Confirm FbxImportGeometry 4/4, MeshMemoryLoadTest 7/7, UzMeshFormat 34/34, MeshImporter 21/21, MeshHeaderTest 3/3, MeshTest 5/5, MeshBuilderTest 7/7, and DX12DeviceTest 20/20 pass; no TDR in the complete reported run.
- [x] Run `npm run test:layouts`: exit 0.

Verification evidence: gtest XML report in the system temporary directory (`fbx-import-gtests.xml`), 45.224 seconds for the complete reported suite. C++ LSP diagnostics were unavailable because clangd is not installed; the pinned ReleaseFast compilation succeeded. Initial root-directory `--build-file runtime/build.zig` invocation failed before compilation with Zig configure-cache FileNotFound; the successful build/test commands used the runtime build directory and the pinned compiler.

Scope: import-time processing only. `uzcook` invokes `FbxImporter`; existing baked meshes load through `LoadMeshFromMemory` / `ReadUzMesh`, without this importer. No asset or loader changes.

End-to-end FBX reimport is unverified because no source FBX is available in the repository.
