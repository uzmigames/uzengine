const std = @import("std");

pub fn build(b: *std.Build) void {
    // Default output to build/ — user can override with -p <path>
    if (std.mem.endsWith(u8, b.install_prefix, "zig-out")) {
        b.resolveInstallPrefix("build", .{});
    }

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const is_windows = target.result.os.tag == .windows;
    const is_macos = target.result.os.tag == .macos;
    const is_ios = target.result.os.tag == .ios;
    const is_apple = is_macos or is_ios;

    const tp = "thirdparty/";

    // -Deditor=true  → also build editor.lib (grid, font, stats, screenshot)
    // Default: false — runtime.lib only (play mode / headless)
    const build_editor = b.option(bool, "editor", "Build editor.lib alongside runtime.lib") orelse false;

    const cpp_flags: []const []const u8 =
        &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB", "-DUZ_PROFILING_ENABLED", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND", "-DSPNG_STATIC" };

    // ── Include path tables (shared between runtime and tests) ───────────

    const engine_include_dirs = [_][]const u8{
        "engine",
        "engine/app",
        "engine/logging",
        "engine/assertions",
        "engine/profiling",
        "engine/memory",
        "engine/config",
        "engine/math",
        "engine/containers",
        "engine/random",
        "engine/rhi",
        "engine/rhi/optimization",
        "engine/shader",
        "engine/paths",
        "engine/core",
        "engine/renderer",
        "engine/renderer/camera",
        "engine/renderer/core",
        "engine/renderer/core/params",
        "engine/renderer/core/gbuffer",
        "engine/renderer/core/shader",
        "engine/renderer/core/pipeline",
        "engine/renderer/core/memory",
        "engine/renderer/core/lod",
        "engine/renderer/core/lighting",
        "engine/renderer/material",
        "engine/material",
        "engine/material/compiler",
        "engine/renderer/memory",
        "engine/renderer/mesh",
        "engine/renderer/upload",
        "engine/renderer/render_graph",
        "engine/renderer/shadows",
        "engine/renderer/ibl",
        "engine/renderer/gi",
        "engine/renderer/probes",
        "engine/renderer/lighting",
        "engine/renderer/atmosphere",
        "engine/renderer/fog",
        "engine/renderer/scene",
        "engine/renderer/frame",
        "engine/renderer/passes",
        "engine/renderer/passes/geometry",
        "engine/renderer/passes/lighting",
        "engine/renderer/passes/atmosphere",
        "engine/renderer/passes/postprocess",
        "engine/renderer/postprocess",
        "engine/renderer/passes/debug",
        "engine/renderer/passes/ibl",
        "engine/asset",
        "engine/asset/formats",
        "engine/asset/importers",
        "engine/job",
        "engine/ecs",
        "engine/ecs/core",
        "engine/ecs/scene",
        "engine/ecs/lighting",
        "engine/ecs/mesh",
        "engine/ecs/environment",
        "engine/ecs/metadata",
        "engine/ecs/update",
        "engine/ecs/loaders",
        "engine/scripting",
        "engine/scripting/scene",
        "engine/scripting/core",
        "engine/scripting/renderer",
        "engine/scripting/editor",
        "engine/ipc",
        "engine/vfx",
        "engine/vfx/core",
        "engine/vfx/components",
        "engine/vfx/curves",
        "engine/vfx/modules",
        "engine/vfx/asset",
        "engine/vfx/parameters",
        "engine/vfx/events",
        "engine/renderer/passes/vfx",
        "engine/vfx/decals",
        "engine/vfx/trails",
        "engine/vfx/lod",
        "engine/vfx/budget",
        "engine/animation",
        "engine/event",
        "engine/property",
        "engine/debug",
        "editor/grid",
        "editor/outline",
        "editor/gizmo",
        "editor/font",
        "editor/stats",
        "editor/screenshot",
        "editor/input",
        "editor/selection",
    };

    const thirdparty_include_dirs = [_][]const u8{
        tp ++ "spdlog/include",
        tp ++ "tracy/public",
        tp ++ "mimalloc/include",
        tp ++ "directxmath/Inc",
        tp ++ "glm",
        tp ++ "nlohmann",
        tp ++ "atomic_queue/include",
        tp ++ "spirv-reflect",
        tp ++ "spirv-reflect/include",
        tp ++ "xxhash",
        tp ++ "lz4/lib",
        tp ++ "stb",
        tp ++ "spng/spng",
        tp ++ "tinyexr",
        tp ++ "libjpeg-turbo",
        tp ++ "libjpeg-turbo/src",
        tp ++ "basis_universal/transcoder",
        tp ++ "astcenc/Source",
        tp ++ "vulkan_headers/include",
        tp ++ "volk",
        tp ++ "vma/include",
        tp ++ "fastgltf/include",
        tp ++ "fastgltf/deps/simdjson",
        tp ++ "meshoptimizer/src",
        tp ++ "entt/single_include",
        tp ++ "luajit/src",
        tp ++ "ufbx",
        tp ++ "etcpak",
        tp ++ "xatlas/source/xatlas",
        tp ++ "libpng",
        tp ++ "msdfgen",
        tp ++ "msdfgen/core",
        tp ++ "msdfgen/ext",
        tp ++ "freetype/include",
        tp ++ "smaa",
        tp ++ "ffmpeg/include",
    };

    const linux_engine_include_dirs = [_][]const u8{
        "platform/linux",
    };

    const windows_engine_include_dirs = [_][]const u8{
        "platform/windows",
    };

    const windows_thirdparty_include_dirs = [_][]const u8{
        tp ++ "d3d12ma/include",
        tp ++ "directx_headers/include",
        tp ++ "directx_headers/include/directx",
        tp ++ "agility/include",
        tp ++ "dxc/inc",
        // vcpkg-installed headers (zlib required by libpng, freetype, etc.)
        // zlib headers from source (not vcpkg DLL headers)
        tp ++ "vcpkg/buildtrees/zlib/src/v1.3.1-2e5db616bf.clean",
    };

    // ── uzembed build tool (generates .hlsl.gen.h headers) ─────────

    const embed_mod = b.createModule(.{ .target = target, .optimize = optimize });
    embed_mod.link_libc = true;
    embed_mod.link_libcpp = true;
    embed_mod.addCSourceFiles(.{
        .root = b.path("tools"),
        .files = &.{"uzembed.cpp"},
        .flags = cpp_flags,
    });

    const embed_exe = b.addExecutable(.{ .name = "uzembed", .root_module = embed_mod });
    b.installArtifact(embed_exe);

    // Clean old generated headers to prevent stale files from conflicting
    // with renamed/moved shaders (e.g., post/ → postprocess/ migration).
    const clean_generated = b.addRemoveDirTree(b.path("build/generated/shaders"));

    // Run uzembed: shaders/ → build/generated/shaders/ (all subdirs)
    const run_embed = b.addRunArtifact(embed_exe);
    run_embed.addArgs(&.{
        "--quiet",
        "--input-dir",  b.path("shaders").getPath(b),
        "--output-dir", b.path("build/generated/shaders").getPath(b),
    });
    run_embed.step.dependOn(&clean_generated.step);

    // ── Runtime static library ───────────────────────────────────────────

    const runtime_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    runtime_mod.link_libc = true;
    runtime_mod.link_libcpp = true;

    // Cross-platform engine sources — auto-discovered via glob
    addCppDir(runtime_mod, b, "engine/app", cpp_flags);
    addCppDir(runtime_mod, b, "engine/logging", cpp_flags);
    addCppDir(runtime_mod, b, "engine/assertions", cpp_flags);
    addCppDir(runtime_mod, b, "engine/profiling", cpp_flags);
    addCppDir(runtime_mod, b, "engine/config", cpp_flags);
    addCppDir(runtime_mod, b, "engine/math", cpp_flags);
    addCppDir(runtime_mod, b, "engine/rhi", cpp_flags);
    addCppDir(runtime_mod, b, "engine/rhi/optimization", cpp_flags);
    addCppDir(runtime_mod, b, "engine/paths", cpp_flags);
    addCppDir(runtime_mod, b, "engine/shader", cpp_flags);
    addCppDir(runtime_mod, b, "engine/core", cpp_flags);
    addCppDir(runtime_mod, b, "engine/asset", cpp_flags);
    addCppDir(runtime_mod, b, "engine/asset/formats", cpp_flags);
    addCppDir(runtime_mod, b, "engine/asset/importers", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/camera", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/core", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/core/params", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/core/gbuffer", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/core/shader", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/core/pipeline", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/material", cpp_flags);
    addCppDir(runtime_mod, b, "engine/material", cpp_flags);
    addCppDir(runtime_mod, b, "engine/material/compiler", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/memory", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/mesh", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/upload", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/geometry", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/lighting", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/atmosphere", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/postprocess", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/postprocess", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/debug", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/vfx", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/passes/ibl", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/shadows", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/lighting", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/atmosphere", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/fog", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/render_graph", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/ibl", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/gi", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/probes", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/scene", cpp_flags);
    addCppDir(runtime_mod, b, "engine/renderer/frame", cpp_flags);
    addCppDir(runtime_mod, b, "engine/job", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ecs/core", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ecs/scene", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ecs/lighting", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ecs/mesh", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ecs/update", cpp_flags);
    // NOTE: engine/ecs/environment/*.cpp are compiled into executables directly
    // (not the static lib) to preserve static initializers for component auto-registration.
    // See component_serializer.h UZ_REGISTER_COMPONENT_SERIALIZER macro.
    addCppDir(runtime_mod, b, "engine/ecs/loaders", cpp_flags);
    addCppDir(runtime_mod, b, "engine/scripting", cpp_flags);
    addCppDir(runtime_mod, b, "engine/scripting/scene", cpp_flags);
    addCppDir(runtime_mod, b, "engine/scripting/core", cpp_flags);
    addCppDir(runtime_mod, b, "engine/scripting/renderer", cpp_flags);
    addCppDir(runtime_mod, b, "engine/scripting/editor", cpp_flags);
    addCppDir(runtime_mod, b, "engine/ipc", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/core", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/components", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/curves", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/modules", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/asset", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/parameters", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/events", cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/decals",  cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/trails",  cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/lod",     cpp_flags);
    addCppDir(runtime_mod, b, "engine/vfx/budget",  cpp_flags);
    addCppDir(runtime_mod, b, "engine/animation", cpp_flags);
    addCppDir(runtime_mod, b, "engine/property", cpp_flags);
    addCppDir(runtime_mod, b, "engine/debug",    cpp_flags);

    // Thirdparty — SPIRV-Reflect (C source, not C++)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "spirv-reflect"),
        .files = &.{"spirv_reflect.c"},
        .flags = &.{"-std=c11"},
    });

    // Platform-specific sources
    if (is_windows) {
        addCppDir(runtime_mod, b, "platform/windows", cpp_flags);
        addCppDir(runtime_mod, b, "engine/rhi/dx12", cpp_flags);
        runtime_mod.addIncludePath(b.path("engine/rhi/dx12"));
        addCppDir(runtime_mod, b, "engine/rhi/dx11", cpp_flags);
        runtime_mod.addIncludePath(b.path("engine/rhi/dx11"));
        runtime_mod.addIncludePath(b.path(tp ++ "d3d12ma/src"));
        // LuaJIT pre-built library (built by setup.ps1 via msvcbuild.bat)
        runtime_mod.addObjectFile(b.path(tp ++ "luajit/src/lua51.lib"));
        // FFmpeg pre-built shared libraries (H.264 encoding for viewport streaming)
        runtime_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avcodec.lib"));
        runtime_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avutil.lib"));
        runtime_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/swscale.lib"));
        runtime_mod.addLibraryPath(b.path(tp ++ "ffmpeg/lib"));
    } else if (target.result.os.tag == .linux) {
        addCppDir(runtime_mod, b, "platform/linux", cpp_flags);
    }

    // Vulkan backend sources (cross-platform)
    addCppDir(runtime_mod, b, "engine/rhi/vulkan", cpp_flags);
    runtime_mod.addIncludePath(b.path("engine/rhi/vulkan"));

    // macOS/iOS platform + Metal backend sources
    if (is_apple) {
        // Platform layer (NSWindow + CAMetalLayer)
        addCppDir(runtime_mod, b, "platform/macos", cpp_flags);
        // Objective-C++ source (.mm) for PlatformWindow (NSWindow + Cocoa event loop)
        runtime_mod.addCSourceFiles(.{
            .root = b.path("platform/macos"),
            .files = &.{"platform_window.mm"},
            .flags = &.{ "-std=c++17", "-ObjC++", "-fno-objc-arc", "-DSPDLOG_COMPILED_LIB", "-DUZ_PROFILING_ENABLED", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" },
        });

        // Metal RHI backend — all files compiled as Objective-C++ because they
        // include <Metal/Metal.h> which uses @class/@interface ObjC syntax.
        const objcxx_flags = &.{ "-std=c++17", "-ObjC++", "-fno-objc-arc", "-DSPDLOG_COMPILED_LIB", "-DUZ_PROFILING_ENABLED", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" };
        runtime_mod.addCSourceFiles(.{
            .root = b.path("engine/rhi/metal"),
            .files = &.{ "metal_device.cpp", "metal_resource_creation.cpp", "metal_command_list.cpp", "metal_fence.cpp", "metal_gpu_profiler.cpp", "metal_swapchain.mm" },
            .flags = objcxx_flags,
        });

        // Tracy Metal wrapper — requires ARC (compiled separately from the rest of the backend)
        const objcxx_arc_flags = &.{ "-std=c++17", "-ObjC++", "-fobjc-arc", "-DSPDLOG_COMPILED_LIB", "-DUZ_PROFILING_ENABLED", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" };
        runtime_mod.addCSourceFiles(.{
            .root = b.path("engine/rhi/metal"),
            .files = &.{"metal_tracy_gpu.mm"},
            .flags = objcxx_arc_flags,
        });

        runtime_mod.addIncludePath(b.path("engine/rhi/metal"));
    }

    // Thirdparty — volk (C source, compiled with VK_NO_PROTOTYPES)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "volk"),
        .files = &.{"volk.c"},
        .flags = &.{ "-std=c11", "-DVK_NO_PROTOTYPES" },
    });

    // Thirdparty — fastgltf (C++17 glTF 2.0 parser + bundled simdjson)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "fastgltf/deps/simdjson"),
        .files = &.{"simdjson.cpp"},
        .flags = &.{"-std=c++17"},
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "fastgltf/src"),
        .files = &.{ "fastgltf.cpp", "base64.cpp", "io.cpp" },
        .flags = &.{"-std=c++17"},
    });

    // Thirdparty — spng (SIMD-accelerated PNG decode)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "spng/spng"),
        .files = &.{"spng.c"},
        .flags = &.{ "-std=c99", "-DSPNG_USE_MINIZ=0", "-DSPNG_STATIC" },
    });

    // Thirdparty — libjpeg-turbo (JPEG codec, pure C fallback — no NASM SIMD)
    // 8-bit core sources (default BITS_IN_JSAMPLE=8).
    // jstdhuff.c is #included by jcparam.c and jdhuff.c — not standalone.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "libjpeg-turbo/src"),
        .files = &.{
            "jaricom.c",
            "jcomapi.c",
            "jdapimin.c",
            "jdapistd.c",
            "jdarith.c",
            "jdatasrc.c",
            "jdcoefct.c",
            "jdcolor.c",
            "jddctmgr.c",
            "jddiffct.c",
            "jdhuff.c",
            "jdicc.c",
            "jdinput.c",
            "jdlhuff.c",
            "jdlossls.c",
            "jdmainct.c",
            "jdmarker.c",
            "jdmaster.c",
            "jdmerge.c",
            "jdphuff.c",
            "jdpostct.c",
            "jdsample.c",
            "jdtrans.c",
            "jerror.c",
            "jidctflt.c",
            "jidctfst.c",
            "jidctint.c",
            "jidctred.c",
            "jmemmgr.c",
            "jmemnobs.c",
            "jpeg_nbits.c",
            "jquant1.c",
            "jquant2.c",
            "jutils.c",
            // Encoder core (required — turbojpeg API uses both encode + decode)
            "jcapimin.c",
            "jcapistd.c",
            "jcarith.c",
            "jccoefct.c",
            "jccolor.c",
            "jcdctmgr.c",
            "jcdiffct.c",
            "jchuff.c",
            "jcicc.c",
            "jcinit.c",
            "jclhuff.c",
            "jclossls.c",
            "jcmainct.c",
            "jcmarker.c",
            "jcmaster.c",
            "jcparam.c",
            "jcphuff.c",
            "jcprepct.c",
            "jcsample.c",
            "jctrans.c",
            "jdatadst.c",
            "jfdctflt.c",
            "jfdctfst.c",
            "jfdctint.c",
            "transupp.c",
        },
        .flags = &.{ "-std=c99", "-fno-stack-protector", "-fno-builtin" },
    });

    // libjpeg-turbo 12-bit and 16-bit multi-precision wrappers.
    // Each wrapper #defines BITS_IN_JSAMPLE and #includes the base .c file.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "libjpeg-turbo/src/wrapper"),
        .files = &.{
            // 12-bit decode
            "jdapistd-12.c",
            "jdcoefct-12.c",
            "jdcolor-12.c",
            "jddctmgr-12.c",
            "jddiffct-12.c",
            "jdlossls-12.c",
            "jdmainct-12.c",
            "jdmerge-12.c",
            "jdpostct-12.c",
            "jdsample-12.c",
            "jidctflt-12.c",
            "jidctfst-12.c",
            "jidctint-12.c",
            "jidctred-12.c",
            "jquant1-12.c",
            "jquant2-12.c",
            "jutils-12.c",
            // 12-bit encode
            "jcapistd-12.c",
            "jccoefct-12.c",
            "jccolor-12.c",
            "jcdctmgr-12.c",
            "jcdiffct-12.c",
            "jclossls-12.c",
            "jcmainct-12.c",
            "jcprepct-12.c",
            "jcsample-12.c",
            "jfdctfst-12.c",
            "jfdctint-12.c",
            // 16-bit (lossless only)
            "jdapistd-16.c",
            "jdcolor-16.c",
            "jddiffct-16.c",
            "jdlossls-16.c",
            "jdmainct-16.c",
            "jdpostct-16.c",
            "jdsample-16.c",
            "jutils-16.c",
            "jcapistd-16.c",
            "jccolor-16.c",
            "jcdiffct-16.c",
            "jclossls-16.c",
            "jcmainct-16.c",
            "jcprepct-16.c",
            "jcsample-16.c",
        },
        .flags = &.{ "-std=c99", "-fno-stack-protector", "-fno-builtin" },
    });

    // Thirdparty — meshoptimizer (mesh optimization library)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "meshoptimizer/src"),
        .files = &.{
            "allocator.cpp",
            "clusterizer.cpp",
            "indexcodec.cpp",
            "indexgenerator.cpp",
            "overdrawanalyzer.cpp",
            "overdrawoptimizer.cpp",
            "quantization.cpp",
            "simplifier.cpp",
            "spatialorder.cpp",
            "stripifier.cpp",
            "vcacheanalyzer.cpp",
            "vcacheoptimizer.cpp",
            "vertexcodec.cpp",
            "vertexfilter.cpp",
            "vfetchanalyzer.cpp",
            "vfetchoptimizer.cpp",
        },
        .flags = &.{"-std=c++17"},
    });

    // Thirdparty — spdlog
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "spdlog/src"),
        .files = &.{
            "spdlog.cpp",
            "async.cpp",
            "cfg.cpp",
            "color_sinks.cpp",
            "file_sinks.cpp",
            "stdout_sinks.cpp",
            "bundled_fmtlib_format.cpp",
        },
        .flags = cpp_flags,
    });

    // Thirdparty — Tracy
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "tracy/public"),
        .files = &.{"TracyClient.cpp"},
        .flags = &.{ "-std=c++17", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" },
    });

    // Thirdparty — basis_universal transcoder
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "basis_universal/transcoder"),
        .files = &.{"basisu_transcoder.cpp"},
        .flags = &.{ "-std=c++17", "-DBASISD_SUPPORT_KTX2_ZSTD=0" },
    });

    // Thirdparty — miniz (zlib-compatible compression for tinyexr)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "tinyexr"),
        .files = &.{"miniz.c"},
        .flags = &.{"-std=c11"},
    });

    // Thirdparty — ufbx (FBX file parser, single C file)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "ufbx"),
        .files = &.{"ufbx.c"},
        .flags = &.{"-std=c11"},
    });

    // Thirdparty — lz4 (fast compression for .uzpak asset archives)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "lz4/lib"),
        .files = &.{"lz4.c"},
        .flags = &.{"-std=c11"},
    });

    // Thirdparty — astcenc (ASTC texture compression, library sources only)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "astcenc/Source"),
        .files = &.{
            "astcenc_averages_and_directions.cpp",
            "astcenc_block_sizes.cpp",
            "astcenc_color_quantize.cpp",
            "astcenc_color_unquantize.cpp",
            "astcenc_compress_symbolic.cpp",
            "astcenc_compute_variance.cpp",
            "astcenc_decompress_symbolic.cpp",
            "astcenc_diagnostic_trace.cpp",
            "astcenc_entry.cpp",
            "astcenc_find_best_partitioning.cpp",
            "astcenc_ideal_endpoints_and_weights.cpp",
            "astcenc_image.cpp",
            "astcenc_integer_sequence.cpp",
            "astcenc_mathlib.cpp",
            "astcenc_mathlib_softfloat.cpp",
            "astcenc_partition_tables.cpp",
            "astcenc_percentile_tables.cpp",
            "astcenc_pick_best_endpoint_format.cpp",
            "astcenc_quantization.cpp",
            "astcenc_symbolic_physical.cpp",
            "astcenc_weight_align.cpp",
            "astcenc_weight_quant_xfer_tables.cpp",
        },
        .flags = &.{"-std=c++17"},
    });

    // Thirdparty — etcpak (BC1/BC3/BC4/BC5/BC7 GPU texture compression)
    // bc7enc.cpp uses std::countr_zero from <bit> which requires C++20.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "etcpak"),
        .files = &.{
            "ProcessDxtc.cpp",
            "Tables.cpp",
            "Dither.cpp",
        },
        .flags = &.{"-std=c++17"},
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "etcpak"),
        .files = &.{
            "bc7enc.cpp",
        },
        .flags = &.{"-std=c++20"},
    });

    // Thirdparty — xatlas (UV unwrapping, single C++17 file)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "xatlas/source/xatlas"),
        .files = &.{"xatlas.cpp"},
        .flags = &.{"-std=c++17"},
    });

    // Thirdparty — zlib (compiled from vcpkg source — static build avoids DLL import issues)
    // Use source dir headers instead of vcpkg-installed DLL headers
    runtime_mod.addIncludePath(b.path(tp ++ "vcpkg/buildtrees/zlib/src/v1.3.1-2e5db616bf.clean"));
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "vcpkg/buildtrees/zlib/src/v1.3.1-2e5db616bf.clean"),
        .files = &.{
            "adler32.c", "compress.c", "crc32.c", "deflate.c",
            "gzclose.c", "gzlib.c", "gzread.c", "gzwrite.c",
            "infback.c", "inffast.c", "inflate.c", "inftrees.c",
            "trees.c", "uncompr.c", "zutil.c",
        },
        .flags = &.{},
    });

    // Thirdparty — libpng (PNG read/write, required by msdfgen ext/save-png.cpp)
    // PNG_ARM_NEON_OPT=0 disables the NEON intrinsics path on non-ARM platforms;
    // without it the build fails when <arm_neon.h> is unavailable.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "libpng"),
        .files = &.{
            "png.c",
            "pngerror.c",
            "pngget.c",
            "pngmem.c",
            "pngpread.c",
            "pngread.c",
            "pngrio.c",
            "pngrtran.c",
            "pngrutil.c",
            "pngset.c",
            "pngtrans.c",
            "pngwio.c",
            "pngwrite.c",
            "pngwtran.c",
            "pngwutil.c",
        },
        .flags = &.{ "-DPNG_ARM_NEON_OPT=0", "-DPNG_STATIC" },
    });

    // Thirdparty — FreeType (font rasterization, required by msdfgen ext/import-font.cpp)
    // Uses FreeType's amalgamated module build: each top-level .c file #includes all
    // files in its module. FT2_BUILD_LIBRARY must be defined when building as a library.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "freetype/src"),
        .files = &.{
            "base/ftsystem.c",
            "base/ftinit.c",
            "base/ftdebug.c",
            "base/ftbase.c",
            "base/ftbbox.c",
            "base/ftbitmap.c",
            "base/ftglyph.c",
            "base/ftmm.c",
            "truetype/truetype.c",
            "sfnt/sfnt.c",
            "smooth/smooth.c",
            "autofit/autofit.c",
            "psnames/psnames.c",
            "gzip/ftgzip.c",
        },
        .flags = &.{ "-DFT2_BUILD_LIBRARY", "-DFT_CONFIG_OPTION_SYSTEM_ZLIB=0" },
    });

    // Thirdparty — msdfgen core (multi-channel SDF generation, C++17)
    // All .cpp files in core/ are library sources (none contain main()).
    // MSDFGEN_PUBLIC= (empty) is defined to satisfy the include guard in base.h:
    //   #ifndef MSDFGEN_PUBLIC
    //   #include <msdfgen/msdfgen-config.h>   <- generated by CMake, not present here
    //   #endif
    // Defining it on the command line bypasses the CMake-generated header entirely.
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "msdfgen/core"),
        .files = &.{
            "Contour.cpp",
            "DistanceMapping.cpp",
            "EdgeHolder.cpp",
            "MSDFErrorCorrection.cpp",
            "Projection.cpp",
            "Scanline.cpp",
            "Shape.cpp",
            "contour-combiners.cpp",
            "convergent-curve-ordering.cpp",
            "edge-coloring.cpp",
            "edge-segments.cpp",
            "edge-selectors.cpp",
            "equation-solver.cpp",
            "export-svg.cpp",
            "msdf-error-correction.cpp",
            "msdfgen.cpp",
            "rasterization.cpp",
            "render-sdf.cpp",
            "save-bmp.cpp",
            "save-fl32.cpp",
            "save-rgba.cpp",
            "save-tiff.cpp",
            "sdf-error-estimation.cpp",
            "shape-description.cpp",
        },
        .flags = &.{ "-std=c++17", "-DMSDFGEN_PUBLIC=" },
    });

    // Thirdparty — msdfgen ext (font/SVG import + PNG export, C++17)
    // MSDFGEN_USE_FREETYPE enables the FreeType backend in import-font.cpp.
    // FreeType and libpng headers are available via thirdparty_include_dirs.
    // MSDFGEN_PUBLIC= bypasses the CMake-generated config header (same as core).
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "msdfgen/ext"),
        .files = &.{
            "import-font.cpp",
            "import-svg.cpp",
            "resolve-shape-geometry.cpp",
            "save-png.cpp",
        },
        .flags = &.{ "-std=c++17", "-DMSDFGEN_USE_FREETYPE", "-DMSDFGEN_PUBLIC=" },
    });

    // Thirdparty — AMD FidelityFX SDK headers (FSR 2.x temporal upscaling)
    // Include paths only — the FFX SDK requires MSVC + CMake to compile its source
    // (C++20 std::span, generated shader permutation blobs, Agility SDK ABI).
    // FSR2Upscaler compiles but links are resolved only when an ffx_fsr2.lib is provided.
    // Build the FFX SDK separately: cd thirdparty/amd-fidelityfx && cmake --build .
    if (is_windows) {
        const ffx = tp ++ "amd-fidelityfx/Kits/FidelityFX/";
        runtime_mod.addIncludePath(b.path(ffx ++ "api/include"));
        runtime_mod.addIncludePath(b.path(ffx ++ "api/internal"));
        runtime_mod.addIncludePath(b.path(ffx ++ "upscalers/fsr3/include"));
        runtime_mod.addIncludePath(b.path(ffx ++ "upscalers/fsr3/internal"));
        runtime_mod.addIncludePath(b.path(ffx ++ "backend/dx12"));
    }

    // Include paths — runtime root (for "platform/platform_types.h" etc.)
    runtime_mod.addIncludePath(b.path("."));

    // Include paths — cross-platform
    for (engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));

    // Generated shader headers (from uzembed tool)
    runtime_mod.addIncludePath(b.path("build/generated/shaders"));

    // Include paths — platform-specific
    if (is_windows) {
        for (windows_engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    } else if (target.result.os.tag == .linux) {
        for (linux_engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    }

    // Include paths — macOS/iOS
    if (is_apple) {
        runtime_mod.addIncludePath(b.path("platform/macos"));
    }

    const lib = b.addLibrary(.{
        .linkage = .static,
        .name = "runtime",
        .root_module = runtime_mod,
    });

    // Runtime compilation depends on uzembed having run
    lib.step.dependOn(&run_embed.step);

    // DX12/DX11 system libraries (Windows only)
    if (is_windows) {
        lib.linkSystemLibrary("d3d12");
        lib.linkSystemLibrary("d3d11");
        lib.linkSystemLibrary("dxgi");
        lib.linkSystemLibrary("dxguid");
        lib.linkSystemLibrary("ws2_32");  // Tracy socket profiling
        lib.linkSystemLibrary("dbghelp"); // Tracy callstack capture
        lib.linkSystemLibrary("winmm");   // timeBeginPeriod/timeEndPeriod for frame limiter
        lib.linkSystemLibrary("xinput1_4");  // XInput gamepad support
        lib.linkSystemLibrary("shcore");      // GetDpiForMonitor (multi-monitor DPI)
        lib.linkSystemLibrary("ole32");       // OLE drag-drop (OleInitialize, RegisterDragDrop)
        lib.linkSystemLibrary("shell32");     // Shell_NotifyIconW, SHCreateItemFromParsingName
        lib.linkSystemLibrary("comdlg32");    // Common dialog support (IFileDialog COM)
        // zlib — compiled from vcpkg source (static, no DLL import issues)
    } else if (target.result.os.tag == .linux) {
        lib.linkSystemLibrary("X11");
    }

    // Apple frameworks (macOS/iOS — Metal backend)
    if (is_apple) {
        lib.linkFramework("Metal");
        lib.linkFramework("QuartzCore");
        lib.linkFramework("Foundation");
        if (is_macos) {
            lib.linkFramework("AppKit");
        } else {
            lib.linkFramework("UIKit");
        }
    }

    b.installArtifact(lib);

    // (DLL target is built after editor_lib — see below)
    const build_dll = b.option(bool, "dll", "Also build runtime as shared library (DLL) for N-API addon") orelse false;

    // ── Editor static library (grid, font, stats, screenshot) ────────────
    // Built when -Deditor=true. Links against runtime.lib for includes only.
    // Output: build/lib/editor.lib
    const editor_lib = blk: {
        const editor_mod = b.createModule(.{ .target = target, .optimize = optimize });
        editor_mod.link_libc = true;
        editor_mod.link_libcpp = true;

        addCppDir(editor_mod, b, "editor/grid",       cpp_flags);
        addCppDir(editor_mod, b, "editor/outline",    cpp_flags);
        addCppDir(editor_mod, b, "editor/gizmo",      cpp_flags);
        addCppDir(editor_mod, b, "editor/font",       cpp_flags);
        addCppDir(editor_mod, b, "editor/stats",      cpp_flags);
        addCppDir(editor_mod, b, "editor/screenshot", cpp_flags);
        addCppDir(editor_mod, b, "editor/input",      cpp_flags);
        addCppDir(editor_mod, b, "editor/selection",  cpp_flags);

        // Include paths — same as runtime so editor code can include engine headers
        editor_mod.addIncludePath(b.path("."));
        for (engine_include_dirs) |dir| editor_mod.addIncludePath(b.path(dir));
        for (thirdparty_include_dirs) |dir| editor_mod.addIncludePath(b.path(dir));
        editor_mod.addIncludePath(b.path("build/generated/shaders"));
        editor_mod.addIncludePath(b.path("editor"));
        if (is_windows) {
            for (windows_engine_include_dirs) |dir| editor_mod.addIncludePath(b.path(dir));
            for (windows_thirdparty_include_dirs) |dir| editor_mod.addIncludePath(b.path(dir));
            editor_mod.addIncludePath(b.path("engine/rhi/dx12"));
            editor_mod.addIncludePath(b.path("engine/rhi/vulkan"));
        }

        const elib = b.addLibrary(.{
            .linkage = .static,
            .name = "editor",
            .root_module = editor_mod,
        });
        elib.step.dependOn(&run_embed.step);

        // Always install — demos link against editor.lib.
        // -Deditor=true is a signal for the VSCode extension to enable gizmos/tools,
        // not a gate on whether the lib is built.
        _ = build_editor; // consumed: future use for compile-time UZ_EDITOR define
        b.installArtifact(elib);
        break :blk elib;
    };

    // ── Shared library (DLL) for N-API addon ─────────────────────────────
    // Exports only the extern "C" uz_* API declared in engine/app/runtime.h.
    // The DLL carries its own libc++ (Zig/LLVM), so the N-API addon compiled
    // by MSVC can link against the MSVC-compatible import .lib produced here
    // without any COMDAT or C++ ABI incompatibility (LNK1143).
    //
    // Architecture:
    //   - runtime.cpp is recompiled with -DUZ_BUILDING_DLL=1 so that the
    //     UZ_API macro expands to __declspec(dllexport) on every uz_* function.
    //     These exported definitions live in the DLL's own .obj file.
    //   - runtime.lib is linked as a positional archive for all the C++
    //     internals (SceneRenderer, World, RHI, etc.) that runtime.cpp calls.
    //     Because runtime.cpp's own symbols take precedence over the archive
    //     copy, lld-link uses the dllexport version and the archive copy of
    //     runtime.cpp is never extracted.
    //   - editor.lib supplies screenshot_capture.cpp and other editor symbols
    //     referenced transitively from runtime.cpp.
    //
    // Build:   zig build -Ddll=true
    // Outputs: build/bin/uzengine_runtime.dll
    //          build/lib/uzengine_runtime.lib  (MSVC-compatible import lib)
    if (build_dll and is_windows) {
        const dll_mod = b.createModule(.{
            .target = target,
            .optimize = optimize,
        });
        dll_mod.link_libc = true;
        dll_mod.link_libcpp = true;

        // Recompile runtime.cpp with -DUZ_BUILDING_DLL=1.
        // UZ_API in runtime.h expands to __declspec(dllexport), making every
        // uz_* function definition in this .obj an exported DLL symbol.
        // All include paths that runtime.cpp needs are added below.
        const dll_cpp_flags = cpp_flags ++ &[_][]const u8{"-DUZ_BUILDING_DLL=1"};
        dll_mod.addCSourceFiles(.{
            .root = b.path("engine/app"),
            .files = &.{"runtime.cpp"},
            .flags = dll_cpp_flags,
        });

        // Include paths — same as runtime_mod so runtime.cpp can resolve all
        // headers it transitively includes.
        dll_mod.addIncludePath(b.path("."));
        for (engine_include_dirs) |dir| dll_mod.addIncludePath(b.path(dir));
        for (thirdparty_include_dirs) |dir| dll_mod.addIncludePath(b.path(dir));
        dll_mod.addIncludePath(b.path("build/generated/shaders"));
        for (windows_engine_include_dirs) |dir| dll_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| dll_mod.addIncludePath(b.path(dir));
        dll_mod.addIncludePath(b.path("engine/rhi/dx12"));
        dll_mod.addIncludePath(b.path("engine/rhi/dx11"));
        dll_mod.addIncludePath(b.path("engine/rhi/vulkan"));
        dll_mod.addIncludePath(b.path(tp ++ "d3d12ma/src"));

        // Component serializers compiled directly into DLL (not from static lib)
        // to preserve static initializers for auto-registration.
        // Without this, environment components (sky_atmosphere, sky_light,
        // height_fog, volumetric_clouds, etc.) are stripped by the linker.
        addCppDir(dll_mod, b, "engine/ecs/environment", dll_cpp_flags);

        // runtime.lib — C++ internals referenced by runtime.cpp.
        // lld-link uses the DLL's own runtime.cpp object for uz_* definitions
        // and extracts other members from this archive to satisfy remaining
        // unresolved references.
        dll_mod.addObjectFile(lib.getEmittedBin());
        // editor.lib — screenshot_capture.cpp and other editor symbols.
        dll_mod.addObjectFile(editor_lib.getEmittedBin());
        // LuaJIT static lib — Lua VM referenced by LuaVM.cpp.
        dll_mod.addObjectFile(b.path(tp ++ "luajit/src/lua51.lib"));
        // FFmpeg import libs for H.264 encoding.
        dll_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avcodec.lib"));
        dll_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avutil.lib"));
        dll_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/swscale.lib"));
        dll_mod.addLibraryPath(b.path(tp ++ "ffmpeg/lib"));

        const dll = b.addLibrary(.{
            .linkage = .dynamic,
            .name = "uzengine_runtime",
            .root_module = dll_mod,
        });

        // Depends on the shader-embed step (generated shader headers must exist
        // before runtime.cpp is compiled).
        dll.step.dependOn(&run_embed.step);

        // System libraries — same as the static lib Windows block above.
        dll.linkSystemLibrary("d3d12");
        dll.linkSystemLibrary("d3d11");
        dll.linkSystemLibrary("dxgi");
        dll.linkSystemLibrary("dxguid");
        dll.linkSystemLibrary("ws2_32");
        dll.linkSystemLibrary("dbghelp");
        dll.linkSystemLibrary("winmm");
        dll.linkSystemLibrary("xinput1_4");
        dll.linkSystemLibrary("shcore");
        dll.linkSystemLibrary("ole32");
        dll.linkSystemLibrary("shell32");
        dll.linkSystemLibrary("comdlg32");
        dll.linkSystemLibrary("gdi32");      // GetStockObject — used by font/overlay subsystem

        b.installArtifact(dll);
    }

    // ── Test executable (links runtime lib — no recompilation) ───────────

    const test_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    test_mod.link_libc = true;
    test_mod.link_libcpp = true;

    // GoogleTest — compiled from source (gtest-all.cc only, we provide main)
    const gtest_flags = &.{ "-std=c++17", "-DGTEST_HAS_PTHREAD=0" };
    test_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "googletest/googletest/src"),
        .files = &.{"gtest-all.cc"},
        .flags = gtest_flags,
    });
    test_mod.addIncludePath(b.path(tp ++ "googletest/googletest/include"));
    test_mod.addIncludePath(b.path(tp ++ "googletest/googletest"));

    // Test sources — auto-discovered via glob
    addCppDir(test_mod, b, "tests", cpp_flags);
    addCppDir(test_mod, b, "tests/engine", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/math", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/containers", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/random", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/paths", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/asset", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/core", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/shader", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/renderer", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/app", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/ipc", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/job", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/ecs", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/event", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/property", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/scripting", cpp_flags);
    addCppDir(test_mod, b, "tests/engine/vfx", cpp_flags);
    addCppDir(test_mod, b, "tests/editor", cpp_flags);
    // Thirdparty image lib tests — disable alignment sanitizer for stb_image_resize2
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/thirdparty"),
        .files = &.{"test_image_libs.cpp"},
        .flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB", "-fno-sanitize=alignment" },
    });

    // Platform-specific test sources
    if (is_windows) {
        addCppDir(test_mod, b, "tests/engine/rhi", cpp_flags);
        addCppDir(test_mod, b, "tests/platform", cpp_flags);
    } else if (is_apple) {
        // RHI type tests (cross-platform) + Metal backend tests
        test_mod.addCSourceFiles(.{
            .root = b.path("tests/engine/rhi"),
            .files = &.{
                "test_rhi_types.cpp",
                "test_rhi_config.cpp",
                "test_rhi_pso_cache.cpp",
                "test_metal_device.cpp",
                "test_metal_resources.cpp",
                "test_metal_render_loop.cpp",
                "test_metal_gpu_profiler.cpp",
            },
            .flags = cpp_flags,
        });
    } else {
        // RHI type tests are cross-platform (no DX12/Metal dependency)
        test_mod.addCSourceFiles(.{
            .root = b.path("tests/engine/rhi"),
            .files = &.{ "test_rhi_types.cpp", "test_rhi_config.cpp", "test_rhi_pso_cache.cpp" },
            .flags = cpp_flags,
        });
    }

    // Engine + thirdparty headers for tests
    test_mod.addIncludePath(b.path("."));
    for (engine_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    test_mod.addIncludePath(b.path("build/generated/shaders"));
    test_mod.addIncludePath(b.path("tests/engine/vfx")); // mock_rhi_device.h

    if (is_windows) {
        test_mod.addIncludePath(b.path("engine/rhi/dx12"));
        test_mod.addIncludePath(b.path("engine/rhi/vulkan"));
        for (windows_engine_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    } else if (target.result.os.tag == .linux) {
        test_mod.addIncludePath(b.path("engine/rhi/vulkan"));
        for (linux_engine_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    }

    if (is_apple) {
        test_mod.addIncludePath(b.path("engine/rhi/metal"));
    }

    // Component serializers compiled directly into exe for auto-registration
    addCppDir(test_mod, b, "engine/ecs/environment", cpp_flags);

    const test_exe = b.addExecutable(.{
        .name = "test_runtime",
        .root_module = test_mod,
    });
    test_exe.linkLibrary(lib);
    test_exe.linkLibrary(editor_lib);

    // LuaJIT — needed by scripting tests that call lua C API directly
    if (is_windows) {
        test_mod.addObjectFile(b.path(tp ++ "luajit/src/lua51.lib"));
        // FFmpeg import libs for H.264 encoder
        test_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avcodec.lib"));
        test_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avutil.lib"));
        test_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/swscale.lib"));
    }

    if (is_windows) {
        test_exe.linkSystemLibrary("d3d12");
        test_exe.linkSystemLibrary("d3d11");
        test_exe.linkSystemLibrary("dxgi");
        test_exe.linkSystemLibrary("dxguid");
        test_exe.linkSystemLibrary("ws2_32");
        test_exe.linkSystemLibrary("dbghelp");
        test_exe.linkSystemLibrary("winmm");
        test_exe.linkSystemLibrary("xinput1_4");
        test_exe.linkSystemLibrary("gdi32");
        test_exe.linkSystemLibrary("ole32");
        test_exe.linkSystemLibrary("shcore");
        test_exe.linkSystemLibrary("shell32");
        test_exe.linkSystemLibrary("comdlg32");
    } else if (target.result.os.tag == .linux) {
        test_exe.linkSystemLibrary("X11");
    }

    b.installArtifact(test_exe);

    // ── Run tests from the install directory (build/bin/) ─────────────────
    const run_test = b.addSystemCommand(&.{
        b.getInstallPath(.bin, "test_runtime.exe"),
    });
    run_test.step.dependOn(b.getInstallStep());
    const test_step = b.step("test", "Run runtime unit tests");
    test_step.dependOn(&run_test.step);

    // ── Demos & Tools (Windows/DX12 only) ────────────────────────────────

    if (is_windows) {
        const win_sys_libs = [_][]const u8{ "d3d12", "d3d11", "dxgi", "dxguid", "gdi32", "user32", "xinput1_4", "ole32", "shcore", "shell32", "comdlg32" };

        // Helper to create a tool exe from a single .cpp
        const tool_names = [_][]const u8{ "uzengine", "uzcook", "uzpack", "uzfbx" };
        for (tool_names) |tool_name| {
            const tool_mod = b.createModule(.{ .target = target, .optimize = optimize });
            tool_mod.link_libc = true;
            tool_mod.link_libcpp = true;
            tool_mod.addIncludePath(b.path("."));
            for (engine_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (thirdparty_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (windows_engine_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (windows_thirdparty_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            tool_mod.addIncludePath(b.path("engine/rhi/dx12"));
            tool_mod.addIncludePath(b.path("build/generated/shaders"));

            tool_mod.addCSourceFiles(.{
                .root = b.path("tools"),
                .files = &.{b.fmt("{s}.cpp", .{tool_name})},
                .flags = cpp_flags,
            });
            // Agility SDK exports — EXE only (enables SM6.6+ on Win10)
            tool_mod.addCSourceFiles(.{
                .root = b.path("engine/rhi/dx12"),
                .files = &.{"dx12_agility.cpp"},
                .flags = cpp_flags,
            });
            // Component serializers compiled directly into exe (not static lib)
            // to preserve static initializers for auto-registration.
            addCppDir(tool_mod, b, "engine/ecs/environment", cpp_flags);

            tool_mod.addObjectFile(b.path(tp ++ "luajit/src/lua51.lib"));
            // FFmpeg import libs for H.264 encoder
            tool_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avcodec.lib"));
            tool_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/avutil.lib"));
            tool_mod.addObjectFile(b.path(tp ++ "ffmpeg/lib/swscale.lib"));

            const tool_exe = b.addExecutable(.{ .name = tool_name, .root_module = tool_mod });
            tool_exe.linkLibrary(lib);
            tool_exe.linkLibrary(editor_lib);

            for (win_sys_libs) |l| tool_exe.linkSystemLibrary(l);
            b.installArtifact(tool_exe);
        }

        // Run steps
        const rt_exe = b.addExecutable(.{ .name = "uzengine_run", .root_module = b.createModule(.{ .target = target, .optimize = optimize }) });
        _ = rt_exe;
        const run_step = b.step("run", "Run UzEngine runtime");
        const run_rt_cmd = b.addSystemCommand(&.{b.getInstallPath(.bin, "uzengine.exe")});
        run_rt_cmd.step.dependOn(b.getInstallStep());
        run_step.dependOn(&run_rt_cmd.step);

        const cook_step = b.step("cook", "Run uzcook asset cook tool");
        const run_cook_cmd = b.addSystemCommand(&.{b.getInstallPath(.bin, "uzcook.exe")});
        run_cook_cmd.step.dependOn(b.getInstallStep());
        if (b.args) |a| for (a) |arg| run_cook_cmd.addArg(arg);
        cook_step.dependOn(&run_cook_cmd.step);

        const pack_step = b.step("pack", "Run uzpack asset packing tool");
        const run_pack_cmd = b.addSystemCommand(&.{b.getInstallPath(.bin, "uzpack.exe")});
        run_pack_cmd.step.dependOn(b.getInstallStep());
        if (b.args) |a| for (a) |arg| run_pack_cmd.addArg(arg);
        pack_step.dependOn(&run_pack_cmd.step);

        // ── DLL deployment ───────────────────────────────────────────────

        // Agility SDK (SM6.6+ on Win10 — EXE only, not deployed for DLL builds)
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/D3D12Core.dll"), "D3D12Core.dll",
        ).step);
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/d3d12SDKLayers.dll"), "d3d12SDKLayers.dll",
        ).step);

        // DXC
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "dxc/bin/x64/dxcompiler.dll"), "dxcompiler.dll",
        ).step);
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "dxc/bin/x64/dxil.dll"), "dxil.dll",
        ).step);

        // FXC (SM 5.0 fallback + future DX11)
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "fxc/bin/x64/d3dcompiler_47.dll"), "d3dcompiler_47.dll",
        ).step);

        // LuaJIT
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "luajit/src/lua51.dll"), "lua51.dll",
        ).step);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Scans a directory for all .cpp files (non-recursive) and adds them as
