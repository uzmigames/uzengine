const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };
    const rt = "../../";
    const tp = rt ++ "thirdparty/";

    // ── Include paths ─────────────────────────────────────────────────────

    const engine_include_dirs = [_][]const u8{
        rt ++ "src",
        rt ++ "core/logging",
        rt ++ "core/assertions",
        rt ++ "core/profiling",
        rt ++ "core/memory",
        rt ++ "core/config",
        rt ++ "core/math",
        rt ++ "core/containers",
        rt ++ "core/random",
        rt ++ "core/rhi",
        rt ++ "core/shader",
        rt ++ "core/paths",
        rt ++ "core/renderer",
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
    };

    const windows_engine_include_dirs = [_][]const u8{
        rt ++ "platform/windows",
    };

    const windows_thirdparty_include_dirs = [_][]const u8{
        tp ++ "d3d12ma/include",
        tp ++ "directx_headers/include",
        tp ++ "directx_headers/include/directx",
        tp ++ "agility/include",
        tp ++ "dxc/inc",
    };

    // ── Module ────────────────────────────────────────────────────────────

    const mod = b.createModule(.{ .target = target, .optimize = optimize });
    mod.link_libc = true;
    mod.link_libcpp = true;

    for (engine_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (windows_engine_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (windows_thirdparty_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    mod.addIncludePath(b.path(rt ++ "core/rhi/dx12"));
    mod.addIncludePath(b.path("."));

    // ── Sources ───────────────────────────────────────────────────────────

    mod.addCSourceFiles(.{
        .root = b.path("."),
        .files = &.{ "main.cpp", "dx12_clear_screen.cpp" },
        .flags = cpp_flags,
    });
    mod.addCSourceFiles(.{
        .root = b.path(rt ++ "core/rhi/dx12"),
        .files = &.{"dx12_agility.cpp"},
        .flags = cpp_flags,
    });

    // ── Executable ────────────────────────────────────────────────────────

    const exe = b.addExecutable(.{ .name = "clear_screen_demo", .root_module = mod });

    // Link runtime library from main build output
    exe.addLibraryPath(b.path(rt ++ "zig-out/lib"));
    exe.addObjectFile(b.path(rt ++ "zig-out/lib/libuzengine_runtime.a"));

    const win_sys_libs = [_][]const u8{ "d3d12", "dxgi", "dxguid", "gdi32", "user32" };
    for (win_sys_libs) |l| exe.linkSystemLibrary(l);

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────────────

    const run = b.addRunArtifact(exe);
    const run_step = b.step("run", "Run clear screen demo");
    run_step.dependOn(&run.step);
}
