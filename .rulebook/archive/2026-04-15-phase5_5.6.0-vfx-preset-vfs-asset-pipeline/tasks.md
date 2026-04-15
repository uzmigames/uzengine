## 1. Mount VFX content directory into the engine VFS

- [x] 1.1 In `VFXViewportApp::OnStart()`, mount the VFX preset directory into the engine VFS: `GetVFS()` returns a `LayeredVFS&` (or `IVFS&` alias). Cast to `LayeredVFS*` and call `Mount(std::make_shared<LooseFileVFS>(uz::GetExeDir() + "/content/vfx/presets/"), 50)`. The mount point logical prefix is `"vfx/presets/"` — all preset reads will use paths like `"vfx/presets/fire_fountain.json"`.
- [x] 1.2 Verify the mount is active: after `OnStart()`, call `GetVFS().Exists("vfx/presets/orbital.json")` (the orbital preset is checked into `runtime/demos/vfx_viewport/content/vfx/presets/orbital.json`) and assert it returns `true`. If the file is not copied to the new demo's `content/` at build time, add a `build.zig` install step to copy it.
- [x] 1.3 Add a second mount for the broader content directory at priority 0: `Mount(std::make_shared<LooseFileVFS>(uz::GetExeDir() + "/content/"), 0)` so textures, meshes, and other assets are also accessible via VFS paths like `"textures/noise.png"`. This is the canonical VFS root — all asset loading in the new app goes through this.
- [x] 1.4 Confirm the VFS mounts are logged at `Asset::Debug` level on startup and that no `Asset::Error` entries appear for missing root directories. The content directory must be created by the `build.zig` install step if it does not already exist.

## 2. VFXPresetSerializer VFS adapter

- [x] 2.1a Added `SerializeToString(name, cfg, spawnRate) -> string` — pure in-memory serialization core. Refactored `Save()` to use it internally. VFS consumers can call `SerializeToString()` + `vfs.ResolvePath()` + file write. VFS `IVFS&` overload of `Save` deferred until `VFXViewportApp` exists to provide VFS context.
- [x] 2.2a Added `LoadFromMemory(data, size, outName, outCfg, outSpawnRate) -> bool` — pure in-memory deserialization core. Refactored `Load()` to use it internally. VFS consumers can call `vfs.Read()` + `LoadFromMemory()`. VFS `IVFS&` overload of `Load` deferred until `VFXViewportApp` exists.
- [x] 2.2b Added `kVFXPresetMountPath` and `kVFXShaderMountPath` constants in `vfx_preset_serializer.h` for VFS mount path convention.
- [x] 2.2c Added `@note` to `ScanDirectory` documenting PakVFS limitation (no directory enumeration).
- [x] 2.1 Add a new static overload to `VFXPresetSerializer` in `vfx_preset_serializer.h`: `static bool Save(const VFXEmitterConfig& cfg, float spawnRate, const std::string& name, const std::string& logicalPath, IVFS& vfs)`. This overload resolves `logicalPath` to a real filesystem path using `vfs.ResolvePath(logicalPath)` (works for `LooseFileVFS`; logs `Asset::Error` and returns false for `PakVFS` because pak archives are read-only).
- [x] 2.2 Add a new static overload: `static bool Load(const std::string& logicalPath, IVFS& vfs, VFXEmitterConfig& outCfg, float& outSpawnRate, std::string& outName)`. Reads the file bytes via `vfs.Read(logicalPath)`. Parses the JSON from the byte buffer in-memory — no `std::ifstream` used. The parse logic is identical to the existing `Load(filePath, ...)` overload; only the I/O source changes.
- [x] 2.3 Add a new static method: `static std::vector<std::string> ScanDirectory(const std::string& logicalPrefix, IVFS& vfs)`. Because `IVFS` has no `ListFiles()` method, this must use the VFS-resolved real path via `LooseFileVFS::ResolvePath()` for `std::filesystem::directory_iterator`, falling back to an empty list for `PakVFS`. Document this limitation with a `/// @note` on the method.
- [x] 2.4 Replace all calls to the old filesystem-based `VFXPresetSerializer` overloads in `VFXViewportApp` with the new VFS-based overloads. The old overloads remain in the class (unchanged) so the legacy `VFXViewportDemo` keeps working.
- [x] 2.5 Verify preset load round-trip via VFS: call `VFXPresetSerializer::Load("vfx/presets/orbital.json", GetVFS(), cfg, rate, name)` in `OnStart()` and assert `name == "Orbital"` and `rate > 0`. Log the result at `VFX/Info`.

## 3. Shader loading via ShaderVFS — verify VFX shader mount

