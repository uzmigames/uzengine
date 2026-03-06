# AI-First Automated Debugging Pipeline

## Priority: CRITICAL (Maximum)

## Summary

Implement a comprehensive AI-first debugging pipeline that gives AI agents autonomous access to visual debugging, structured crash diagnostics, real-time telemetry, and build verification — eliminating the need for manual user intervention during development.

## Why

AI agents need autonomous access to visual debugging, structured crash diagnostics, real-time telemetry, and build verification to eliminate manual user intervention during development. Without this pipeline, every GPU crash, rendering bug, or build failure requires human intervention to gather information.

## Motivation

The current development workflow is bottlenecked by:
1. **AI blindness** — AI agents must ask the user for screenshots to verify rendering.
2. **Opaque crashes** — GPU crashes (TDR, Device Removed) produce no structured telemetry for AI consumption.
3. **No build feedback loop** — When C++ builds break, AI has no structured path to recovery.
4. **Log noise** — Text logs are unstructured and flood IPC when queried.

## Current State

Several capabilities already exist and should NOT be reimplemented:
- Frame capture: MCP `uz_screenshot` / IPC `engine.screenshot`
- Real-time metrics: MCP `uz_status`
- Shader hot-reload: IPC `shader.reload`
- Engine restart: MCP `uz_restart`
- Headless mode: `--headless` CLI flag

## Scope

### Pillar 1: Visual Debugging Enhancement
- CLI wrapper script for frame capture
- Golden Image regression tests (SSIM-based, threshold 0.98)
- GPU resource introspection (intermediate buffer extraction)

### Pillar 2: Crash Diagnostics
- Global C++ crash handler (SetUnhandledExceptionFilter)
- Structured JSON crash reports with symbolicated stacks
- Windows MiniDump generation (.dmp files)
- DX12 DRED breadcrumb capture on device removal

### Pillar 3: Structured Telemetry
- Subscription-based log filtering over IPC
- Rate limiting (100 msg/s cap)
- RenderGraph introspection API

### Pillar 4: Build Verification
- CMake/MSBuild error parser with JSON output
- MCP tool for automated build + error retrieval
- Retry loop (max 3 iterations)

## Architecture Reference

Full specification: `docs/architecture/ai-debug-pipeline.md`

## Dependencies

- Existing IPC/WebSocket infrastructure
- RHI layer (for readback and DRED)
- CMake build system
- VS Code extension host
