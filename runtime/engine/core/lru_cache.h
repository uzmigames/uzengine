#pragma once

#include <cstddef>
#include <list>
#include <unordered_map>
#include <utility>

namespace uz {

/// Generic LRU cache with a configurable memory budget.
///
/// Provides O(1) lookup, insertion, and eviction using a doubly-linked list
/// ordered by recency (front = most recent) and a hash map for direct access.
///
/// @tparam Key   Hashable key type (must support std::hash and operator==).
/// @tparam Value Cached value type (must be move-constructible).
template <typename Key, typename Value>
class LRUCache {
public:
    /// Constructs a cache with the given memory budget in bytes.
    explicit LRUCache(size_t maxBytes) noexcept
        : maxBytes_(maxBytes) {}

    ~LRUCache() = default;
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
    LRUCache(LRUCache&&) noexcept = default;
    LRUCache& operator=(LRUCache&&) noexcept = default;

    /// Looks up a cached value by key. Returns nullptr on miss.
    /// On hit, promotes the entry to most-recently-used.
    [[nodiscard]] Value* Get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        // Splice to front (most recent) without invalidating the iterator.
        order_.splice(order_.begin(), order_, it->second);
        return &it->second->second.value;
    }

    /// Inserts or updates a cache entry. Evicts least-recently-used entries
    /// until the total size fits within the memory budget.
    ///
    /// If `sizeBytes` alone exceeds `maxBytes`, the entry is still inserted
    /// but all other entries are evicted first.
    void Put(const Key& key, Value value, size_t sizeBytes) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            // Key exists — update value and size, promote to front.
            currentBytes_ -= it->second->second.sizeBytes;
            it->second->second.value = std::move(value);
            it->second->second.sizeBytes = sizeBytes;
            currentBytes_ += sizeBytes;
            order_.splice(order_.begin(), order_, it->second);
        } else {
            // New key — push to front, add to index.
            order_.emplace_front(key, Entry{std::move(value), sizeBytes});
            index_.emplace(key, order_.begin());
            currentBytes_ += sizeBytes;
        }

        // Evict from back (least recently used) until within budget.
        while (currentBytes_ > maxBytes_ && order_.size() > 1) {
            evictBack();
        }
    }

    /// Removes all entries and resets memory usage to zero.
    void Clear() noexcept {
        order_.clear();
        index_.clear();
        currentBytes_ = 0;
    }

    /// Returns the current memory usage in bytes (sum of all entry sizes).
    [[nodiscard]] size_t CurrentBytes() const noexcept { return currentBytes_; }

    /// Returns the configured memory budget in bytes.
    [[nodiscard]] size_t MaxBytes() const noexcept { return maxBytes_; }

    /// Returns the number of entries currently in the cache.
    [[nodiscard]] size_t EntryCount() const noexcept { return index_.size(); }

    /// Returns true if the cache contains an entry for the given key.
    /// Does NOT promote the entry (const operation).
    [[nodiscard]] bool Contains(const Key& key) const {
        return index_.find(key) != index_.end();
    }

private:
    struct Entry {
        Value value;
        size_t sizeBytes;
    };

    using ListType = std::list<std::pair<Key, Entry>>;
    using MapType = std::unordered_map<Key, typename ListType::iterator>;

    void evictBack() {
        auto& back = order_.back();
        currentBytes_ -= back.second.sizeBytes;
        index_.erase(back.first);
        order_.pop_back();
    }

    ListType order_;       // Front = most recent, back = least recent.
    MapType index_;        // Key -> iterator into order_.
    size_t maxBytes_ = 0;
    size_t currentBytes_ = 0;
};

} // namespace uz
