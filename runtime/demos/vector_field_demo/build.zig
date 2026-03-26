const std = @import("std");
const common = @import("uz_demo_common");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "vector_field_demo",
        .sources = &.{"main.cpp"},
        .use_demo_application = false,
    });
    demo.install();
}
