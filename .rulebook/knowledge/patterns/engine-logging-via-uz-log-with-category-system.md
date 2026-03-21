# Engine Logging via UZ_LOG with Category System

**Category**: code
**Tags**: logging, assertions, profiling, engine-api, macros

## Description

All engine logging must use UZ_LOG(Category, Level, ...) macro — never spdlog directly. Each subsystem declares its own log category via UZ_DECLARE_LOG_CATEGORY / UZ_DEFINE_LOG_CATEGORY. This enables per-category filtering, consistent formatting, and engine-level log routing. Similarly: UZ_CHECK/UZ_ENSURE for assertions (not raw assert()), UZ_PROFILE_SCOPE for profiling (not raw Tracy macros).

## Example

// CORRECT:\nUZ_LOG(Asset, Info, \"Loaded texture: path='{}'\", path);\nUZ_LOG(Renderer, Error, \"Failed to create PSO: reason='{}'\", err);\nUZ_CHECK(buffer != nullptr); // crashes on failure\nUZ_ENSURE(size > 0);         // recoverable\nUZ_PROFILE_SCOPE(\"SceneRenderer::Render\");\n\n// WRONG:\nspdlog::info(\"Loaded texture\");  // bypasses engine logging\nassert(buffer);                  // no context, no engine integration\nTracyZoneScoped;                 // raw Tracy, not engine macro

## When to Use

Every time you need to log in engine code. When creating a new subsystem (add a new log category). When adding assertions or profiling scopes.

## When NOT to Use

In thirdparty code that has its own logging. In unit tests where direct output is acceptable.
