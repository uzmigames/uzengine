# Tasks: Code Quality Fixes

## 1. Critical — File Size
- [ ] 1.1 Split `dx12_resource_creation.cpp` (1,427 lines) into: dx12_texture_creation.cpp, dx12_buffer_creation.cpp, dx12_pso_creation.cpp (CQ-01)
- [ ] 1.2 Split `dx12_backend.cpp` (~1,100 lines) into: dx12_backend.cpp (core), dx12_command_list.cpp (recording), dx12_sync.cpp (fences) (CQ-03)
- [ ] 1.3 Clean up orphaned forward_pbr_pass references from build system (CQ-02)

## 2. High — Engine API Compliance
- [ ] 2.1 Find and replace ALL raw `spdlog::info/error/warn/debug` calls with `UZ_LOG(Category, Level, ...)` (CQ-04)
- [ ] 2.2 Add `UZ_PROFILE_SCOPE` to: shader compilation, resource creation, PSO creation, texture upload (CQ-05)
- [ ] 2.3 Replace magic numbers in camera.h with named constants (`kDefaultFOV`, `kDefaultNearPlane`, `kDefaultFarPlane`) (CQ-07)
- [ ] 2.4 Break functions >40 lines in renderer passes into smaller named functions (CQ-08)

## 3. Medium — Documentation & Consistency
- [ ] 3.1 Add complete Rust-style `///` docs to all public items in math headers (CQ-06)
- [ ] 3.2 Add `// ───` section separators to .cpp files that lack them (CQ-11)
- [ ] 3.3 Standardize method naming to PascalCase across all engine code (CQ-12)
- [ ] 3.4 Add proper error context to log messages: include what failed, why, and input (CQ-10)

## 4. Low — Demo Cleanup
- [ ] 4.1 Extract common demo rendering setup into shared utility (CQ-09)

## 5. Testing
- [ ] 5.1 Verify build passes after all splits (zig, msvc, clang)
- [ ] 5.2 Verify all existing tests still pass
- [ ] 5.3 Run clang-tidy and fix any new warnings

## 6. Documentation
- [ ] 6.1 Update file references in docs/ after splits
