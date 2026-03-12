## 1. Implementation

### Thirdparty Setup
- [ ] 1.1 Download and add `metal-cpp` headers to `thirdparty/metal-cpp/`
- [ ] 1.2 Verify metal-cpp includes: `Metal/Metal.hpp`, `Foundation/Foundation.hpp`, `QuartzCore/QuartzCore.hpp`
- [ ] 1.3 Add `thirdparty/metal-cpp/` to `.gitignore` exclusions (ensure it's tracked)

### build.zig
- [ ] 1.4 Add `is_macos` and `is_ios` target detection (`target.os_tag == .macos`)
- [ ] 1.5 Conditionally add `engine/rhi/metal/*.cpp` sources when `is_macos or is_ios`
- [ ] 1.6 Add `.mm` file compilation support — use `addCSourceFiles` with `-ObjC++` flag for `metal_swapchain.mm`
- [ ] 1.7 Add `thirdparty/metal-cpp/` to include paths
- [ ] 1.8 Link Apple frameworks: `Metal`, `QuartzCore`, `Foundation`, `AppKit` (macOS) / `UIKit` (iOS)
- [ ] 1.9 Add `UZ_BACKEND_METAL` compile define when targeting Apple platforms
- [ ] 1.10 Verify DX12/DX11 sources are NOT included on macOS (platform guards)

### CMakeLists.txt
- [ ] 1.11 Add `if(APPLE)` block for Metal backend source inclusion
- [ ] 1.12 Use `find_library()` for Metal, QuartzCore, Foundation, AppKit frameworks
- [ ] 1.13 Set `LANGUAGE OBJCXX` property for `.mm` files
- [ ] 1.14 Add metal-cpp include path
- [ ] 1.15 Add `UZ_BACKEND_METAL` compile definition

### Factory Registration
- [ ] 1.16 Update `rhi_factory.cpp` — add `case RHIBackend::Metal:` with `#if defined(__APPLE__)` guard
- [ ] 1.17 Return descriptive error on non-Apple platforms (`"Metal backend requires macOS or iOS"`)
- [ ] 1.18 Update auto-detection priority: macOS → Metal, Linux → Vulkan, Windows → DX12

### Validation
- [ ] 1.19 Verify full build compiles on macOS with `zig build`
- [ ] 1.20 Verify Windows build still works (Metal sources excluded)
- [ ] 1.21 Verify `--rhi metal` works on macOS, errors gracefully on Windows/Linux

## 2. Testing

- [ ] 2.1 Test macOS build compiles without errors
- [ ] 2.2 Test Windows build is unaffected (no Metal sources compiled)
- [ ] 2.3 Test `--rhi metal` auto-detection on macOS
- [ ] 2.4 Test `--rhi metal` rejection on Windows/Linux with clear error message
- [ ] 2.5 Test all Apple frameworks link correctly
- [ ] 2.6 Verify metal-cpp headers are accessible from Metal source files

## 3. Documentation

- [ ] 3.1 Update `docs/engine/06-build-toolchain/01-zig-build-system.md` with macOS build instructions
- [ ] 3.2 Update `docs/engine/06-build-toolchain/02-thirdparty-catalog.md` with metal-cpp entry
- [ ] 3.3 Document platform-specific build flags and framework requirements
- [ ] 3.4 Update CHANGELOG with Metal build support
