const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the HDRI environment demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
/// Assets (.exr files) are packed into data/game.uzpak.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "hdri_test",
        .sources = &.{ "main.cpp", "hdri_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .asset_dirs = &.{ "assets", "../../content" },
        .cook_textures = false, // EXR is HDR — uzcook only handles LDR textures
    });
    demo.install();
}
