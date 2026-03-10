const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };

    const tp = "thirdparty/";

    // ── Runtime module ──────────────────────────────────────────────────

    const runtime_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    runtime_mod.link_libc = true;
    runtime_mod.link_libcpp = true;

    // Engine sources
    runtime_mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{"uz_runtime.cpp"},
        .flags = cpp_flags,
    });

    // Core — logging
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/logging"),
        .files = &.{"uz_log.cpp"},
        .flags = cpp_flags,
    });

    // Core — assertions
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/assertions"),
        .files = &.{"uz_assert.cpp"},
        .flags = cpp_flags,
    });

    // Core — profiling
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/profiling"),
        .files = &.{"uz_profiler.cpp"},
        .flags = cpp_flags,
    });

    // Core — config
    runtime_mod.addCSourceFiles(.{
        .root = b.path("core/config"),
        .files = &.{"uz_config.cpp"},
        .flags = cpp_flags,
    });

    // Platform — Windows
    runtime_mod.addCSourceFiles(.{
        .root = b.path("platform/windows"),
        .files = &.{ "uz_platform_window.cpp", "uz_input.cpp" },
        .flags = cpp_flags,
    });

    // ── spdlog (compiled library) ───────────────────────────────────────

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

    // ── Tracy (compiled client) ─────────────────────────────────────────

    runtime_mod.addCSourceFiles(.{
        .root = b.path(tp ++ "tracy/public"),
        .files = &.{"TracyClient.cpp"},
        .flags = &.{ "-std=c++17", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" },
    });

    // ── Include paths ───────────────────────────────────────────────────

    // Engine
    runtime_mod.addIncludePath(b.path("src"));
    runtime_mod.addIncludePath(b.path("core/logging"));
    runtime_mod.addIncludePath(b.path("core/assertions"));
    runtime_mod.addIncludePath(b.path("core/profiling"));
    runtime_mod.addIncludePath(b.path("core/memory"));
    runtime_mod.addIncludePath(b.path("core/config"));
    runtime_mod.addIncludePath(b.path("platform/windows"));

    // spdlog
    runtime_mod.addIncludePath(b.path(tp ++ "spdlog/include"));

    // Tracy
    runtime_mod.addIncludePath(b.path(tp ++ "tracy/public"));

    // mimalloc
    runtime_mod.addIncludePath(b.path(tp ++ "mimalloc/include"));

    // D3D12 Memory Allocator
    runtime_mod.addIncludePath(b.path(tp ++ "d3d12ma/include"));

    // DirectX Headers
    runtime_mod.addIncludePath(b.path(tp ++ "directx_headers/include"));
    runtime_mod.addIncludePath(b.path(tp ++ "directx_headers/include/directx"));

    // DirectXMath
    runtime_mod.addIncludePath(b.path(tp ++ "directxmath/Inc"));

    // Agility SDK
    runtime_mod.addIncludePath(b.path(tp ++ "agility/include"));

    // DXC (shader compiler headers)
    runtime_mod.addIncludePath(b.path(tp ++ "dxc/inc"));

    // GLM
    runtime_mod.addIncludePath(b.path(tp ++ "glm"));

    // nlohmann/json
    runtime_mod.addIncludePath(b.path(tp ++ "nlohmann"));

    // atomic_queue
    runtime_mod.addIncludePath(b.path(tp ++ "atomic_queue/include"));

    // ── Output ──────────────────────────────────────────────────────────

    const lib = b.addLibrary(.{
        .linkage = .static,
        .name = "uzengine_runtime",
        .root_module = runtime_mod,
    });
    b.installArtifact(lib);

    // ── Test executable ───────────────────────────────────────────────

    const test_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
    });
    test_mod.link_libc = true;
    test_mod.link_libcpp = true;

    // Include paths (same as runtime)
    test_mod.addIncludePath(b.path("src"));
    test_mod.addIncludePath(b.path("core/logging"));
    test_mod.addIncludePath(b.path("core/assertions"));
    test_mod.addIncludePath(b.path("core/profiling"));
    test_mod.addIncludePath(b.path("core/memory"));
    test_mod.addIncludePath(b.path("core/config"));
    test_mod.addIncludePath(b.path("platform/windows"));
    test_mod.addIncludePath(b.path(tp ++ "spdlog/include"));
    test_mod.addIncludePath(b.path(tp ++ "nlohmann"));

    // Test sources
    test_mod.addCSourceFiles(.{
        .root = b.path("tests"),
        .files = &.{"test_runtime.cpp"},
        .flags = cpp_flags,
    });

    // Runtime sources needed by tests
    test_mod.addCSourceFiles(.{
        .root = b.path("src"),
        .files = &.{"uz_runtime.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("core/logging"),
        .files = &.{"uz_log.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("core/assertions"),
        .files = &.{"uz_assert.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("core/profiling"),
        .files = &.{"uz_profiler.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("core/config"),
        .files = &.{"uz_config.cpp"},
        .flags = cpp_flags,
    });
    test_mod.addCSourceFiles(.{
        .root = b.path("platform/windows"),
        .files = &.{ "uz_input.cpp" },
        .flags = cpp_flags,
    });

    // spdlog for tests
    test_mod.addCSourceFiles(.{
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

    const test_exe = b.addExecutable(.{
        .name = "test_runtime",
        .root_module = test_mod,
    });
    b.installArtifact(test_exe);

    const run_test = b.addRunArtifact(test_exe);
    const test_step = b.step("test", "Run runtime unit tests");
    test_step.dependOn(&run_test.step);
}
