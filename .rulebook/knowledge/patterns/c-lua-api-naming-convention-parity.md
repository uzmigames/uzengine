# C++/Lua API Naming Convention Parity

**Category**: code
**Tags**: lua, cpp, api-parity, naming-convention, bindings

## Description

Every engine feature implemented in C++ must also be exposed to Lua with naming parity: C++ uses PascalCase (GetPosition), Lua uses snake_case (uz.scene.get_position). Both languages must have access to the same capabilities. Lua API files are organized by domain: api/scene/ (entities, transforms, lights), api/core/ (camera, input, time, math), api/renderer/ (render path, debug modes). Registration via LuaRegister*API() called from LuaVM::Init().

## Example

// C++ implementation:\nvoid CameraSystem::SetPosition(const Vec3& pos);\n\n// Lua binding (must exist):\nuz.camera.set_position(x, y, z)\n\n// Checklist for every new feature:\n// ☐ C++ implementation complete and tested\n// ☐ Lua binding in correct api/ subdirectory\n// ☐ Registration in LuaVM::Init()\n// ☐ Lua unit tests written\n// ☐ API documented in docs/lua-api-reference.md

## When to Use

After implementing any new C++ system or feature. When adding new ECS components. When exposing debug/editor functionality to scripts.

## When NOT to Use

For internal engine implementation details that should never be script-accessible. For performance-critical tight loops that would be slower in Lua.
