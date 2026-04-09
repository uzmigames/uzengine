# Proposal: Code Quality Fixes

## Why
Internal audit (2026-03-13) found 2 CRITICAL and 6 HIGH code quality issues. `dx12_resource_creation.cpp`
is at 1,427 lines (limit is 1,500), raw spdlog calls bypass engine logging, and missing profiling scopes
hide performance bottlenecks. These violate CLAUDE.md mandatory rules.

## What Changes
- Split dx12_resource_creation.cpp by resource type (CQ-01)
- Split dx12_backend.cpp proactively at ~1,100 lines (CQ-03)
- Replace all raw spdlog:: calls with UZ_LOG macro (CQ-04)
- Add UZ_PROFILE_SCOPE to critical paths (CQ-05)
- Add Rust-style docs to math headers (CQ-06)
- Replace magic numbers with named constants in camera.h (CQ-07)
- Break long functions (>40 lines) into smaller focused functions (CQ-08)
- Extract shared demo setup code into common utilities (CQ-09)

## Impact
- Affected specs: none (internal quality)
- Affected code: runtime/engine/rhi/dx12/*.cpp, runtime/engine/renderer/camera.h, runtime/engine/math/*.h
- Breaking change: NO
- User benefit: Maintainable codebase, better profiling visibility, consistent logging
