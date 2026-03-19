<!-- doc-meta
type: index
category: analysis
theme: asset-pipeline
title: Asset Pipeline Analysis — Loading, Streaming, Cooking
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Asset Pipeline & Loading Analysis

Complete analysis of UE5 asset pipeline, shader code libraries, PSO caching, texture cooking and streaming, runtime loading performance.

## Files

- [loading-analysis-readme.md](loading-analysis-readme.md) — Loading architecture analysis overview (20-second bottleneck analysis)
- [ue5-asset-pipeline-patterns.md](ue5-asset-pipeline-patterns.md) — Detailed technical patterns: texture cooking, shader library, PSO disk cache
- [ue5-loading-architecture.md](ue5-loading-architecture.md) — High-level UE5 loading architecture and comparison with UzEngine
- [uzengine-loading-bottlenecks.md](uzengine-loading-bottlenecks.md) — Analysis of UzEngine loading performance issues and improvement opportunities

## Quick Navigation

### Understanding Why UE5 Loads Fast?
1. [ue5-loading-architecture.md](ue5-loading-architecture.md) — High-level architecture
2. [ue5-asset-pipeline-patterns.md](ue5-asset-pipeline-patterns.md) — Technical patterns
3. [loading-analysis-readme.md](loading-analysis-readme.md) — Bottleneck overview

### Identifying UzEngine Loading Bottlenecks?
- [uzengine-loading-bottlenecks.md](uzengine-loading-bottlenecks.md) — Current issues

## Key Concepts

### UE5 Three-Tier Caching System
1. **FShaderCodeLibrary** — Pre-compiled shader bytecode (no runtime compilation)
2. **FShaderPipelineCache** — PSO state caching with batching
3. **FTexturePlatformData** — Pre-cooked texture format (BC7, ASTC, ETC2)

### Texture Cooking Pipeline
- **Offline process**: PNG/TGA decode → compression → binary format
- **Format selection**: BC7 (DX), ASTC (Vulkan), ETC2 (Mobile)
- **Mipmap generation**: Offline for quality control
- **Runtime loading**: Binary load + direct GPU upload (zero decompression)

### Shader Code Library
- **Hash-indexed bytecode**: O(1) lookup by hash instead of compilation
- **Platform variants**: Separate libraries per SM (SM5, SM6, etc.)
- **No compilation**: All bytecode pre-compiled offline

### PSO Disk Cache
- **Serialized state**: Render state + shader hashes stored on disk
- **Memory-mapped**: Windows file mapping for fast access
- **Incremental build**: New PSOs cached, old ones reused
- **Batch compilation**: Group PSOs for faster driver compilation

### Memory-Mapped Access
- **File mapping**: Windows CreateFileMapping for O(1) page-based access
- **Page faults**: OS loads pages on demand, no explicit reads
- **Cache-efficient**: Avoids RAM allocation, leverages OS page cache

### Async Texture Streaming
- **FAsyncLoadingThread**: Background thread pulls from disk/CDN
- **Virtual memory**: Streaming via virtual address space
- **Priority**: Near-camera assets load first
- **Discard**: Unload when out of view
