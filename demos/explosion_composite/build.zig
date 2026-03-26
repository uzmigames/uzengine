/// Explosion Composite Demo -- Build script.
///
/// Compiles the headless multi-emitter VFX demo as a standalone executable.
/// Links against VFX core, modules, curves, budget, emitter, composite asset,
/// event system, and RHI types from the runtime engine.
///
/// # Usage
///   cd demos/explosion_composite
///   zig build
///   ./zig-out/bin/explosion_composite_demo
///
/// Or from the project root:
///   cd demos/explosion_composite && zig build && ./zig-out/bin/explosion_composite_demo

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
        .name = "explosion_composite_demo",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── VFX engine sources (core + modules + curves + asset + events) ──
    exe.addCSourceFiles(.{
        .root = b.path(runtime),
        .files = &.{
            "engine/vfx/core/gpu_particle_buffer.cpp",
            "engine/vfx/core/particle_budget.cpp",
            "engine/vfx/components/particle_emitter.cpp",
            "engine/vfx/vfx_system.cpp",
            "engine/vfx/modules/module_registry.cpp",
            "engine/vfx/modules/module_compositor.cpp",
            "engine/vfx/modules/register_builtin_modules.cpp",
            "engine/vfx/modules/emitter_module_stack.cpp",
            "engine/vfx/modules/spawn_module.cpp",
            "engine/vfx/modules/update_module.cpp",
            "engine/vfx/modules/decay_module.cpp",
            "engine/vfx/modules/sprite_render_module.cpp",
            "engine/vfx/modules/turbulence_module.cpp",
            "engine/vfx/modules/constraint_module.cpp",
            "engine/vfx/curves/vfx_curve.cpp",
            "engine/vfx/curves/vfx_curve_loader.cpp",
            "engine/vfx/asset/vfx_composite.cpp",
            "engine/vfx/events/vfx_event.cpp",
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
        "engine/vfx/modules",
        "engine/vfx/curves",
        "engine/vfx/passes",
        "engine/vfx/asset",
        "engine/vfx/events",
        "engine/vfx/parameters",
    };

    for (engine_dirs) |dir| {
        exe.addIncludePath(b.path(runtime ++ dir));
    }

    // Thirdparty (spdlog for log macros, nlohmann-json for composite parsing)
    exe.addIncludePath(b.path(tp ++ "spdlog/include"));
    exe.addIncludePath(b.path(tp ++ "nlohmann"));

    // C++ standard library
    exe.linkLibCpp();

    b.installArtifact(exe);

    // ── Run step ──────────────────────────────────────────────────
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());

    const run_step = b.step("run", "Run the explosion composite demo");
    run_step.dependOn(&run_cmd.step);
}
