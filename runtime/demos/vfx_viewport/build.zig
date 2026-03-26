const std = @import("std");
const common = @import("uz_demo_common");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "vfx_viewport",
        .sources = &.{
            "main.cpp",
            "vfx_viewport_demo.cpp",
            "vfx_viewport_mesh.cpp",
            "vfx_viewport_vfx.cpp",
        },
        .use_demo_application = true,
    });
    demo.install();
}
