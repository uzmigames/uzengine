const std = @import("std");

/// Standalone build for the clear screen demo (cross-platform).
/// Links against the pre-compiled uzengine_runtime lib from runtime/build/lib/.
/// Run `zig build` from runtime/ first, then `zig build run` from here.
///
/// Platforms:
///   Windows — Links DX12/DX11/DXGI libs, deploys Agility SDK + DXC DLLs
///   macOS   — Links Metal/QuartzCore/AppKit/Cocoa frameworks
///   Linux   — Links Vulkan (via volk, no static vulkan-1.lib needed)
pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const is_windows = target.result.os.tag == .windows;
    const is_macos = target.result.os.tag == .macos;
    const is_apple = is_macos or (target.result.os.tag == .ios);

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
        .root = b.path("../../editor/screenshot"),
        .files = &.{"screenshot_capture.cpp"},
        .flags = cpp_flags,
    });

    // Windows-only: DX12 Agility SDK loader
    if (is_windows) {
        mod.addCSourceFiles(.{
            .root = b.path("../../engine/rhi/dx12"),
            .files = &.{"dx12_agility.cpp"},
            .flags = cpp_flags,
        });
    }

    // ── Engine include paths (cross-platform) ───────────────────────────
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
        "../../engine/rhi/vulkan",
        "../../editor/screenshot",
    };
    for (engine_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Platform include paths
    if (is_windows) {
        mod.addIncludePath(b.path("../../platform/windows"));
        mod.addIncludePath(b.path("../../engine/rhi/dx12"));
        mod.addIncludePath(b.path("../../engine/rhi/dx11"));
    }
    if (is_apple) {
        mod.addIncludePath(b.path("../../platform/macos"));
        mod.addIncludePath(b.path("../../engine/rhi/metal"));
    }

    // ── Thirdparty include paths (cross-platform) ───────────────────────
    const tp_dirs = [_][]const u8{
        "../../thirdparty/spdlog/include",
        "../../thirdparty/tracy/public",
        "../../thirdparty/mimalloc/include",
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
        "../../thirdparty/vulkan_headers/include",
        "../../thirdparty/volk",
        "../../thirdparty/vma/include",
    };
    for (tp_dirs) |dir| mod.addIncludePath(b.path(dir));

    // Windows-only thirdparty
    if (is_windows) {
        mod.addIncludePath(b.path("../../thirdparty/directxmath/Inc"));
        mod.addIncludePath(b.path("../../thirdparty/d3d12ma/include"));
        mod.addIncludePath(b.path("../../thirdparty/directx_headers/include"));
        mod.addIncludePath(b.path("../../thirdparty/directx_headers/include/directx"));
        mod.addIncludePath(b.path("../../thirdparty/agility/include"));
        mod.addIncludePath(b.path("../../thirdparty/dxc/inc"));
    }

    // Demo's own directory
    mod.addIncludePath(b.path("."));

    // ── Link pre-compiled runtime lib ───────────────────────────────────
    if (is_windows) {
        mod.addObjectFile(b.path("../../build/lib/uzengine_runtime.lib"));
    } else {
        mod.addObjectFile(b.path("../../build/lib/libuzengine_runtime.a"));
    }

    const exe = b.addExecutable(.{ .name = "clear_screen_demo", .root_module = mod });

    // ── System libraries / frameworks ───────────────────────────────────
    if (is_windows) {
        const win_libs = [_][]const u8{ "d3d12", "d3d11", "dxgi", "dxguid", "gdi32", "user32", "ws2_32", "dbghelp", "winmm" };
        for (win_libs) |l| exe.linkSystemLibrary(l);
    }
    if (is_apple) {
        exe.linkFramework("Metal");
        exe.linkFramework("QuartzCore");
        exe.linkFramework("Foundation");
        exe.linkFramework("AppKit");
        exe.linkFramework("Cocoa");
    }

    b.installArtifact(exe);

    // ── DLL deployment (Windows only) ───────────────────────────────────
    if (is_windows) {
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
    }

    // ── Run step (forwards CLI args: zig build run -- --rhi metal --frames 30)
    const run = b.addRunArtifact(exe);
    run.step.dependOn(b.getInstallStep());
    const step = b.step("run", "Run the clear screen demo");
    step.dependOn(&run.step);
}
