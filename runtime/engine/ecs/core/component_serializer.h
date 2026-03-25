/// Component serialization registry.
///
/// Components self-register by defining static Serialize/Deserialize lambdas
/// and calling UZ_REGISTER_COMPONENT_SERIALIZER at file scope in a .cpp file.
/// The scene loader queries ComponentSerializerRegistry::Get() to dynamically
/// create and deserialize any registered component type by string key.
///
/// This eliminates hardcoded `else if` chains in scene_loader.cpp for new
/// component types. Existing hardcoded parsers (transform, camera, mesh,
/// light, script, material) remain unchanged — the registry is a fallback
/// for component types not handled by the built-in parsers.
///
/// # Thread Safety
/// Registration happens during static initialization (before main).
/// After main begins, the registry is read-only and safe for concurrent reads.

#pragma once

#include "entity.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace uz {

class World;

/// Type-erased component factory + serializer.
///
/// Each registered component type provides two callbacks: one to create the
/// component from JSON data, and one to write the component back to JSON.
/// The `void*` parameters are typed as `const nlohmann::json*` and
/// `nlohmann::json*` respectively — the type erasure avoids pulling
/// `<json.hpp>` into this header.
struct ComponentSerializerEntry {
    /// Creates the component on the entity and deserializes JSON into it.
    /// The void* parameter is a `const nlohmann::json*`.
    std::function<void(World& world, Entity entity, const void* json)> deserialize;

    /// Serializes the component to JSON. Returns false if the entity
    /// doesn't have this component.
    /// The void* parameter is a `nlohmann::json*`.
    std::function<bool(const World& world, Entity entity, void* json)> serialize;
};

/// Singleton registry mapping component type name strings to their serializers.
///
/// Populated during static initialization via the UZ_REGISTER_COMPONENT_SERIALIZER
/// macro. After main begins, the registry is effectively immutable.
class ComponentSerializerRegistry {
public:
    /// Returns the singleton instance.
    static ComponentSerializerRegistry& Get() {
        static ComponentSerializerRegistry instance;
        return instance;
    }

    /// Registers a component serializer entry under the given key.
    ///
    /// Called during static initialization. If a key is registered twice,
    /// the second registration silently overwrites the first.
    void Register(const std::string& key, ComponentSerializerEntry entry) {
        m_entries[key] = std::move(entry);
    }

    /// Attempts to deserialize a component from JSON.
    ///
    /// # Returns
    /// `true` if the key was found in the registry (component was deserialized).
    /// `false` if the key is not registered.
    [[nodiscard]] bool Deserialize(const std::string& key,
                                    World& world,
                                    Entity entity,
                                    const void* json) const {
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) return false;
        it->second.deserialize(world, entity, json);
        return true;
    }

    /// Attempts to serialize a component to JSON.
    ///
    /// # Returns
    /// `true` if the key was found AND the entity has the component.
    /// `false` if the key is not registered or the entity lacks the component.
    [[nodiscard]] bool Serialize(const std::string& key,
                                  const World& world,
                                  Entity entity,
                                  void* json) const {
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) return false;
        return it->second.serialize(world, entity, json);
    }

    /// Returns all registered entries (for iteration during scene save).
    [[nodiscard]] const std::unordered_map<std::string, ComponentSerializerEntry>&
    GetEntries() const {
        return m_entries;
    }

    /// Checks whether a key is registered.
    [[nodiscard]] bool Has(const std::string& key) const {
        return m_entries.find(key) != m_entries.end();
    }

private:
    ComponentSerializerRegistry() = default;
    std::unordered_map<std::string, ComponentSerializerEntry> m_entries;
};

} // namespace uz

/// Auto-registration macro. Place at file scope in a .cpp file.
///
/// Creates a static struct whose constructor registers the serializer entry
/// during static initialization (before main). The `key` parameter becomes
/// the JSON component name string (e.g., "sky_atmosphere").
///
/// # Parameters
/// - `key`: Identifier used as both the C++ struct suffix and the JSON string key.
/// - `Type`: The component type (for documentation; not used in the macro body).
/// - `deserializeLambda`: Lambda with signature
///   `(uz::World& world, uz::Entity entity, const void* jsonPtr) -> void`.
/// - `serializeLambda`: Lambda with signature
///   `(const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool`.
///
/// # Example
/// ```cpp
/// UZ_REGISTER_COMPONENT_SERIALIZER(sky_atmosphere, SkyAtmosphereComponent,
///     [](uz::World& world, uz::Entity entity, const void* jsonPtr) {
///         const auto& j = *static_cast<const nlohmann::json*>(jsonPtr);
///         auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
///         auto& c = world.GetRegistry().emplace<uz::SkyAtmosphereComponent>(entt);
///         // read fields from j...
///     },
///     [](const uz::World& world, uz::Entity entity, void* jsonPtr) -> bool {
///         auto entt = static_cast<entt::entity>(static_cast<uint32_t>(entity.id));
///         const auto* c = world.GetRegistry().try_get<uz::SkyAtmosphereComponent>(entt);
///         if (!c) return false;
///         auto& j = *static_cast<nlohmann::json*>(jsonPtr);
///         // write fields to j...
///         return true;
///     }
/// )
/// ```
#define UZ_REGISTER_COMPONENT_SERIALIZER(key, Type, deserializeLambda, serializeLambda) \
    static struct UzCompReg_##key {                                                      \
        UzCompReg_##key() {                                                              \
            uz::ComponentSerializerRegistry::Get().Register(#key, {                      \
                deserializeLambda,                                                        \
                serializeLambda                                                           \
            });                                                                           \
        }                                                                                 \
    } s_uzCompReg_##key
