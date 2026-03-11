const std = @import("std");

pub fn build(b: *std.Build) void {
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
    };

    const thirdparty_include_dirs = [_][]const u8{
        tp ++ "spdlog/include",
        tp ++ "tracy/public",
        tp ++ "mimalloc/include",
        tp ++ "directxmath/Inc",
        tp ++ "glm",
        tp ++ "nlohmann",
        tp ++ "atomic_queue/include",
        tp ++ "spirv-reflect/include",
        tp ++ "xxhash",
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

    // ── Runtime static library ───────────────────────────────────────────

    const runtime_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    runtime_mod.link_libc = true;
    runtime_mod.link_libcpp = true;

    // Engine sources — cross-platform core
    runtime_mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{"uz_runtime.cpp"},
        .flags = cpp_flags,
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/logging"),
        .files = &.{"uz_log.cpp"},
        .flags = cpp_flags,
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/assertions"),
        .files = &.{"uz_assert.cpp"},
        .flags = cpp_flags,
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/profiling"),
        .files = &.{"uz_profiler.cpp"},
        .flags = cpp_flags,
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/config"),
        .files = &.{"uz_config.cpp"},
        .flags = cpp_flags,
    });
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/math"),
        .files = &.{"uz_math.cpp"},
        .flags = cpp_flags,
    });

    // RHI core (cross-platform — no backend dependency)
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/rhi"),
        .files = &.{"rhi_config.cpp"},
        .flags = cpp_flags,
    });

    // Windows-only sources
    if (is_windows) {
        runtime_mod.addCSourceFiles(.{
            .root = b.path("platform/windows"),
            .files = &.{ "uz_platform_window.cpp", "uz_input.cpp" },
            .flags = cpp_flags,
        });

        // RHI DX12 backend
        runtime_mod.addCSourceFiles(.{
            .root = b.path("core/rhi/dx12"),
            .files = &.{
                "dx12_device.cpp",
                "dx12_swapchain.cpp",
                "dx12_fence.cpp",
                "dx12_command_list.cpp",
                "dx12_descriptor_allocator.cpp",
                "dx12_resource_creation.cpp",
                "dx12_gpu_profiler.cpp",
                "dx12_gpu_breadcrumbs.cpp",
            },
            .flags = cpp_flags,
        });
        runtime_mod.addIncludePath(b.path("core/rhi/dx12"));

        // Thirdparty — D3D12MA (compiled via wrapper for Clang/MinGW GUID compatibility)
        runtime_mod.addCSourceFiles(.{
            .root = b.path("core/rhi/dx12"),
            .files = &.{"dx12_d3d12ma.cpp"},
            .flags = &.{ "-std=c++17", "-Wno-undefined-bool-conversion", "-Wno-switch", "-Wno-unused-private-field" },
        });
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

    // Include paths — cross-platform
    for (engine_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| runtime_mod.addIncludePath(b.path(dir));

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

    // Test sources — custom main + per-module test files
    test_mod.addCSourceFiles(.{
        .root = b.path("tests"),
        .files = &.{"test_main.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/core"),
        .files = &.{
            "test_runtime_api.cpp",
            "test_logging.cpp",
            "test_assertions.cpp",
            "test_profiler.cpp",
            "test_memory.cpp",
            "test_config.cpp",
            "test_allocators.cpp",
        },
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/core/math"),
        .files = &.{
            "test_vec.cpp",
            "test_mat4.cpp",
            "test_quat.cpp",
            "test_transform.cpp",
            "test_color.cpp",
            "test_bounds.cpp",
        },
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/core/containers"),
        .files = &.{
            "test_ring_buffer.cpp",
            "test_free_list.cpp",
            "test_handle_map.cpp",
            "test_frame_array.cpp",
        },
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("tests/core/random"),
        .files = &.{"test_random.cpp"},
        .flags = cpp_flags,
    });

    // Windows-only test sources
    if (is_windows) {
        test_mod.addCSourceFiles(.{
            .root = b.path("tests/core/rhi"),
            .files = &.{ "test_rhi_types.cpp", "test_rhi_config.cpp", "test_dx12_resources.cpp" },
            .flags = cpp_flags,
        });
        test_mod.addCSourceFiles(.{
            .root = b.path("tests/platform"),
            .files = &.{"test_input.cpp"},
            .flags = cpp_flags,
        });
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

    const run_test = b.addRunArtifact(test_exe);
    const test_step = b.step("test", "Run runtime unit tests");
    test_step.dependOn(&run_test.step);

    // ── Clear screen demo executable (Windows/DX12 only) ─────────────────

    if (is_windows) {
        const demo_mod = b.createModule(.{
            .target = target,
            .optimize = optimize,
        });
        demo_mod.link_libc = true;
        demo_mod.link_libcpp = true;

        demo_mod.addCSourceFiles(.{
            .root = b.path("tools"),
            .files = &.{ "clear_screen_demo.cpp", "dx12_clear_screen.cpp" },
            .flags = cpp_flags,
        });
        // Agility SDK activation (exports D3D12SDKVersion + D3D12SDKPath)
        demo_mod.addCSourceFiles(.{
            .root = b.path("core/rhi/dx12"),
            .files = &.{"dx12_agility.cpp"},
            .flags = cpp_flags,
        });

        for (engine_include_dirs) |dir| demo_mod.addIncludePath(b.path(dir));
        for (thirdparty_include_dirs) |dir| demo_mod.addIncludePath(b.path(dir));
        for (windows_engine_include_dirs) |dir| demo_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| demo_mod.addIncludePath(b.path(dir));
        demo_mod.addIncludePath(b.path("core/rhi/dx12"));
        demo_mod.addIncludePath(b.path("tools"));

        const demo_exe = b.addExecutable(.{
            .name = "clear_screen_demo",
            .root_module = demo_mod,
        });
        demo_exe.linkLibrary(lib);
        demo_exe.linkSystemLibrary("d3d12");
        demo_exe.linkSystemLibrary("dxgi");
        demo_exe.linkSystemLibrary("dxguid");
        demo_exe.linkSystemLibrary("gdi32");
        demo_exe.linkSystemLibrary("user32");
        b.installArtifact(demo_exe);

        const run_demo = b.addRunArtifact(demo_exe);
        const demo_step = b.step("demo", "Run DX12 clear screen demo");
        demo_step.dependOn(&run_demo.step);

        // ── UzEngine Runtime executable (Windows/DX12 only) ─────────────────

        const rt_mod = b.createModule(.{
            .target = target,
            .optimize = optimize,
        });
        rt_mod.link_libc = true;
        rt_mod.link_libcpp = true;

        rt_mod.addCSourceFiles(.{
            .root = b.path("tools"),
            .files = &.{"uzengine_runtime.cpp"},
            .flags = cpp_flags,
        });
        // Agility SDK activation (exports D3D12SDKVersion + D3D12SDKPath)
        rt_mod.addCSourceFiles(.{
            .root = b.path("core/rhi/dx12"),
            .files = &.{"dx12_agility.cpp"},
            .flags = cpp_flags,
        });

        for (engine_include_dirs) |dir| rt_mod.addIncludePath(b.path(dir));
        for (thirdparty_include_dirs) |dir| rt_mod.addIncludePath(b.path(dir));
        for (windows_engine_include_dirs) |dir| rt_mod.addIncludePath(b.path(dir));
        for (windows_thirdparty_include_dirs) |dir| rt_mod.addIncludePath(b.path(dir));
        rt_mod.addIncludePath(b.path("core/rhi/dx12"));
        rt_mod.addIncludePath(b.path("tools"));

        const rt_exe = b.addExecutable(.{
            .name = "uzengine_runtime",
            .root_module = rt_mod,
        });
        rt_exe.linkLibrary(lib);
        rt_exe.linkSystemLibrary("d3d12");
        rt_exe.linkSystemLibrary("dxgi");
        rt_exe.linkSystemLibrary("dxguid");
        rt_exe.linkSystemLibrary("gdi32");
        rt_exe.linkSystemLibrary("user32");
        b.installArtifact(rt_exe);

        const run_rt = b.addRunArtifact(rt_exe);
        const run_step = b.step("run", "Run UzEngine runtime");
        run_step.dependOn(&run_rt.step);

        // ── Agility SDK DLL deployment ───────────────────────────────────────
        // Copy D3D12Core.dll and d3d12SDKLayers.dll to zig-out/bin/D3D12/
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/D3D12Core.dll"), "D3D12/D3D12Core.dll",
        ).step);
        b.getInstallStep().dependOn(&b.addInstallBinFile(
            b.path(tp ++ "agility/bin/x64/d3d12SDKLayers.dll"), "D3D12/d3d12SDKLayers.dll",
        ).step);
    }
}
