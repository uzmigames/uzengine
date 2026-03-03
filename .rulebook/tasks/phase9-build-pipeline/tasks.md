## 1. Cook Stage

- [ ] 1.1 `CookPipeline::cook_all(project_root)` — scan + cook all stale assets
- [ ] 1.2 Incremental: skip if `.import.json` hash matches source file sha256
- [ ] 1.3 Multi-threaded: N parallel cook jobs (configurable in project.uzproj)
- [ ] 1.4 Cook textures: stb_image decode + DirectXTex compress → .dds
- [ ] 1.5 Cook meshes: ufbx/fastgltf + meshoptimizer → .uzmesh / .uzskel / .uzanim
- [ ] 1.6 Cook audio: read .wav → encode to .ogg via libvorbis (or copy .ogg as-is)
- [ ] 1.7 Cook shaders: DXC compile all .hlsl to .dxil for `offline_compile=true` projects
- [ ] 1.8 `build.cook` IPC handler streams per-asset progress events to extension
- [ ] 1.9 `--force` flag bypasses hash check

## 2. Pack Stage

- [ ] 2.1 After cook: `PakWriter` assembles all `.import/` outputs + loose text assets
- [ ] 2.2 Include: .uzscene, .uzmat, .uzproj, .uzskel, .uzmask, .uzanim_sm, .uzinput, .uzcam, .uzphys
- [ ] 2.3 Include cooked: .uzmesh, .uzanim, .dds, .ogg, .dxil
- [ ] 2.4 Output: `build/<platform>/game.uzpak`
- [ ] 2.5 `build.pack` IPC handler

## 3. Shipping Binary

- [ ] 3.1 CMake target `UzEngine-Runtime` with `UZ_SHIPPING=1`
- [ ] 3.2 `#ifndef UZ_SHIPPING` guards around: IPC server, editor panels, DXC runtime, asset importers
- [ ] 3.3 Shipping binary mounts `game.uzpak` from executable directory
- [ ] 3.4 Shipping binary loads startup scene from `project.uzproj` `main_scene` field
- [ ] 3.5 Verify shipping binary starts without VSCode extension

## 4. Stage & Manifest

- [ ] 4.1 CMake install rules: copy exe, PAK, required DLLs to `staged/<platform>/`
- [ ] 4.2 Generate `manifest.json`: file list + sha256 hashes (for update diffing)
- [ ] 4.3 `build.openStaged(platform)` IPC — opens folder in Windows Explorer

## 5. Windows Packaging

- [ ] 5.1 CPack NSIS configuration in CMakeLists.txt
- [ ] 5.2 NSIS: install exe + pak + DLLs, create Desktop shortcut, uninstaller
- [ ] 5.3 Application icon: `.ico` from `assets/icon.png` via ImageMagick/IcoTool
- [ ] 5.4 Version stamped from `project.uzproj` version field
- [ ] 5.5 `build.package(platform)` IPC runs `cpack -C Release`

## 6. Build Panel (VSCode)

- [ ] 6.1 Build panel Webview: platform selector, config selector (Debug/Release/Shipping)
- [ ] 6.2 Buttons: [Cook] [Pack] [Build] [Build & Run]
- [ ] 6.3 Progress bar driven by `build.*` IPC event stream
- [ ] 6.4 Per-asset cook status: ✓ / ⏳ / ✗
- [ ] 6.5 Output size estimate

## 7. Platform Abstraction Layer (`src/platform/`)

- [ ] 7.1 `platform.h`: `create_window`, `poll_events`, `swap_buffers`, `get_time`, `load_library`
- [ ] 7.2 `win32.cpp`: Win32 implementation (refactor from existing main.cpp)
- [ ] 7.3 Stub `linux.cpp`, `macos.mm`, `android.cpp` with TODO markers
- [ ] 7.4 CMake: select platform source by `CMAKE_SYSTEM_NAME`

## 8. Testing

- [ ] 8.1 `build.cook` completes with no errors on sample project
- [ ] 8.2 `game.uzpak` contains all expected assets
- [ ] 8.3 `UzEngine-Runtime.exe` starts from staged directory (no VSCode)
- [ ] 8.4 All assets load from PAK, scene renders correctly
- [ ] 8.5 NSIS installer installs, runs, uninstalls cleanly
