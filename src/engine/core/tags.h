#pragma once

// tags.h — Tag and group system for entities.
//
// Tags are lightweight string labels attached to entity IDs.
// Groups are named sets of entity IDs, allowing batch queries.
// Both use StringId for O(1) comparison after initial hashing.
//
// Storage is flat-array, zero-heap. TagRegistry supports up to kMaxEntities
// entities with up to kMaxTagsPerEntity tags each. GroupRegistry supports up
// to kMaxGroups named groups with up to kMaxEntitiesPerGroup members each.

#include "engine/core/string_id.h"
#include "engine/scene/scene_node.h"

#include <cstdint>
#include <functional>

namespace uz {

// ============================================================================
// TagRegistry — attach/query named tags on entities
// ============================================================================

class TagRegistry {
public:
    static constexpr uint32_t kMaxEntities      = 4096;
    static constexpr uint32_t kMaxTagsPerEntity  = 16;
    static constexpr uint32_t kMaxUniqueTags     = 256;

    TagRegistry();

    /// Add a tag to an entity. Returns false if entity already has
    /// kMaxTagsPerEntity tags or the registry is full and the entity is new.
    [[nodiscard]] bool add_tag(NodeId entity, StringId tag);

    /// Remove a tag from an entity. No-op if entity doesn't have it.
    void remove_tag(NodeId entity, StringId tag);

    /// Check if entity has a specific tag.
    [[nodiscard]] bool has_tag(NodeId entity, StringId tag) const;

    /// Number of tags currently attached to this entity. Zero if unknown.
    [[nodiscard]] uint32_t tag_count(NodeId entity) const;

    /// Iterate entities that have ALL of the given tags.
    /// Calls fn(entity) for each matching entity.
    /// If tag_count is 0, no entities are visited (vacuous truth excluded
    /// to avoid surprising "match everything" behavior).
    using VisitFn = std::function<void(NodeId)>;
    void query_all(const StringId* tags, uint32_t tag_count, VisitFn fn) const;

    /// Convenience: query entities with a single tag.
    void query(StringId tag, VisitFn fn) const;

    /// Remove all tags from an entity and free its slot.
    void clear_entity(NodeId entity);

    /// Remove all tags from all entities, resetting the registry.
    void clear_all();

    /// Total number of entities that have at least one tag.
    [[nodiscard]] uint32_t entity_count() const;

private:
    struct EntityTags {
        NodeId   entity;
        StringId tags[kMaxTagsPerEntity];
        uint32_t count;
        bool     alive;
    };

    EntityTags entries_[kMaxEntities];
    uint32_t   count_ = 0;

    EntityTags*       find_or_create(NodeId entity);
    const EntityTags* find(NodeId entity) const;
    EntityTags*       find(NodeId entity);
};

// ============================================================================
// GroupRegistry — named collections of entities
// ============================================================================

class GroupRegistry {
public:
    static constexpr uint32_t kMaxGroups           = 128;
    static constexpr uint32_t kMaxEntitiesPerGroup = 512;

    GroupRegistry();

    /// Create a named group. Returns false if name already exists or max reached.
    [[nodiscard]] bool create_group(StringId name);

    /// Delete a group. No-op if not found.
    void delete_group(StringId name);

    /// Add entity to group. Returns false if group not found or group is full.
    /// Duplicate adds are silently ignored (returns true).
    [[nodiscard]] bool add_to_group(StringId group, NodeId entity);

    /// Remove entity from group. No-op if not found.
    void remove_from_group(StringId group, NodeId entity);

    /// Check if entity is in group.
    [[nodiscard]] bool in_group(StringId group, NodeId entity) const;

    /// Check if group exists.
    [[nodiscard]] bool has_group(StringId name) const;

    /// Number of entities in a group. Returns 0 if group not found.
    [[nodiscard]] uint32_t group_size(StringId name) const;

    /// Iterate entities in a group. No-op if group not found.
    using VisitFn = std::function<void(NodeId)>;
    void for_each(StringId group, VisitFn fn) const;

    /// Remove entity from ALL groups.
    void remove_entity(NodeId entity);

    /// Remove all entities from all groups (keep group definitions).
    void clear_entities();

    /// Delete all groups.
    void clear_all();

    /// Number of defined groups.
    [[nodiscard]] uint32_t group_count() const;

private:
    struct Group {
        StringId name;
        NodeId   entities[kMaxEntitiesPerGroup];
        uint32_t count;
        bool     alive;
    };

    Group    groups_[kMaxGroups];
    uint32_t group_count_ = 0;

    Group*       find_group(StringId name);
    const Group* find_group(StringId name) const;
};

} // namespace uz
