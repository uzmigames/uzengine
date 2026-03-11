const std = @import("std");

pub fn build(b: *std.Build) void {
    // Default output to build/ — user can override with -p <path>
    if (std.mem.endsWith(u8, b.install_prefix, "zig-out")) {
        b.resolveInstallPrefix("build", .{});
    }

    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const is_windows = target.result.os.tag == .windows;

    const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };
    const tp = "thirdparty/";

    // ── Include path tables (shared between runtime and tests) ───────────

    const engine_include_dirs = [_][]const u8{
        "src",
        "core/logging",
        "core/assertions",
        "core/profiling",
        "core/memory",
        "core/config",
        "core/math",
        "core/containers",
        "core/random",
        "core/rhi",
        "core/shader",
        "core/paths",
        "core/renderer",
        "core/asset",
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
        tp ++ "stb",
        tp ++ "tinyexr",
        tp ++ "libjpeg-turbo",
        tp ++ "basis_universal/transcoder",
        tp ++ "astcenc/Source",
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

    // Run uzembed: shaders/editor/ → generated/shaders/editor/
    const run_embed = b.addRunArtifact(embed_exe);
    run_embed.addArgs(&.{
        "--input-dir",  b.path("shaders/editor").getPath(b),
        "--output-dir", b.path("generated/shaders/editor").getPath(b),
    });

    // ── Runtime static library ───────────────────────────────────────────

    const runtime_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    runtime_mod.link_libc = true;
    runtime_mod.link_libcpp = true;

    // Cross-platform engine sources — auto-discovered via glob
    addCppDir(runtime_mod, b, "src", cpp_flags);
    addCppDir(runtime_mod, b, "core/logging", cpp_flags);
    addCppDir(runtime_mod, b, "core/assertions", cpp_flags);
    addCppDir(runtime_mod, b, "core/profiling", cpp_flags);
    addCppDir(runtime_mod, b, "core/config", cpp_flags);
    addCppDir(runtime_mod, b, "core/math", cpp_flags);
    addCppDir(runtime_mod, b, "core/rhi", cpp_flags);
    addCppDir(runtime_mod, b, "core/paths", cpp_flags);
    addCppDir(runtime_mod, b, "core/shader", cpp_flags);
    addCppDir(runtime_mod, b, "core/asset", cpp_flags);
    addCppDir(runtime_mod, b, "core/renderer", cpp_flags);

    // Thirdparty — SPIRV-Reflect (C source, not C++)
    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "spirv-reflect"),
        .files = &.{"spirv_reflect.c"},
        .flags = &.{"-std=c11"},
    });

    // Windows-only sources
    if (is_windows) {
        addCppDir(runtime_mod, b, "platform/windows", cpp_flags);
        addCppDir(runtime_mod, b, "core/rhi/dx12", cpp_flags);
        runtime_mod.addIncludePath(b.path("core/rhi/dx12"));
        runtime_mod.addIncludePath(b.path(tp ++ "d3d12ma/src"));
    }

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

    // Include paths — cross-platform
    for (engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));

    // Generated shader headers (from uzembed tool)
    runtime_mod.addIncludePath(b.path("generated/shaders"));

    // Include paths — Windows-only
    if (is_windows) {
        for (windows_engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    }

    const lib = b.addLibrary(.{
        .linkage = .static,
        .name = "uzengine_runtime",
        .root_module = runtime_mod,
    });

    // Runtime compilation depends on uzembed having run
    lib.step.dependOn(&run_embed.step);

    // DX12 system libraries (Windows only)
    if (is_windows) {
        lib.linkSystemLibrary("d3d12");
        lib.linkSystemLibrary("dxgi");
        lib.linkSystemLibrary("dxguid");
    }

    b.installArtifact(lib);

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
    addCppDir(test_mod, b, "tests/core", cpp_flags);
    addCppDir(test_mod, b, "tests/core/math", cpp_flags);
    addCppDir(test_mod, b, "tests/core/containers", cpp_flags);
    addCppDir(test_mod, b, "tests/core/random", cpp_flags);
    addCppDir(test_mod, b, "tests/core/paths", cpp_flags);
    addCppDir(test_mod, b, "tests/core/asset", cpp_flags);
    addCppDir(test_mod, b, "tests/core/shader", cpp_flags);
    addCppDir(test_mod, b, "tests/core/renderer", cpp_flags);
    // Thirdparty image lib tests — disable alignment sanitizer for stb_image_resize2
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/thirdparty"),
        .files = &.{"test_image_libs.cpp"},
        .flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB", "-fno-sanitize=alignment" },
    });

    // Windows-only test sources
    if (is_windows) {
        addCppDir(test_mod, b, "tests/core/rhi", cpp_flags);
        addCppDir(test_mod, b, "tests/platform", cpp_flags);
    } else {
        // RHI type tests are cross-platform (no DX12 dependency)
        test_mod.addCSourceFiles(.{
            .root = b.path("tests/core/rhi"),
            .files = &.{ "test_rhi_types.cpp", "test_rhi_config.cpp" },
            .flags = cpp_flags,
        });
    }

    // Engine + thirdparty headers for tests
    for (engine_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    test_mod.addIncludePath(b.path("generated/shaders"));

    if (is_windows) {
        test_mod.addIncludePath(b.path("core/rhi/dx12"));
        for (windows_engine_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| test_mod.addIncludePath(b.path(dir));
    }

    const test_exe = b.addExecutable(.{
        .name = "test_runtime",
        .root_module = test_mod,
    });
    test_exe.linkLibrary(lib);

    if (is_windows) {
        test_exe.linkSystemLibrary("d3d12");
        test_exe.linkSystemLibrary("dxgi");
        test_exe.linkSystemLibrary("dxguid");
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
        const win_sys_libs = [_][]const u8{ "d3d12", "dxgi", "dxguid", "gdi32", "user32" };

        // Helper to create a tool exe from a single .cpp + agility
        const tool_names = [_][]const u8{ "uzengine", "uzcook" };
        for (tool_names) |tool_name| {
            const tool_mod = b.createModule(.{ .target = target, .optimize = optimize });
            tool_mod.link_libc = true;
            tool_mod.link_libcpp = true;
            for (engine_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (thirdparty_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (windows_engine_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            for (windows_thirdparty_include_dirs) |dir| tool_mod.addIncludePath(b.path(dir));
            tool_mod.addIncludePath(b.path("core/rhi/dx12"));

            tool_mod.addCSourceFiles(.{
                .root = b.path("tools"),
                .files = &.{b.fmt("{s}.cpp", .{tool_name})},
                .flags = cpp_flags,
            });
            tool_mod.addCSourceFiles(.{
                .root = b.path("core/rhi/dx12"),
                .files = &.{"dx12_agility.cpp"},
                .flags = cpp_flags,
            });

            const tool_exe = b.addExecutable(.{ .name = tool_name, .root_module = tool_mod });
            tool_exe.linkLibrary(lib);
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

        // ── DLL deployment ───────────────────────────────────────────────

        // Agility SDK
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/D3D12Core.dll"), "D3D12/D3D12Core.dll",
        ).step);
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/d3d12SDKLayers.dll"), "D3D12/d3d12SDKLayers.dll",
        ).step);

        // DXC
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "dxc/bin/x64/dxcompiler.dll"), "dxcompiler.dll",
        ).step);
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "dxc/bin/x64/dxil.dll"), "dxil.dll",
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
