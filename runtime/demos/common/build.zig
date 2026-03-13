const std = @import("std");
const build_common = @import("build_common.zig");

pub fn build(b: *std.Build) void {
    _ = b;
}

// Re-export public API from build_common.zig
pub const createDemo = build_common.createDemo;
pub const DemoConfig = build_common.DemoConfig;
pub const DemoHandle = build_common.DemoHandle;
pub const ExtraSource = build_common.ExtraSource;
pub const DeployFile = build_common.DeployFile;
