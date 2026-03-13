#pragma once

/// UzEngine Property Serializer
///
/// Converts reflected component instances to/from JSON using their `ClassInfo` metadata.
/// Supports all `PropertyType` variants except `Entity` (serialized as id+generation pair).
/// Properties marked with `PropertyFlags::Transient` are skipped during serialization.
///
/// # JSON Format
/// Each property is serialized as a key-value pair where the key is the property name.
/// Scalar types map directly to JSON primitives. Vector/quaternion types serialize as
/// JSON arrays: `[x, y]`, `[x, y, z]`, `[x, y, z, w]`.
///
/// # Example
/// ```json
/// {
///   "speed": 2.5,
///   "health": 100,
///   "target": [10.0, 0.0, 5.0],
///   "name": "Guard"
/// }
/// ```

#include "class_info.h"

#include <json.hpp>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PropertySerializer — JSON serialization for reflected properties
// ─────────────────────────────────────────────────────────────────────────────

/// Serializes and deserializes reflected component instances to/from JSON.
///
/// All methods are static — no instance state. Uses `ClassInfo` property metadata
/// and raw byte offsets to read/write fields without knowing the concrete type.
///
/// # Thread Safety
/// Thread-safe for concurrent serialization of different instances. Not safe to
/// serialize the same instance from multiple threads without external synchronization.
class PropertySerializer {
public:
    /// Serializes all non-transient properties of an instance to a JSON object.
    ///
    /// # Arguments
    /// * `instance` - Pointer to the component/struct instance. Must not be null.
    /// * `info` - ClassInfo describing the instance's reflected properties.
    ///
    /// # Returns
    /// JSON object with property names as keys and serialized values. Properties
    /// with `PropertyFlags::Transient` are excluded. Properties with unknown types
    /// are skipped with a warning log.
    [[nodiscard]] static nlohmann::json SerializeToJSON(const void* instance, const ClassInfo& info);

    /// Deserializes a JSON object into an existing instance, overwriting matching fields.
    ///
    /// Missing JSON keys are silently skipped (the instance retains its current/default value).
    /// Type mismatches between JSON and the property's `PropertyType` are logged as warnings
    /// and the field is left unchanged.
    ///
    /// # Arguments
    /// * `instance` - Pointer to the component/struct instance. Must not be null.
    /// * `info` - ClassInfo describing the instance's reflected properties.
    /// * `data` - JSON object with property values. Must be a JSON object (not array/scalar).
    ///
    /// # Returns
    /// `true` if all present fields were deserialized successfully.
    /// `false` if any field had a type mismatch or deserialization error (partial results
    /// are still applied for fields that succeeded).
    static bool DeserializeFromJSON(void* instance, const ClassInfo& info,
                                    const nlohmann::json& data);

private:
    /// Serializes a single property value to JSON.
    [[nodiscard]] static nlohmann::json PropertyToJSON(const void* instance,
                                                       const PropertyInfo& prop);

    /// Deserializes a single property value from JSON into the instance.
    ///
    /// # Returns
    /// `true` on success, `false` on type mismatch or unsupported type.
    static bool PropertyFromJSON(void* instance, const PropertyInfo& prop,
                                 const nlohmann::json& value);
};

} // namespace uz
