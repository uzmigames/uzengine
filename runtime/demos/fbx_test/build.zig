const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the FBX mesh test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
/// Assets are packed via `uzpack` into data/game.uzpak — no loose files deployed here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "fbx_test",
        .sources = &.{ "main.cpp", "fbx_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .asset_dirs = &.{"assets"},
        .extra_deploy = &.{
            .{ .source = "assets/materials/mi_procgrid.uzmat", .dest = "assets/materials/mi_procgrid.uzmat" },
        },
    });
    demo.install();
}
