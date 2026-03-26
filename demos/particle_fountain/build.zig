/// Particle Fountain Demo — Build script.
///
/// Compiles the headless VFX demo as a standalone executable.
/// Links against the VFX core, budget, emitter, and RHI types from the
/// runtime engine.
///
/// # Usage
///   cd demos/particle_fountain
///   zig build
///   ./zig-out/bin/particle_fountain
///
/// Or from the project root:
///   cd demos/particle_fountain && zig build && ./zig-out/bin/particle_fountain

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
        .name = "particle_fountain",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── VFX engine sources ────────────────────────────────────────
    exe.addCSourceFiles(.{
        .root = b.path(runtime),
        .files = &.{
            "engine/vfx/core/gpu_particle_buffer.cpp",
            "engine/vfx/core/gpu_particle_sort.cpp",
            "engine/vfx/core/particle_budget.cpp",
            "engine/vfx/components/particle_emitter.cpp",
            "engine/vfx/vfx_system.cpp",
        },
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
        "engine/vfx/core",
        "engine/vfx/components",
        "engine/vfx/passes",
    };

    for (engine_dirs) |dir| {
        exe.addIncludePath(b.path(runtime ++ dir));
    }

    // Thirdparty (spdlog for log macros)
    exe.addIncludePath(b.path(tp ++ "spdlog/include"));

    // C++ standard library
    exe.linkLibCpp();

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the particle fountain demo");
    run_step.dependOn(&run_cmd.step);
}
