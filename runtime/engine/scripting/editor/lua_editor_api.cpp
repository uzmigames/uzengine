#include "lua_editor_api.h"

#include "log.h"
#include "selection_manager.h"
#include "editor_input_handler.h"
#include "entity.h"

/// Lua bindings for editor APIs (selection, input handling, node management).

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <cstdint>
#include <cstring>

static_assert(sizeof(void*) >= 8,
    "uz::Entity requires 64-bit pointers for lightuserdata encoding");

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry keys for editor state pointers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kSelectionRegistryKey = "uz_selection_manager";
static constexpr const char* kEditorInputRegistryKey = "uz_editor_input_handler";

// ─────────────────────────────────────────────────────────────────────────────
// Entity <-> lightuserdata encoding (same layout as lua_scene_api.cpp)
// ─────────────────────────────────────────────────────────────────────────────

/// Packs an Entity into a void* for use as Lua lightuserdata.
/// High 32 bits = entity.id, low 32 bits = entity.generation.
[[nodiscard]] static void* EntityToUserdata(Entity e) noexcept {
    const uint64_t packed = (static_cast<uint64_t>(e.id) << 32)
                          | static_cast<uint64_t>(e.generation);
    void* p;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&p, &packed, sizeof(p));
    return p;
}

/// Unpacks an Entity from a void* lightuserdata.
[[nodiscard]] static Entity UserdataToEntity(void* p) noexcept {
    uint64_t packed;
    static_assert(sizeof(packed) == sizeof(p));
    std::memcpy(&packed, &p, sizeof(packed));
    Entity e;
    e.id         = static_cast<uint32_t>(packed >> 32);
    e.generation = static_cast<uint32_t>(packed & 0xFFFFFFFFu);
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — retrieve editor state from Lua registry
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] static SelectionManager* GetSelection(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kSelectionRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SelectionManager*>(p);
}

