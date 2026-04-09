# Proposal: Metal RHI Build System Integration

## Why

The Metal backend requires build system changes to compile Objective-C++ (`.mm`) files, link Apple frameworks (Metal, QuartzCore, Foundation, AppKit/UIKit), integrate metal-cpp headers, and conditionally include Metal source files only on macOS/iOS. Both `build.zig` and `CMakeLists.txt` must be updated, plus the RHI factory needs to route `RHIBackend::Metal` to the MetalDevice constructor.

## What Changes

### build.zig Updates
1. **Platform detection**: Add `is_macos` / `is_ios` target checks
2. **Metal sources**: Conditionally add `engine/rhi/metal/*.cpp` and `engine/rhi/metal/*.mm`
3. **Objective-C++ compilation**: Add `-ObjC++` flag for `.mm` files via Zig's `addCSourceFiles`
4. **Framework linking**: Link `Metal.framework`, `QuartzCore.framework`, `Foundation.framework`, `AppKit.framework` (macOS) or `UIKit.framework` (iOS)
5. **metal-cpp headers**: Add `thirdparty/metal-cpp/` to include paths

### CMakeLists.txt Updates
1. Mirror all build.zig changes for the CMake fallback path
2. Use `find_library()` for Apple frameworks
3. Set `LANGUAGE OBJCXX` for `.mm` files

### Factory Registration
1. Update `rhi_factory.cpp` — add `case RHIBackend::Metal: return new MetalDevice();`
2. Update `rhi_backend.h` — ensure `RHIBackend::Metal` is already in the enum (it is)
3. Platform guard — return error on non-Apple platforms

### Thirdparty
1. Add `metal-cpp` headers from Apple's open-source release
2. Add `thirdparty/metal-cpp/Metal/Metal.hpp`, `QuartzCore/QuartzCore.hpp`, `Foundation/Foundation.hpp`

## Impact

- Affected specs: `docs/engine/06-build-toolchain/01-zig-build-system.md`, `docs/engine/06-build-toolchain/02-thirdparty-catalog.md`
- Affected code: `runtime/build.zig`, `runtime/CMakeLists.txt`, `runtime/engine/rhi/rhi_factory.cpp`, `thirdparty/metal-cpp/` (new)
- Breaking change: NO — conditional compilation, existing platforms unaffected
- User benefit: Metal backend compiles on macOS with `zig build` or `cmake`
