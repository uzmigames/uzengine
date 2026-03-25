#pragma once

/// lua_editor_api — Lua bindings for editor selection and gizmo controls.
///
/// Creates the `uz.editor` sub-table with functions for querying/setting
/// the selected entity, gizmo manipulation mode, and gizmo coordinate space.
///
/// Must be called AFTER `RegisterUzTable()` has created the `uz` global table.
///
/// # New Functions (uz.editor)
///
/// - `uz.editor.get_selected()` → entity lightuserdata or nil
/// - `uz.editor.select(entity)` → void
/// - `uz.editor.deselect()` → void
/// - `uz.editor.set_gizmo_mode(mode)` → void ("translate"/"rotate"/"scale")
/// - `uz.editor.set_gizmo_space(space)` → void ("world"/"local")
/// - `uz.editor.get_gizmo_mode()` → string ("translate"/"rotate"/"scale")
/// - `uz.editor.get_gizmo_space()` → string ("world"/"local")

struct lua_State;

namespace uz {

class SelectionManager;
class EditorInputHandler;

/// Registers editor functions into a new `uz.editor` table.
///
/// Must be called AFTER `RegisterUzTable()` has created the `uz` global table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterEditorAPI(lua_State* L);

/// Stores pointers to the SelectionManager and EditorInputHandler in the Lua
/// registry so that editor API functions can access them.
///
/// Must be called after LuaVM::Init() and after the editor systems are created.
/// Both pointers are non-owning — the caller must ensure they outlive the Lua state.
///
/// Passing nullptr for either pointer disables the corresponding API functions
/// (they will return nil / be no-ops).
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
/// * `selection` — SelectionManager for get/set selection. May be null.
/// * `editorInput` — EditorInputHandler for gizmo mode/space. May be null.
void LuaSetEditorState(lua_State* L,
                       SelectionManager* selection,
                       EditorInputHandler* editorInput);

} // namespace uz
