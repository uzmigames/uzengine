# uz::Application Abstraction for Demos and Runtime

**Category**: architecture
**Tags**: architecture, application, demos, lifecycle, abstraction

## Description

All demos and the main runtime use the uz::Application base class that handles window creation, RHI initialization, main loop, and shutdown. Demos override OnInit/OnUpdate/OnRender/OnShutdown. This eliminated boilerplate duplication across 10+ demos and ensures consistent lifecycle management. Migrated from raw main() functions to uz::Application in commit 67a2fac.

## Example

class MyDemo : public uz::Application {\n    void OnInit() override { /* load assets */ }\n    void OnUpdate(float dt) override { /* game logic */ }\n    void OnRender() override { /* draw calls */ }\n    void OnShutdown() override { /* cleanup */ }\n};\nUZ_APP_MAIN(MyDemo);

## When to Use

When creating new demos. When implementing the main runtime executable. When adding new lifecycle hooks (resize, focus, etc.).

## When NOT to Use

For headless tools (uzcook, uzembed) that don't need windowed rendering.
