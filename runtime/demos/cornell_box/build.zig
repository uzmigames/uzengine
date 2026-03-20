const std = @import("std");
const common = @import("uz_demo_common");

/// Standalone build for the Cornell Box GI test demo.
/// Run `zig build` from runtime/ first, then `zig build` from here.
pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "cornell_box",
        .sources = &.{ "main.cpp", "cornell_box.cpp" },
        .use_demo_application = false,
        .deploy_fxc = true,
        .extra_deploy = &.{
            .{ .source = "assets/cube.uzmesh", .dest = "assets/cube.uzmesh" },
        },
    });
    demo.install();
}
