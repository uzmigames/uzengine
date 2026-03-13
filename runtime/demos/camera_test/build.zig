const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the camera test demo (cross-platform).
/// Run `zig build` from runtime/ first, then `zig build run` from here.
///
/// Platforms:
///   Windows — Links DX12/DX11/DXGI libs, deploys Agility SDK + DXC DLLs
///   macOS   — Links Metal/QuartzCore/AppKit/Cocoa frameworks
///   Linux   — Links Vulkan (via volk, no static vulkan-1.lib needed)
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "camera_test_demo",
        .sources = &.{ "main.cpp", "camera_test_demo.cpp" },
        .extra_sources = &.{
            .{ .root = "../common", .files = &.{"fps_camera_controller.cpp"} },
            .{ .root = "../../editor/grid", .files = &.{"editor_grid.cpp"} },
            .{ .root = "../../editor/font", .files = &.{ "bitmap_font.cpp", "text_renderer.cpp" } },
            .{ .root = "../../editor/stats", .files = &.{"stats_overlay.cpp"} },
        },
        .extra_includes = &.{
            "../common",
            "../../editor/grid",
            "../../editor/font",
            "../../editor/stats",
            "../../build/generated/shaders",
        },
        .deploy_fxc = true,
    });
    demo.install();
}
