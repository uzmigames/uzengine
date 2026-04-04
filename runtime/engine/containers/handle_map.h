#pragma once

/// UzEngine HandleMap — Generational handle-based allocator.
///
/// # Design
/// - **Handles**: Typed references (Handle<Entity>) that include generation for safe dereferencing
/// - **Use-after-free prevention**: Incrementing generation invalidates old handles on reuse
/// - **O(1) access**: Index lookup into sparse array
/// - **Sparse storage**: Only alive objects occupy space (via FreeList pattern)
///
/// # Problem Solved
/// Pointers become invalid when memory reallocates (std::vector).
/// Indices alone don't detect reuse (allocate slot N, free, reallocate N).
/// Handles combine both: (index, generation) so Handle(N, gen=5) != Handle(N, gen=6).
///
/// # Inspiration
/// Generational index pattern — common in game engines for safe resource handles.
///
/// # Example
/// ```cpp
/// HandleMap<Entity, 1000> entities;
/// Handle<Entity> h = entities.Create(name, pos);
/// Entity* e = entities.Get(h);  // safe: validates generation
/// entities.Destroy(h);
/// e = entities.Get(h);  // nullptr: handle stale
/// ```

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

namespace uz {

/// Generational handle — typed, safe reference to a pooled object.
///
/// # Structure
/// - `index`: Slot in the HandleMap (uint32_t, 32-bit)
/// - `generation`: Version counter for safe reuse (uint32_t, 32-bit)
///
/// # Validity
/// A handle is valid (IsValid()) only if generation != 0.
/// After Destroy(), the handle's generation is never reused in this session,
/// so the stale handle is guaranteed not to match new allocations.
///
/// # Comparison
/// Handles are equal only if both index AND generation match.
/// ```cpp
/// Handle<Entity> h1 = map.Create(...);  // {0, 1}
/// map.Destroy(h1);                      // generation becomes unreachable
/// Handle<Entity> h2 = map.Create(...);  // {0, 2}
/// assert(h1 != h2);  // true: same slot, different generation
/// ```
template <typename T> struct Handle {
    uint32_t index = 0;      ///< Slot index in pool
    uint32_t generation = 0; ///< Version counter (0 = invalid)

    /// Returns true if this handle was ever created (generation != 0).
    bool IsValid() const { return generation != 0; }

    /// Equality: both index and generation must match.
    bool operator==(Handle other) const {
        return index == other.index && generation == other.generation;
    }

    /// Inequality: either index or generation differs.
    bool operator!=(Handle other) const { return !(*this == other); }

    /// Creates an invalid handle (generation=0).
    /// Use to signal "not found" or initialization.
    static Handle Invalid() { return {0, 0}; }
};

// ── HandleMap — sparse generational array ────────────────────────────────────

/// Generational handle-based sparse array with O(1) access.
///
/// Combines a dense storage array with a generation counter per slot so that
/// stale handles (use-after-free) are detected at lookup time rather than
/// silently succeeding. A singly-linked free-list chains unused slots so
/// both Create() and Destroy() are O(1) with no searching.
///
/// # Template Parameters
/// * `T` - Element type to store (must be destructible; moved-from state not required)
/// * `Capacity` - Maximum number of elements (stack-allocated, fixed at compile time)
///
/// # Memory Layout
/// Total size ≈ `Capacity * (sizeof(T) + sizeof(uint32_t)*2 + sizeof(bool)) + 12` bytes.
/// All arrays are contiguous — iteration is cache-friendly for alive checks.
///
/// # Free-List Invariant
/// `m_freeHead` points to the first available slot. Each free slot's `m_freeNext`
/// entry points to the next free slot, forming a singly-linked chain terminated
/// by `kInvalid` (0xFFFFFFFF). When `m_freeHead == kInvalid`, the pool is full.
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from a single thread, or the
/// caller must provide external synchronization (mutex). No atomic operations
/// are used internally.
///
/// # Examples
/// ```cpp
/// HandleMap<Enemy, 256> enemies;
/// Handle<Enemy> h = enemies.Create("goblin", 100);
/// Enemy* e = enemies.Get(h);       // valid pointer
/// enemies.Destroy(h);
/// assert(enemies.Get(h) == nullptr); // stale handle detected
///
/// // Iteration:
/// enemies.ForEach([](Handle<Enemy> h, Enemy& e) {
///     e.Update();
/// });
/// ```
template <typename T, size_t Capacity> class HandleMap {
    static_assert(Capacity > 0, "Capacity must be > 0");

  public:
    /// Initializes empty map with all slots in the free chain.
    HandleMap() {
        for (size_t i = 0; i < Capacity - 1; ++i) {
            m_freeNext[i] = static_cast<uint32_t>(i + 1);
        }
        m_freeNext[Capacity - 1] = kInvalid;
        m_freeHead = 0;
        m_count = 0;
    }

    /// Destroys all live objects in the map.
    ~HandleMap() {
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_alive[i]) {
                reinterpret_cast<T&>(m_storage[i]).~T();
            }
        }
    }

    HandleMap(const HandleMap&) = delete;
    HandleMap& operator=(const HandleMap&) = delete;

    /// Creates a new element in-place and returns a generational handle to it.
    ///
    /// Pops the head of the free-list, increments that slot's generation,
    /// placement-constructs T with the forwarded arguments, and returns a
    /// handle encoding (index, new_generation).
    ///
    /// # Arguments
    /// * `args` - Forwarded to T's constructor via perfect forwarding.
    ///
    /// # Returns
    /// Valid handle on success. `Handle::Invalid()` (generation=0) if every
    /// slot is occupied (m_freeHead == kInvalid).
    ///
    /// # Panics
    /// None — returns Invalid on full pool rather than asserting.
    template <typename... Args> Handle<T> Create(Args&&... args) {
        if (m_freeHead == kInvalid)
            return Handle<T>::Invalid();
        uint32_t idx = m_freeHead;
        m_freeHead = m_freeNext[idx];
        m_generation[idx]++;
        new (&m_storage[idx]) T(static_cast<Args&&>(args)...);
        m_alive[idx] = true;
        ++m_count;
        return {idx, m_generation[idx]};
    }

    /// Destroys the element referenced by handle and returns its slot to the free-list.
    ///
    /// Calls T's destructor, marks the slot dead, and pushes it onto the
    /// free-list head. The slot's generation was already incremented on Create,
    /// so the old handle will fail future IsValidHandle() checks.
    ///
    /// # Arguments
    /// * `h` - Handle previously returned by Create(). Stale or invalid handles
    ///         are silently ignored (no-op).
    ///
    /// # Panics
    /// None — silently ignores invalid handles.
    void Destroy(Handle<T> h) {
        if (!IsValidHandle(h))
            return;
        reinterpret_cast<T&>(m_storage[h.index]).~T();
        m_alive[h.index] = false;
        m_freeNext[h.index] = m_freeHead;
        m_freeHead = h.index;
        --m_count;
    }

    /// Returns mutable pointer to the element, or nullptr if the handle is stale.
    ///
    /// Validates index bounds, alive flag, AND generation match before
    /// returning a pointer. All three must pass — this is the core
    /// use-after-free protection.
    ///
    /// # Arguments
    /// * `h` - Handle to look up. May be stale or invalid.
    ///
    /// # Returns
    /// Non-null pointer to the live T, or nullptr if any validation fails.
    T* Get(Handle<T> h) {
        if (!IsValidHandle(h))
            return nullptr;
        return &reinterpret_cast<T&>(m_storage[h.index]);
    }

    /// Returns const pointer to element, or nullptr if handle is stale/invalid.
    const T* Get(Handle<T> h) const {
        if (!IsValidHandle(h))
            return nullptr;
        return &reinterpret_cast<const T&>(m_storage[h.index]);
    }

    /// Returns true if the handle refers to a currently alive element.
    ///
    /// Checks three conditions: index is in bounds, slot is alive, and the
    /// stored generation matches the handle's generation. All three must hold.
    ///
    /// # Arguments
    /// * `h` - Handle to validate. Safe to call with any value including Invalid().
    bool IsValidHandle(Handle<T> h) const {
        return h.index < Capacity && m_alive[h.index] && m_generation[h.index] == h.generation;
    }

    /// Number of currently alive elements.
    size_t Count() const { return m_count; }
    /// Maximum capacity (fixed).
    size_t GetCapacity() const { return Capacity; }

    /// Iterates over all alive entries, calling `fn(Handle<T>, T&)` for each.
    ///
    /// Scans the full capacity array and invokes the callback only for slots
    /// where `m_alive[i]` is true. Iteration order is by index (0..Capacity-1),
    /// NOT insertion order.
    ///
    /// # Arguments
    /// * `fn` - Callable with signature `void(Handle<T>, T&)`. May modify the
    ///          element but must NOT call Create/Destroy on this map (invalidates
    ///          iteration state).
    ///
    /// # Complexity
    /// O(Capacity) — always scans all slots regardless of alive count.
    template <typename Fn> void ForEach(Fn&& fn) {
        for (size_t i = 0; i < Capacity; ++i) {
            if (m_alive[i]) {
                fn(Handle<T>{static_cast<uint32_t>(i), m_generation[i]},
                   reinterpret_cast<T&>(m_storage[i]));
            }
        }
    }

  private:
    /// Sentinel value indicating end-of-chain in the free-list, or that the
    /// pool is completely full when stored in `m_freeHead`.
    static constexpr uint32_t kInvalid = 0xFFFFFFFF;

    /// Raw aligned storage for T instances. Only slots where `m_alive[i]` is
    /// true contain a constructed object; all others are uninitialized memory.
    typename std::aligned_storage<sizeof(T), alignof(T)>::type m_storage[Capacity];

    /// Per-slot generation counter. Starts at 0 and increments each time the
    /// slot is allocated via Create(). Never decrements or wraps to 0, so a
    /// handle's generation can never accidentally match after the slot has been
    /// freed and reallocated (until uint32_t overflow at ~4 billion reuses).
    uint32_t m_generation[Capacity] = {};

    /// Intrusive singly-linked free-list. For each free slot i, `m_freeNext[i]`
    /// holds the index of the next free slot, or `kInvalid` if i is the last
    /// free slot. Values are meaningless for alive slots (not read).
    uint32_t m_freeNext[Capacity];

    /// Per-slot liveness flag. True when the slot holds a constructed T, false
    /// when it is on the free-list. Kept separate from generation because
    /// generation alone cannot distinguish "alive with gen=N" from "freed with
    /// gen=N" — the alive flag resolves that ambiguity.
    bool m_alive[Capacity] = {};

    /// Index of the first free slot, or `kInvalid` (0xFFFFFFFF) when every
    /// slot is occupied and Create() must return Invalid().
    uint32_t m_freeHead = 0;

    /// Number of currently alive (constructed) elements. Maintained for O(1)
    /// Count() queries; also used by callers to check fullness without scanning.
    size_t m_count = 0;
};

} // namespace uz
