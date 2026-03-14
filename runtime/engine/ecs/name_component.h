#pragma once

/// CName — entity name tag component.
///
/// A lightweight string label attached to an entity for human-readable
/// identification. Used by the Lua scripting API (`uz.scene.find(name)`)
/// to locate entities by name at runtime without maintaining a separate
/// name-to-entity registry.
///
/// # Naming Convention
/// Follows the C-prefix convention for ECS components (CName, CTransform, etc.).
/// Components are plain data structs with no virtual functions or side-effect
/// constructors, making them safe for `entt::registry` storage.
///
/// # Uniqueness
/// The engine does NOT enforce name uniqueness. `uz.scene.find(name)` returns
/// the first matching entity. If multiple entities share a name, which one is
/// returned is unspecified (depends on EnTT iteration order).
///
/// # Example
/// ```cpp
/// auto player = world.CreateEntity();
/// world.AddComponent<CName>(player, CName{"Player"});
///
/// // From Lua:
/// // local e = uz.scene.find("Player")
/// ```

#include <string>

namespace uz {

/// Name tag component for entity identification in the scene and Lua scripting.
///
/// Attach to any entity that needs to be addressable by name. The string is
/// stored by value — short-string optimization applies for names under ~15 chars.
///
/// # Thread Safety
/// Not thread-safe. Access only from the main thread via the World API.
struct CName {
    /// Human-readable identifier for the entity.
    ///
    /// No uniqueness is enforced. Empty string is valid but not queryable
    /// by `uz.scene.find()` (an empty search string returns nil).
    std::string name;
};

} // namespace uz
