/// Attractor Demo -- Build script.
///
/// Compiles the headless VFX attractor black hole demo as a standalone executable.
/// Links against VFX core, modules, curves, budget, emitter, and RHI types
/// from the runtime engine. Includes all extended VFX module sources required
/// by the attractor system (collision, light/mesh render, vector field, sub-emitter,
/// orbit, pin, distance constraint, attractor buffer, GPU sort, and VFX events).
///
/// # Usage
///   cd demos/attractor_demo
///   zig build
///   ./zig-out/bin/attractor_demo
///
/// Or from the project root:
///   cd demos/attractor_demo && zig build && ./zig-out/bin/attractor_demo

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
        .name = "attractor_demo",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── VFX engine sources (core + modules + curves) ──────────────
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
            "engine/vfx/modules/collision_module.cpp",
            "engine/vfx/modules/light_render_module.cpp",
            "engine/vfx/modules/mesh_render_module.cpp",
            "engine/vfx/modules/attractor_module.cpp",
            "engine/vfx/modules/vector_field_module.cpp",
            "engine/vfx/modules/sub_emitter_module.cpp",
            "engine/vfx/modules/orbit_module.cpp",
            "engine/vfx/modules/pin_module.cpp",
            "engine/vfx/modules/distance_constraint_module.cpp",
            "engine/vfx/core/attractor_buffer.cpp",
            "engine/vfx/core/sub_emitter_readback.cpp",
            "engine/vfx/core/gpu_particle_sort.cpp",
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
        "engine/vfx/events",
        "engine/vfx/budget",
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

    const run_step = b.step("run", "Run the attractor demo");
    run_step.dependOn(&run_cmd.step);
}
