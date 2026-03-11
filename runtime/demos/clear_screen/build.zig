const std = @import("std");

/// Called by the main runtime/build.zig to add the clear screen demo.
pub fn addTo(
    b: *std.Build,
    lib: *std.Build.Step.Compile,
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    engine_include_dirs: []const []const u8,
    thirdparty_include_dirs: []const []const u8,
    windows_engine_include_dirs: []const []const u8,
    windows_thirdparty_include_dirs: []const []const u8,
    cpp_flags: []const []const u8,
) void {
    const mod = b.createModule(.{ .target = target, .optimize = optimize });
    mod.link_libc = true;
    mod.link_libcpp = true;

    for (engine_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (thirdparty_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (windows_engine_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    for (windows_thirdparty_include_dirs) |dir| mod.addIncludePath(b.path(dir));
    mod.addIncludePath(b.path("core/rhi/dx12"));
    mod.addIncludePath(b.path("demos/clear_screen"));

    mod.addCSourceFiles(.{
        .root = b.path("demos/clear_screen"),
        .files = &.{ "main.cpp", "dx12_clear_screen.cpp" },
        .flags = cpp_flags,
    });
    mod.addCSourceFiles(.{
        .root = b.path("core/rhi/dx12"),
        .files = &.{"dx12_agility.cpp"},
        .flags = cpp_flags,
    });

    const exe = b.addExecutable(.{ .name = "clear_screen_demo", .root_module = mod });
    exe.linkLibrary(lib);

    const win_sys_libs = [_][]const u8{ "d3d12", "dxgi", "dxguid", "gdi32", "user32" };
    for (win_sys_libs) |l| exe.linkSystemLibrary(l);

    b.installArtifact(exe);

    // Run from install dir so Agility SDK DLLs are found
    const run = b.addSystemCommand(&.{
        b.getInstallPath(.bin, "clear_screen_demo.exe"),
    });
    run.step.dependOn(b.getInstallStep());
    const step = b.step("demo-clear", "Run DX12 clear screen demo");
    step.dependOn(&run.step);
}