[[nodiscard]] static EditorInputHandler* GetEditorInput(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kEditorInputRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<EditorInputHandler*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.1: uz.editor.get_selected() → entity lightuserdata or nil
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the currently selected entity as lightuserdata, or nil if nothing
/// is selected or the SelectionManager is not wired.
static int EditorGetSelected(lua_State* L) {
    SelectionManager* sel = GetSelection(L);
    if (!sel) {
        lua_pushnil(L);
        return 1;
    }

    const Entity selected = sel->GetSelected();
    if (!selected.IsValid()) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushlightuserdata(L, EntityToUserdata(selected));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.2: uz.editor.select(entity) and uz.editor.deselect()
// ─────────────────────────────────────────────────────────────────────────────

/// Selects the given entity (lightuserdata). Passing nil or no argument deselects.
static int EditorSelect(lua_State* L) {
    SelectionManager* sel = GetSelection(L);
    if (!sel) {
        UZ_LOG(Scripting, Warning, "uz.editor.select: SelectionManager not set");
        return 0;
    }

    if (lua_isnil(L, 1) || lua_isnone(L, 1)) {
        sel->Deselect();
        return 0;
    }

    if (!lua_islightuserdata(L, 1)) {
        UZ_LOG(Scripting, Warning,
            "uz.editor.select: argument 1 must be an entity (lightuserdata), got {}",
            lua_typename(L, lua_type(L, 1)));
        return 0;
    }

    const Entity entity = UserdataToEntity(lua_touserdata(L, 1));
    sel->Select(entity);
    return 0;
}

/// Clears the current entity selection.
static int EditorDeselect(lua_State* L) {
    SelectionManager* sel = GetSelection(L);
    if (!sel) {
        UZ_LOG(Scripting, Warning, "uz.editor.deselect: SelectionManager not set");
        return 0;
    }
    sel->Deselect();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.3: uz.editor.set_gizmo_mode(mode) — "translate"/"rotate"/"scale"
//      uz.editor.get_gizmo_mode() → string
// ─────────────────────────────────────────────────────────────────────────────

/// Sets the gizmo manipulation mode from a string.
/// Valid values: "translate", "rotate", "scale".
static int EditorSetGizmoMode(lua_State* L) {
    EditorInputHandler* input = GetEditorInput(L);
    if (!input) {
        UZ_LOG(Scripting, Warning, "uz.editor.set_gizmo_mode: EditorInputHandler not set");
        return 0;
    }

    const char* modeStr = luaL_checkstring(L, 1);

    if (std::strcmp(modeStr, "translate") == 0) {
        input->SetGizmoMode(GizmoMode::Translate);
    } else if (std::strcmp(modeStr, "rotate") == 0) {
        input->SetGizmoMode(GizmoMode::Rotate);
    } else if (std::strcmp(modeStr, "scale") == 0) {
        input->SetGizmoMode(GizmoMode::Scale);
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.editor.set_gizmo_mode: invalid mode '{}' — expected 'translate', 'rotate', or 'scale'",
            modeStr);
    }

    return 0;
}

/// Returns the current gizmo mode as a string.
static int EditorGetGizmoMode(lua_State* L) {
    EditorInputHandler* input = GetEditorInput(L);
    if (!input) {
        lua_pushnil(L);
        return 1;
    }

    switch (input->GetGizmoMode()) {
        case GizmoMode::Translate: lua_pushstring(L, "translate"); break;
        case GizmoMode::Rotate:    lua_pushstring(L, "rotate");    break;
        case GizmoMode::Scale:     lua_pushstring(L, "scale");     break;
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.4: uz.editor.set_gizmo_space(space) — "world"/"local"
//      uz.editor.get_gizmo_space() → string
// ─────────────────────────────────────────────────────────────────────────────

/// Sets the gizmo coordinate space from a string.
/// Valid values: "world", "local".
static int EditorSetGizmoSpace(lua_State* L) {
    EditorInputHandler* input = GetEditorInput(L);
    if (!input) {
        UZ_LOG(Scripting, Warning, "uz.editor.set_gizmo_space: EditorInputHandler not set");
        return 0;
    }

    const char* spaceStr = luaL_checkstring(L, 1);

    if (std::strcmp(spaceStr, "world") == 0) {
        input->SetGizmoSpace(GizmoSpace::World);
    } else if (std::strcmp(spaceStr, "local") == 0) {
        input->SetGizmoSpace(GizmoSpace::Local);
    } else {
        UZ_LOG(Scripting, Warning,
            "uz.editor.set_gizmo_space: invalid space '{}' — expected 'world' or 'local'",
            spaceStr);
    }

    return 0;
}

/// Returns the current gizmo coordinate space as a string.
static int EditorGetGizmoSpace(lua_State* L) {
    EditorInputHandler* input = GetEditorInput(L);
    if (!input) {
        lua_pushnil(L);
        return 1;
    }

    switch (input->GetGizmoSpace()) {
        case GizmoSpace::World: lua_pushstring(L, "world"); break;
        case GizmoSpace::Local: lua_pushstring(L, "local"); break;
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterEditorAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterEditorAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    // Create uz.editor = {}
    lua_newtable(L);

    lua_pushcfunction(L, EditorGetSelected);   lua_setfield(L, -2, "get_selected");
    lua_pushcfunction(L, EditorSelect);        lua_setfield(L, -2, "select");
    lua_pushcfunction(L, EditorDeselect);      lua_setfield(L, -2, "deselect");
    lua_pushcfunction(L, EditorSetGizmoMode);  lua_setfield(L, -2, "set_gizmo_mode");
    lua_pushcfunction(L, EditorGetGizmoMode);  lua_setfield(L, -2, "get_gizmo_mode");
    lua_pushcfunction(L, EditorSetGizmoSpace); lua_setfield(L, -2, "set_gizmo_space");
    lua_pushcfunction(L, EditorGetGizmoSpace); lua_setfield(L, -2, "get_gizmo_space");

    lua_setfield(L, -2, "editor"); // uz.editor = table

    lua_pop(L, 1); // pop uz table

    UZ_LOG(Scripting, Info, "uz.editor API registered");
}

void LuaSetEditorState(lua_State* L,
                       SelectionManager* selection,
                       EditorInputHandler* editorInput) {
    if (!L) return;

    lua_pushlightuserdata(L, static_cast<void*>(selection));
    lua_setfield(L, LUA_REGISTRYINDEX, kSelectionRegistryKey);

    lua_pushlightuserdata(L, static_cast<void*>(editorInput));
    lua_setfield(L, LUA_REGISTRYINDEX, kEditorInputRegistryKey);

    UZ_LOG(Scripting, Info, "Editor state wired to Lua (selection={}, editorInput={})",
           static_cast<void*>(selection), static_cast<void*>(editorInput));
}

} // namespace uz