/// C++ source files to the given module. This replaces manual file lists —
/// just drop a .cpp in the directory and it gets compiled automatically.
///
/// Special handling:
/// - `dx12_d3d12ma.cpp` gets extra flags to suppress upstream warnings.
/// - Only .cpp files are collected (not .c or .h).
fn addCppDir(
    mod: *std.Build.Module,
    b: *std.Build,
    dir: []const u8,
    flags: []const []const u8,
) void {
    // D3D12MA needs special flags to suppress upstream warnings
    const d3d12ma_flags = &[_][]const u8{
        "-std=c++17",
        "-fno-sanitize=undefined",
        "-Wno-undefined-bool-conversion",
        "-Wno-switch",
        "-Wno-unused-private-field",
    };

    var cpp_files = std.ArrayListUnmanaged([]const u8){};

    var d3d12ma_found = false;

    const resolved = b.path(dir);
    var iter_dir = std.fs.openDirAbsolute(resolved.getPath(b), .{ .iterate = true }) catch return;
    defer iter_dir.close();

    var iter = iter_dir.iterate();
    while (iter.next() catch null) |entry| {
        if (entry.kind != .file) continue;
        const name = entry.name;
        if (!std.mem.endsWith(u8, name, ".cpp")) continue;

        // D3D12MA wrapper gets its own flags
        if (std.mem.eql(u8, name, "dx12_d3d12ma.cpp")) {
            d3d12ma_found = true;
            continue;
        }

        // Agility SDK exports — only linked into EXE builds, not lib/DLL.
        if (std.mem.eql(u8, name, "dx12_agility.cpp")) continue;

        cpp_files.append(b.allocator, b.allocator.dupe(u8, name) catch @panic("OOM")) catch @panic("OOM");
    }

    if (cpp_files.items.len > 0) {
        const files_slice = b.allocator.dupe([]const u8, cpp_files.items) catch @panic("OOM");
        mod.addCSourceFiles(.{
            .root = b.path(dir),
            .files = files_slice,
            .flags = flags,
        });
    }

    if (d3d12ma_found) {
        mod.addCSourceFiles(.{
            .root = b.path(dir),
            .files = &.{"dx12_d3d12ma.cpp"},
            .flags = d3d12ma_flags,
        });
    }
}
