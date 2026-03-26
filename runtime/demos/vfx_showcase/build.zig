const std = @import("std");
const common = @import("uz_demo_common");

pub fn build(b: *std.Build) void {
    var demo = common.createDemo(b, .{
        .name = "vfx_showcase",
        .sources = &.{"main.cpp"},
        .use_demo_application = false,
        // Deploy the Lua script alongside the executable so it is available
        // at runtime relative to the binary (uz::GetExeDir() resolution).
        .extra_deploy = &.{
            .{ .source = "lua_vfx_showcase.lua", .dest = "lua_vfx_showcase.lua" },
        },
    });
    demo.install();
}
