# macOS/Metal build recipe and current platform limits

**Category**: build
**Tags**: none

## Description

macOS/Metal build bring-up (2026-08-31): use CMake+Ninja (zig 0.15.2 build runner can't link on macOS 27; zig 0.16 needs the build.zig patches now in-tree: @hasDecl for addRemoveDirTree, std.Io.Dir with b.graph.io, Module-level linkSystemLibrary/linkFramework with .{} options, system libc++ on Apple via linkLibCpp helper). Build: cmake -S runtime -B runtime/build/macos-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja -C runtime/build/macos-debug test_runtime. Debug is REQUIRED for tests (VFXHotReload etc. are #if UZ_DEV_BUILD || !NDEBUG). Vendored submodules were empty gitlinks — restored entt (b4e58bd), fastgltf (d4734a7, spnda/fastgltf) + simdjson 3.10.1 amalgamated, meshoptimizer (47aafa5, zeux). LuaJIT builds via make -C runtime/thirdparty/luajit/src MACOSX_DEPLOYMENT_TARGET=15.0. triangle_demo runs on Metal and screenshots via portable RHI readback. Full scene renderer on macOS blocked: ShaderPipeline/ShaderCompiler are _WIN32-only (now fail loudly); Metal needs an MSL/precompiled-archive shader path.
