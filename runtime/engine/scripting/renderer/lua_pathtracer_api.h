#pragma once

struct lua_State;

namespace uz {

/// Registers `uz.pathtracer` into the existing `uz` global table.
/// Controls use the CVar registry; sample queries use the SceneRenderer pointer
/// injected by LuaSetSceneRenderer. Call from the main/render thread.
void LuaRegisterPathTracerAPI(lua_State* L);

} // namespace uz
