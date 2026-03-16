# Lua API Parity

Every C++ feature MUST also be exposed to Lua. No exceptions.

## Checklist (apply to every new feature)
- [ ] C++ implementation complete and tested
- [ ] Lua binding in correct `engine/scripting/api/` subdirectory
- [ ] Registration function added to `LuaVM::Init()`
- [ ] Lua unit tests written
- [ ] Lua API documented

## Rules
- NEVER mark a C++ feature as complete without its Lua binding
- NEVER create Lua-only features — C++ first, then bind
- API naming: Lua = `snake_case`, C++ = `PascalCase`
- Delegate Lua binding work to `lua-scripting-engineer` (sonnet)
