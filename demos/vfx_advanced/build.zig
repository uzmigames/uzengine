/// VFX Advanced Modules Demo -- Build script.
///
/// Compiles the headless VFX advanced demo as a standalone executable. Links
/// against the collision module, mesh render module, LOD evaluator, significance
/// manager, and budget system from the runtime engine. No GPU backend is
/// required -- the demo is entirely headless.
///
/// # Usage
///   cd demos/vfx_advanced
///   zig build
///   ./zig-out/bin/vfx_advanced_demo
///
/// Or from the project root:
///   cd demos/vfx_advanced && zig build && ./zig-out/bin/vfx_advanced_demo

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
        .name     = "vfx_advanced_demo",
        .target   = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    // main.cpp, phase_lod.cpp, and phase_budget.cpp are compiled as separate
    // translation units. vfx_lod.h and vfx_significance_manager.h both declare
    // VFXLodLevel in namespace uz -- including both in a single TU would be a
    // redefinition error. The split keeps each header in its own TU.
    exe.addCSourceFiles(.{
        .files = &.{
            "main.cpp",
            "phase_lod.cpp",
            "phase_budget.cpp",
        },
        .flags = cpp_flags,
    });

    // ── VFX module sources ────────────────────────────────────────
    exe.addCSourceFiles(.{
        .root  = b.path(runtime),
        .files = &.{
            // Collision module (depth-buffer particle collision & response).
            "engine/vfx/modules/collision_module.cpp",
            // Light render module (required by module_registry link chain).
            "engine/vfx/modules/light_render_module.cpp",
            // Mesh render module (instanced 3D mesh particle rendering).
            "engine/vfx/modules/mesh_render_module.cpp",
            // LOD evaluator (distance + screen-coverage quality selection).
            "engine/vfx/lod/vfx_lod.cpp",
            // Significance manager (emitter ranking & budget enforcement).
            "engine/vfx/budget/vfx_significance_manager.cpp",
            // Budget system (quality level presets & per-frame stats).
            "engine/vfx/budget/vfx_budget.cpp",
        },
        .flags = cpp_flags,
    });

    // ── Platform file-system helper ───────────────────────────────
    // Some VFX source files transitively include file_system.h.
    // Compile the correct platform implementation to satisfy the linker.
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
        "engine/vfx/modules",
        "engine/vfx/lod",
        "engine/vfx/budget",
        "platform/windows",
        "platform/linux",
        "platform/macos",
    };

    for (engine_dirs) |dir| {
        exe.addIncludePath(b.path(runtime ++ dir));
    }

    // Thirdparty (spdlog for UZ_LOG macros, nlohmann-json for ParseConfig).
    exe.addIncludePath(b.path(tp ++ "spdlog/include"));
    exe.addIncludePath(b.path(tp ++ "nlohmann"));

    // C++ standard library
    exe.linkLibCpp();

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the VFX advanced modules demo");
    run_step.dependOn(&run_cmd.step);
}
