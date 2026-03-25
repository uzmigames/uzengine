/// Distortion Effect Demo -- Build script.
///
/// Compiles the headless distortion VFX demo as a standalone executable.
/// Links against VFX core, modules, curves, budget, emitter, and distortion
/// pass type headers from the runtime engine. The distortion pass .cpp files
/// are NOT linked -- the demo only exercises the struct layouts from the headers
/// (DistortionConstants, DistortionEmitterData, DistortionResolveConstants).
///
/// # Usage
///   cd demos/distortion_effect
///   zig build
///   ./zig-out/bin/distortion_effect
///
/// Or from the project root:
///   cd demos/distortion_effect && zig build && ./zig-out/bin/distortion_effect

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
        .name = "distortion_effect",
        .target = target,
        .optimize = optimize,
    });

    // ── Demo source ───────────────────────────────────────────────
    exe.addCSourceFiles(.{
        .files = &.{"main.cpp"},
        .flags = cpp_flags,
    });

    // ── VFX engine sources (core + modules + curves) ──────────────
    // The distortion pass .cpp files depend on the full render graph
    // (RgBuilder, RgPassBuilder, etc.) which is not included in the
    // headless demo. We only use the pass header types for struct layout
    // validation -- no pass function calls at runtime.
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
        },
        .flags = cpp_flags,
    });

    // ── Include paths ─────────────────────────────────────────────
    // Both engine/renderer/core/memory/ and engine/renderer/render_graph/
    // are needed for distortion_pass.h's transitive includes:
    //   distortion_pass.h -> cb_ring_allocator.h  (engine/renderer/core/memory/)
    //   distortion_pass.h -> render_graph_types.h (engine/renderer/render_graph/)
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
        "engine/renderer/core/memory",
        "engine/renderer/render_graph",
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

    const run_step = b.step("run", "Run the distortion effect demo");
    run_step.dependOn(&run_cmd.step);
}
