const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the IBL test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "ibl_test",
        .sources = &.{ "main.cpp", "ibl_test.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .extra_deploy = &.{
            .{ .source = "assets/cube.uzmesh",                .dest = "assets/cube.uzmesh" },
            .{ .source = "assets/Stonebricks_BaseColor.jpg",  .dest = "assets/Stonebricks_BaseColor.jpg" },
            .{ .source = "assets/Stonebricks_Normal.jpg",     .dest = "assets/Stonebricks_Normal.jpg" },
            .{ .source = "assets/Stonebricks_Roughness.jpg",  .dest = "assets/Stonebricks_Roughness.jpg" },
            .{ .source = "assets/Stonebricks_Metallic.jpg",   .dest = "assets/Stonebricks_Metallic.jpg" },
            .{ .source = "assets/Stonebricks_AO.jpg",         .dest = "assets/Stonebricks_AO.jpg" },
        },
    });
    demo.install();
}
