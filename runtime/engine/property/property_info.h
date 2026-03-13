#pragma once

/// UzEngine Property Info
///
/// Describes a single reflected property: its name, type, offset within the owning
/// struct, byte size, documentation, editor category, and behavioral flags.
///
/// `PropertyInfo` is a lightweight POD-like descriptor. It does not own memory —
/// all string pointers (`name`, `description`, `category`) must point to string
/// literals or storage with static lifetime.

#include "property_types.h"

#include <cstddef>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PropertyInfo — metadata for a single reflected field
// ─────────────────────────────────────────────────────────────────────────────

/// Metadata descriptor for one reflected property of a class.
///
/// Created by `ClassInfo::AddProperty<T>()` or the `UZ_PROPERTIES_BEGIN` macro block.
/// All string pointers must have static lifetime (typically string literals).
///
/// # Example
/// ```cpp
/// PropertyInfo info;
/// info.name = "speed";
/// info.type = PropertyType::Float;
/// info.offset = offsetof(Enemy, speed);
/// info.size = sizeof(float);
/// info.description = "Movement speed in units/sec";
/// info.category = "Movement";
/// info.flags = PropertyFlags::None;
/// ```
struct PropertyInfo {
    const char* name = "";             ///< Field name (must be a string literal).
    PropertyType type = PropertyType::None; ///< Reflected type of this property.
    size_t offset = 0;                 ///< Byte offset from start of owning struct.
    size_t size = 0;                   ///< Size in bytes of the property's C++ type.
    const char* description = "";      ///< Human-readable description for editors/docs.
    const char* category = "";         ///< Editor grouping category (e.g., "Physics").
    PropertyFlags flags = PropertyFlags::None; ///< Behavioral flags.
};

} // namespace uz
