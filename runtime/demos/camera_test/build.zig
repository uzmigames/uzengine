const std = @import("std");

/// Standalone build for the camera test demo.
/// Links against the pre-compiled uzengine_runtime.lib from runtime/build/lib/.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const cpp_flags = &.{ "-std=c++17", "-DSPDLOG_COMPILED_LIB" };

    const mod = b.createModule(.{ .target = target, .optimize = optimize });
    mod.link_libc = true;
    mod.link_libcpp = true;

    // Demo sources
    mod.addCSourceFiles(.{
        .root = b.path("."),
        .files = &.{ "main.cpp", "camera_test_demo.cpp" },
        .flags = cpp_flags,
    });
    // FPS camera controller from demos/common
    mod.addCSourceFiles(.{
        .root = b.path("../common"),
        .files = &.{"fps_camera_controller.cpp"},
        .flags = cpp_flags,
    });
    // Editor grid
    mod.addCSourceFiles(.{
        .root = b.path("../../editor/grid"),
        .files = &.{"editor_grid.cpp"},
        .flags = cpp_flags,
    });
    // Screenshot capture
    mod.addCSourceFiles(.{
        .root = b.path("../../editor/screenshot"),
        .files = &.{"screenshot_capture.cpp"},
        .flags = cpp_flags,
    });
    // Bitmap font + text renderer
    mod.addCSourceFiles(.{
        .root = b.path("../../editor/font"),
        .files = &.{ "bitmap_font.cpp", "text_renderer.cpp" },
        .flags = cpp_flags,
    });
    // Stats overlay
    mod.addCSourceFiles(.{
        .root = b.path("../../editor/stats"),
        .files = &.{"stats_overlay.cpp"},
        .flags = cpp_flags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("../../core/rhi/dx12"),
        .files = &.{"dx12_agility.cpp"},
        .flags = cpp_flags,
    });

    // Engine include paths
    const engine_dirs = [_][]const u8{
        "../../src",
        "../../core/logging",
        "../../core/assertions",
        "../../core/profiling",
        "../../core/memory",
        "../../core/config",
        "../../core/math",
        "../../core/containers",
        "../../core/random",
        "../../core/rhi",
        "../../core/shader",
        "../../core/paths",
        "../../core/renderer",
        "../../core/asset",
        "../../platform/windows",
        "../../core/rhi/dx12",
    };
    for (engine_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Generated shader headers (produced by uzembed during runtime build)
    mod.addIncludePath(b.path("../../generated/shaders"));

    // Demo includes
    mod.addIncludePath(b.path("."));
    mod.addIncludePath(b.path("../common"));
    mod.addIncludePath(b.path("../../editor/grid"));
    mod.addIncludePath(b.path("../../editor/screenshot"));
    mod.addIncludePath(b.path("../../editor/font"));
    mod.addIncludePath(b.path("../../editor/stats"));

    // Thirdparty include paths
    const tp_dirs = [_][]const u8{
        "../../thirdparty/spdlog/include",
        "../../thirdparty/tracy/public",
        "../../thirdparty/mimalloc/include",
        "../../thirdparty/directxmath/Inc",
        "../../thirdparty/glm",
        "../../thirdparty/nlohmann",
        "../../thirdparty/atomic_queue/include",
        "../../thirdparty/spirv-reflect",
        "../../thirdparty/spirv-reflect/include",
        "../../thirdparty/xxhash",
        "../../thirdparty/stb",
        "../../thirdparty/tinyexr",
        "../../thirdparty/libjpeg-turbo",
        "../../thirdparty/basis_universal/transcoder",
        "../../thirdparty/astcenc/Source",
        "../../thirdparty/d3d12ma/include",
        "../../thirdparty/directx_headers/include",
        "../../thirdparty/directx_headers/include/directx",
        "../../thirdparty/agility/include",
        "../../thirdparty/dxc/inc",
    };
    for (tp_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Link pre-compiled runtime lib
    mod.addObjectFile(b.path("../../build/lib/uzengine_runtime.lib"));

    const exe = b.addExecutable(.{ .name = "camera_test_demo", .root_module = mod });

    const sys_libs = [_][]const u8{ "d3d12", "dxgi", "dxguid", "gdi32", "user32" };
    for (sys_libs) |l| exe.linkSystemLibrary(l);

    b.installArtifact(exe);

    // Deploy DLLs
    b.getInstallStep().dependOn(&b.addInstallBinFile(
        b.path("../../thirdparty/agility/bin/x64/D3D12Core.dll"), "D3D12/D3D12Core.dll",
    ).step);
    b.getInstallStep().dependOn(&b.addInstallBinFile(
        b.path("../../thirdparty/agility/bin/x64/d3d12SDKLayers.dll"), "D3D12/d3d12SDKLayers.dll",
    ).step);
    b.getInstallStep().dependOn(&b.addInstallBinFile(
        b.path("../../thirdparty/dxc/bin/x64/dxcompiler.dll"), "dxcompiler.dll",
    ).step);
    b.getInstallStep().dependOn(&b.addInstallBinFile(
        b.path("../../thirdparty/dxc/bin/x64/dxil.dll"), "dxil.dll",
    ).step);

    // Run step
    const run = b.addSystemCommand(&.{
        b.getInstallPath(.bin, "camera_test_demo.exe"),
    });
    run.step.dependOn(b.getInstallStep());
    const step = b.step("run", "Run the camera test demo");
    step.dependOn(&run.step);
}
