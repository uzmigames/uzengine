/// Trail/Ribbon VFX System Demo -- Build script.
///
/// Compiles the headless trail demo as a standalone executable. Links against
/// the trail subsystem (trail_asset, trail_component, trail_mesh_builder,
/// trail_point, trail_system) plus logging, math, and assertions from the
/// runtime engine. No GPU backend is required -- the demo is entirely headless.
///
/// # Usage
///   cd demos/trails
///   zig build
///   ./zig-out/bin/trail_demo
///
/// Or from the project root:
///   cd demos/trails && zig build && ./zig-out/bin/trail_demo

const std = @import("std");

pub fn build(b: *std.Build) void {
    const target   = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags: []const []const u8 = &.{
        "-std=c++17",
        "-DSPDLOG_COMPILED_LIB",
    };

    const runtime = "../../runtime/";
    const tp      = "../../runtime/thirdparty/";

    const exe = b.addExecutable(.{
        .name   = "trail_demo",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── Trail engine sources ──────────────────────────────────────
    exe.addCSourceFiles(.{
        .root  = b.path(runtime),
        .files = &.{
            "engine/vfx/trails/trail_asset.cpp",
            "engine/vfx/trails/trail_component.cpp",
            "engine/vfx/trails/trail_mesh_builder.cpp",
            "engine/vfx/trails/trail_system.cpp",
        },
        .flags = cpp_flags,
    });

    // ── Platform file-system helper (required by trail_asset.cpp::Load) ───
    // The demo only calls LoadFromString (no disk I/O), but trail_asset.cpp
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
        .root  = b.path(runtime),
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
        "engine/vfx/trails",
        "platform/windows",
        "platform/linux",
        "platform/macos",
    };

    for (engine_dirs) |dir| {
        exe.addIncludePath(b.path(runtime ++ dir));
    }

    // Thirdparty (spdlog for UZ_LOG macros, nlohmann-json for asset parsing)
    exe.addIncludePath(b.path(tp ++ "spdlog/include"));
    exe.addIncludePath(b.path(tp ++ "nlohmann"));

    // C++ standard library
    exe.linkLibCpp();

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the trail/ribbon VFX system demo");
    run_step.dependOn(&run_cmd.step);
}
