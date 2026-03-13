const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the clear screen demo (cross-platform).
/// Run `zig build` from runtime/ first, then `zig build run` from here.
///
/// Platforms:
///   Windows — Links DX12/DX11/DXGI libs, deploys Agility SDK + DXC DLLs
///   macOS   — Links Metal/QuartzCore/AppKit/Cocoa frameworks
///   Linux   — Links Vulkan (via volk, no static vulkan-1.lib needed)
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "clear_screen_demo",
        .sources = &.{ "main.cpp", "clear_screen.cpp" },
    });
    demo.install();
}