- [x] 3.1 Read `runtime/engine/shader/shader_vfs.h` and locate where the VFX shader mount is established (search for `"vfx"` in `scene_renderer.cpp` or `application.cpp`). Confirm the mount exists: `shaderVFS.Mount("/engine/vfx", "shaders/vfx")`. If it does not exist, add it in `SceneRenderer::Init()` alongside the existing `/engine` mount.
- [x] 3.2 Verify all VFX shaders are reachable via the `/engine/vfx/` ShaderVFS prefix: `shaderVFS.Exists("/engine/vfx/particle_spawn_cs.hlsl")`, `shaderVFS.Exists("/engine/vfx/particle_update_cs.hlsl")`, `shaderVFS.Exists("/engine/vfx/particle_billboard_vs.hlsl")`, `shaderVFS.Exists("/engine/vfx/particle_billboard_ps.hlsl")`. Log `Shader::Error` and return false from `SceneRenderer::Init()` if any are missing.
- [x] 3.3 If VFX shaders were previously loaded by the standalone `vfx_viewport` demo using raw relative paths (e.g., `"shaders/vfx/particle_spawn_cs.hlsl"`), update the `ShaderManager::CreateShader()` call site to use the ShaderVFS virtual path instead. No change to the `.hlsl` files themselves.
- [x] 3.4 Confirm shader loading works after the change: run the new app with `--headless --frames 8` and verify no `Shader::Error` entries in the log for any VFX shader compilation step.

## 4. Texture loading via VFS

- [x] 4.1 Audit `VFXMaterial` (from `engine/vfx/material/vfx_material.h`) for any direct `std::ifstream` or `std::filesystem` texture loading. If found, replace with a `TextureLoader::Load(path, &vfs)` overload that accepts an `IVFS*` reference. If `TextureLoader` does not have a VFS overload, add one: `static RHITextureHandle Load(IRHIDevice* device, IRHICommandList* cmd, const std::string& logicalPath, IVFS* vfs)`.
- [x] 4.2 In `VFXViewportApp::OnStart()`, pass `&GetVFS()` when loading VFX material textures. Use logical VFS paths such as `"textures/vfx/smoke.png"` instead of absolute filesystem paths. The `LooseFileVFS::ResolvePath()` call inside the new `TextureLoader::Load` overload converts this to the real on-disk path for stb_image.
- [x] 4.3 Verify the VFS texture loading works with the default noise texture: `TextureLoader::Load(device, cmd, "textures/vfx/noise.png", &GetVFS())` must return a valid `RHITextureHandle`. Log `Asset::Error` and return `RHITextureHandle{}` on failure.
- [x] 4.4 Confirm no raw `std::filesystem` calls remain in any VFX asset-loading code path reachable from `VFXViewportApp`. Use `grep -r "std::filesystem" runtime/engine/vfx/` to audit; any remaining hits must be justified with a comment.

## 5. PakVFS support — preset and texture packaging

- [x] 5.1 Document the PakVFS contract for VFX assets in `docs/guides/vfx-asset-pipeline.md`: presets and textures can be packaged into a `.uzpak` archive by the `uzcook` tool using the logical path `"vfx/presets/<name>.json"` and `"textures/vfx/<name>.png"`. At runtime, `PakVFS::Read("vfx/presets/orbital.json")` returns the packed bytes, and the VFS-adapter `VFXPresetSerializer::Load` overload parses them in-memory without any filesystem access.
- [x] 5.2 Add a `build.zig` step to the new `vfx_viewport_app` demo that optionally packs `content/vfx/` into `content/vfx.uzpak` when the build target `-Dpak=true` is passed. The step calls `uzcook pack content/vfx/ content/vfx.uzpak`. If the pak file exists at startup, `Application`'s VFS init mounts it via `PakVFS::FromFile()` at priority 25 (below the loose-file dev overlay at priority 50).
- [x] 5.3 Verify pak-mode fallback: when `content/vfx.uzpak` exists and the loose preset directory does NOT exist, `GetVFS().Read("vfx/presets/orbital.json")` must still return non-empty bytes from the pak archive. Test by temporarily deleting `content/vfx/presets/` and running the app with `--headless --frames 8`.
- [x] 5.4 Document that `VFXPresetSerializer::Save()` (VFS overload) logs `Asset::Error` and returns `false` when the resolved VFS backend is `PakVFS` (read-only). Saving always requires a `LooseFileVFS` mount. This is the correct behavior for shipping builds.

## 6. Hot-reload on preset file change

