/// Decal System Demo -- Build script.
///
/// Compiles the headless decal subsystem demo as a standalone executable.
/// Links against decal pool, atlas, asset, component, manager, logging,
/// math, assertions, and RHI types from the runtime engine.
///
/// # Usage
///   cd demos/decals
///   zig build
///   ./zig-out/bin/decal_demo
///
/// Or from the project root:
///   cd demos/decals && zig build && ./zig-out/bin/decal_demo

const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags: []const []const u8 = &.{
        "-std=c++17",
        "-DSPDLOG_COMPILED_LIB",
    };

    const runtime = "../../runtime/";
    const tp = "../../runtime/thirdparty/";

    const exe = b.addExecutable(.{
        .name = "decal_demo",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── Decal engine sources ──────────────────────────────────────
    exe.addCSourceFiles(.{
        .root = b.path(runtime),
        .files = &.{
            "engine/vfx/decals/decal_manager.cpp",
            "engine/vfx/decals/decal_pool.cpp",
            "engine/vfx/decals/decal_atlas.cpp",
            "engine/vfx/decals/decal_asset.cpp",
            "engine/vfx/decals/decal_component.cpp",
        },
        .flags = cpp_flags,
    });

    // ── Platform file-system helper (required by decal_asset.cpp::Load) ───
    // The demo only calls LoadFromString (no disk I/O), but decal_asset.cpp
    // unconditionally includes file_system.h and defines FileReadAllText, so
    // we must compile the correct platform implementation.
    const os_tag = target.result.os.tag;
    const fs_src = if (os_tag == .windows)
        "platform/windows/file_system.cpp"
    else if (os_tag == .macos)
        "platform/macos/file_system.cpp"
    else
        "platform/linux/file_system.cpp";

    exe.addCSourceFiles(.{
        .root = b.path(runtime),
        .files = &.{fs_src},
        .flags = cpp_flags,
    });

    // ── Include paths ─────────────────────────────────────────────
    const engine_dirs = [_][]const u8{
        "engine",
        "engine/logging",
        "engine/assertions",
        "engine/profiling",
        "engine/memory",
        "engine/math",
        "engine/rhi",
        "engine/vfx",
        "engine/vfx/decals",
        "platform/windows",
        "platform/linux",
        "platform/macos",
    };

    for (engine_dirs) |dir| {
        exe.addIncludePath(b.path(runtime ++ dir));
    }

    // Thirdparty (spdlog for log macros, nlohmann-json for asset parsing)
    exe.addIncludePath(b.path(tp ++ "spdlog/include"));
    exe.addIncludePath(b.path(tp ++ "nlohmann"));

    // C++ standard library
    exe.linkLibCpp();

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the decal system demo");
    run_step.dependOn(&run_cmd.step);
}
