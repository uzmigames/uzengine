const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the textured quad demo (cross-platform).
/// Run `zig build` from runtime/ first, then `zig build run` from here.
///
/// Platforms:
///   Windows — Links DX12/DX11/DXGI libs, deploys Agility SDK + DXC DLLs
///   macOS   — Links Metal/QuartzCore/AppKit/Cocoa frameworks
///   Linux   — Links Vulkan (via volk, no static vulkan-1.lib needed)
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "textured_quad_demo",
        .sources = &.{ "main.cpp", "textured_quad_demo.cpp" },
        .deploy_fxc = true,
        .extra_includes = &.{
            "../../engine/renderer/mesh",
            "../../engine/renderer/upload",
        },
    });
    demo.install();
}