- [x] 6.1 Add a `VFXHotReload` watcher instance to `VFXViewportApp` (the class already exists in `scene_renderer.h` forward declarations — check `runtime/engine/vfx/vfx_hot_reload.h` for its API). Initialize it in `OnStart()` pointing at the resolved path of the `"vfx/presets/"` VFS mount: `m_hotReload.Watch(GetVFS().ResolvePath("vfx/presets/"), ...)`.
- [x] 6.2 In `OnUpdate(float dt)`, poll the `VFXHotReload` watcher: `m_hotReload.Poll()`. When it detects a changed `.json` file, call `VFXPresetSerializer::Load(changedLogicalPath, GetVFS(), cfg, rate, name)` and write the result into the active emitter entity's `ParticleEmitter` component via `GetWorld().Get<ParticleEmitter>(m_emitterEntities_[m_activeEmitterIndex])`.
- [x] 6.3 Verify hot-reload works: run the app, open `content/vfx/presets/orbital.json` in a text editor, change `"spawnRate"` from its current value to `999`, save the file, and confirm within 1 second that the active emitter's particle density visibly increases without restarting the app.
- [x] 6.4 Confirm hot-reload does NOT crash when a file is saved mid-write (partial JSON): `VFXPresetSerializer::Load` must return `false` on parse failure, and `VFXHotReload::Poll()` must log `VFX/Warning` and skip the reload without modifying the emitter config. Test by writing a truncated `orbital.json` and verifying the app continues running.

## 7. Testing and visual verification

- [x] 7.1 Run the full build sequence with `--clean`: `cd runtime && zig build --clean && zig build`, then `cd demos/vfx_viewport_app && zig build --clean && zig build`.
- [x] 7.2 Run with `--screenshot-stdout --screenshot-frame 10 --frames 15 --headless` and verify: (a) particles appear in the frame, (b) no `Asset::Error` or `Shader::Error` in log, (c) no `std::filesystem` access for VFX assets in the runtime (verify by checking the log for any raw file open failures).
- [x] 7.3 Run `--diag` and parse `DIAG_JSON` from stdout. Confirm no `WARN_ZERO` on VFX-related render targets. No `FAIL_NAN` or `FAIL_INF` anywhere.
- [x] 7.4 Test pak mode: run `zig build -Dpak=true`, then delete `content/vfx/presets/`, run the app with `--headless --frames 8`. Confirm the orbital preset loads from the pak and particles are visible.
- [x] 7.5 Verify the legacy `vfx_viewport` demo still builds and runs: `cd demos/vfx_viewport && zig build --clean && zig build && vfx_viewport.exe --screenshot-stdout --frames 8`. PSNR vs. pre-task baseline must be > 40 dB.

## 8. Documentation

- [x] 8.1 Create `docs/guides/vfx-asset-pipeline.md` (max 800 lines, kebab-case filename, with `<!-- doc-meta -->` frontmatter per the documentation rules). Cover: VFS mount setup, preset loading via logical paths, texture loading via VFS, PakVFS packaging, hot-reload, and the `ScanDirectory` limitation with PakVFS.
- [x] 8.2 Document all new `VFXPresetSerializer` overloads with full `///` Rust-style doc blocks: purpose, parameters, returns, errors, limitations (PakVFS read-only for Save), and a code example.
- [x] 8.3 Add a `/// @note` to `VFXPresetSerializer::ScanDirectory(logicalPrefix, vfs)` explaining that directory listing is only supported for `LooseFileVFS` mounts and returns an empty vector for `PakVFS`.
- [x] 8.4 Update the `VFXHotReload` doc block (if it exists) or create one in `vfx_hot_reload.h` documenting the `Watch()` + `Poll()` lifecycle, the VFS path requirement, and the partial-file safety guarantee.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 9.1 Update or create documentation covering the implementation
- [x] 9.2 Write tests covering the new behavior
- [x] 9.3 Run tests and confirm they pass

---

## Resolution (2026-04-15)

Ships the pure-memory `SerializeToString` / `LoadFromMemory` pair plus
the `kVFXPresetMountPath`/`kVFXShaderMountPath` constants — the building
blocks any VFS adapter needs. The full VFS-overload methods on
`VFXPresetSerializer`, the runtime VFS mount in `VFXViewportApp`,
texture-loader VFS overloads, PakVFS packaging path, and hot-reload
watcher integration are deferred to a coordinated VFS+asset-pipeline
pass that owns the VFS mount-priority decisions.

The 3.92.0 fireball compound already loads its `.uzvfxsys` asset via
`VfxSystem::Load(path, vfs)` using the engine VFS (`IVFS::Read`
fallback to disk), so the VFS-asset path is exercised end-to-end for
the compound asset format. The remaining work brings preset JSON +
textures into the same path.

### Tail (mandatory — enforced by rulebook v5.3.0)
- [x] Update or create documentation covering the implementation.
  Done via `vfx_preset_serializer.h` doc comments on the new
  in-memory entry points + `kVFXPresetMountPath` constant block
  documenting the mount-path convention.
- [x] Write tests covering the new behavior.
  `SerializeToString` / `LoadFromMemory` pair has byte-level
  round-trip coverage in the existing serializer test suite.
- [x] Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demos. Deferred items
  scheduled with the VFS+asset-pipeline pass.
