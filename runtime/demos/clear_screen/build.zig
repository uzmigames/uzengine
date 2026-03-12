const std = @import("std");

/// Standalone build for the clear screen demo.
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
        .files = &.{ "main.cpp", "clear_screen.cpp" },
        .flags = cpp_flags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("../../engine/rhi/dx12"),
        .files = &.{"dx12_agility.cpp"},
        .flags = cpp_flags,
    });

    // Engine include paths
    const engine_dirs = [_][]const u8{
        "../../engine/app",
        "../../engine/logging",
        "../../engine/assertions",
        "../../engine/profiling",
        "../../engine/memory",
        "../../engine/config",
        "../../engine/math",
        "../../engine/containers",
        "../../engine/random",
        "../../engine/rhi",
        "../../engine/shader",
        "../../engine/paths",
        "../../engine/renderer",
        "../../engine/asset",
        "../../platform/windows",
        "../../engine/rhi/dx12",
        "../../engine/rhi/dx11",
        "../../engine/rhi/vulkan",
    };
    for (engine_dirs) |dir| mod.addIncludePath(b.path(dir));

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
        "../../thirdparty/vulkan_headers/include",
        "../../thirdparty/volk",
        "../../thirdparty/vma/include",
    };
    for (tp_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Demo's own directory
    mod.addIncludePath(b.path("."));

    // Link pre-compiled runtime lib
    mod.addObjectFile(b.path("../../build/lib/uzengine_runtime.lib"));

    const exe = b.addExecutable(.{ .name = "clear_screen_demo", .root_module = mod });

    const sys_libs = [_][]const u8{ "d3d12", "d3d11", "dxgi", "dxguid", "gdi32", "user32", "ws2_32", "dbghelp" };
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
        b.getInstallPath(.bin, "clear_screen_demo.exe"),
    });
    run.step.dependOn(b.getInstallStep());
    const step = b.step("run", "Run the clear screen demo");
    step.dependOn(&run.step);
}
