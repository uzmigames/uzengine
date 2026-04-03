const std = @import("std");
const common = @import("uz_demo_common");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "vfx_viewport_app",
        .sources = &.{
            "main.cpp",
            "vfx_viewport_app.cpp",
        },
        .use_demo_application = false,
    });
    demo.install();
}
