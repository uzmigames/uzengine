#pragma once

/// UzEngine Memory Ownership System
///
/// Implements Rust-inspired ownership semantics in C++:
/// - **`Own<T>`**: Exclusive ownership (move-only, like `std::unique_ptr`)
/// - **`Ref<T>`**: Immutable borrow (copyable, const access only)
/// - **`RefMut<T>`**: Mutable borrow (move-only, exclusive mutable access)
///
/// # Borrow Checking Architecture
/// In debug builds (`!UZ_SHIPPING`), each `Own<T>` embeds a `BorrowTracker` that
/// counts active immutable and mutable borrows using atomics. Violations trigger
/// `assert()` with a diagnostic message to stderr.
///
/// In shipping builds (`UZ_SHIPPING`), `BorrowTracker` is compiled out entirely —
/// `Own<T>` shrinks to a single raw pointer (8 bytes), and `Ref<T>`/`RefMut<T>`
/// become thin non-owning pointer wrappers with zero overhead.
///
/// # Borrow Rules (enforced at runtime in debug)
/// 1. **Multiple `Ref<T>` OK**: Any number of immutable borrows can coexist.
/// 2. **`RefMut<T>` is exclusive**: Cannot create while any `Ref<T>` or other
///    `RefMut<T>` is alive.
/// 3. **`Ref<T>` blocked by `RefMut<T>`**: Cannot create an immutable borrow
///    while a mutable borrow is active.
///
/// # Thread Safety
/// `BorrowTracker` uses `std::atomic<int32_t>` for cross-thread borrow checking.
/// However, the borrow check is a **non-atomic read-then-write** sequence (load
/// count, assert, then increment) — this catches most violations but is NOT a
/// formal guarantee. For true thread-safe shared access, use external
/// synchronization (mutex). The ownership types themselves (`Own`, `Ref`, `RefMut`)
/// are NOT safe to move or copy across threads without synchronization.
///
/// # Panics (Debug Builds)
/// - `Ref<T>` constructed while `RefMut<T>` active: asserts "immutable borrow
///   while mutable borrow active"
/// - `RefMut<T>` constructed while `Ref<T>` active: asserts "mutable borrow
///   while immutable borrows active"
/// - `RefMut<T>` constructed while another `RefMut<T>` active: asserts "double
///   mutable borrow"
/// - Borrow released more times than acquired: asserts "released borrow that
///   wasn't held"
///
/// # Design Philosophy
/// Catch memory safety bugs at compile-time (via type system) and runtime
/// (borrow checks) rather than discovering them in production.
/// Inspired by Rust's ownership model, adapted for C++ game engine constraints.
///
/// # Examples
/// ```cpp
/// // Create an owned resource
/// Own<Mesh> mesh = MakeOwn<Mesh>(vertices, indices);
///
/// // Multiple immutable borrows are fine
/// Ref<Mesh> r1(mesh);
/// Ref<Mesh> r2(mesh);  // OK: multiple readers
/// r1->GetVertexCount();
///
/// // Mutable borrow requires exclusivity
/// // r1 and r2 must be destroyed first!
/// {
///     RefMut<Mesh> rm(mesh);
///     rm->Optimize();  // exclusive mutable access
/// } // rm released here
/// ```

#include <cstdint>
#include <memory>
#include <utility>

#ifndef UZ_SHIPPING
#include <atomic>
#include "engine_assert.h"
#include <cstdio>
#endif

namespace uz {

// ── Borrow tracking (debug only) ────────────────────────────────────────────

#ifndef UZ_SHIPPING

/// Runtime borrow checker for `Own<T>` resources (debug builds only).
///
/// Tracks counts of active immutable and mutable borrows using atomics.
/// Asserts on violations (see file-level doc for full list).
///
/// Compiled out entirely in `UZ_SHIPPING` builds for zero overhead.
///
/// # Memory Layout
/// 8 bytes: two `std::atomic<int32_t>` counters (immutable + mutable).
/// Embedded directly in `Own<T>` — no heap allocation.
///
/// # Thread Safety
/// Atomics provide visibility across threads, but the check-then-increment
/// sequence is not linearizable. This catches most bugs but does not
/// replace a mutex for concurrent borrow creation.
///
/// # Invariants
/// - `mutableCount` is always 0 or 1.
/// - If `mutableCount == 1`, `immutableCount` must be 0.
/// - Both counters must be 0 when `Own<T>` is destroyed or moved.
struct BorrowTracker {
    /// Number of active `Ref<T>` borrows.
    std::atomic<int32_t> immutableCount{0};
    /// Number of active `RefMut<T>` borrows (should be 0 or 1).
    std::atomic<int32_t> mutableCount{0};

    /// Registers a new immutable borrow.
    ///
    /// # Panics
    /// Asserts if `mutableCount != 0` (a `RefMut<T>` is active).
    void AcquireImmutable() {
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: immutable borrow while mutable borrow active!\n");
            UZ_CHECK(false);
        }
        immutableCount.fetch_add(1, std::memory_order_release);
    }

    /// Releases an immutable borrow.
    ///
    /// # Panics
    /// Asserts if `immutableCount` was already 0 (double-release).
    void ReleaseImmutable() {
        int32_t prev = immutableCount.fetch_sub(1, std::memory_order_release);
        UZ_CHECK(prev > 0);
    }

    /// Registers a new mutable borrow (exclusive access).
    ///
    /// # Panics
    /// Asserts if `immutableCount != 0` or `mutableCount != 0`.
    void AcquireMutable() {
        int32_t imm = immutableCount.load(std::memory_order_acquire);
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (imm != 0) {
            std::fprintf(stderr, "UZ_BORROW: mutable borrow while %d immutable borrows active!\n",
                         imm);
            UZ_CHECK(false);
        }
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: double mutable borrow!\n");
            UZ_CHECK(false);
        }
        mutableCount.fetch_add(1, std::memory_order_release);
    }

    /// Releases a mutable borrow.
    ///
    /// # Panics
    /// Asserts if `mutableCount` was already 0 (double-release).
    void ReleaseMutable() {
        int32_t prev = mutableCount.fetch_sub(1, std::memory_order_release);
        UZ_CHECK(prev > 0);
    }
};

#endif // !UZ_SHIPPING

// ── Own<T> — unique owning pointer (move-only) ─────────────────────────────

/// Exclusive-ownership smart pointer (move-only, similar to `std::unique_ptr`).
///
/// Owns the pointed-to object and deletes it on destruction. Move-only —
/// copying is deleted to enforce single ownership.
///
/// In debug builds, embeds a `BorrowTracker` for runtime borrow violation
/// detection. In shipping builds, this is compiled out.
///
/// # Memory Layout
/// - **Debug**: `sizeof(T*) + sizeof(BorrowTracker)` = 16 bytes on 64-bit.
/// - **Shipping**: `sizeof(T*)` = 8 bytes on 64-bit (same as `unique_ptr`).
///
/// # Thread Safety
/// NOT thread-safe. Do not move or access from multiple threads without
/// external synchronization. `BorrowTracker` atomics provide cross-thread
/// visibility for borrow checking only.
///
/// # Panics
/// None from `Own<T>` itself. Panics originate from `Ref<T>` / `RefMut<T>`
/// construction when borrow rules are violated (see `BorrowTracker`).
///
/// # Examples
/// ```cpp
/// Own<Mesh> mesh = MakeOwn<Mesh>(vertices, indices);
/// mesh->Draw();
///
/// // Immutable borrows — multiple OK
/// Ref<Mesh> r = Ref<Mesh>(mesh);
///
/// // Mutable borrow — fails if r is still alive (debug assert)
/// // r must go out of scope first
/// ```
template <typename T> class Own {
  public:
    /// Default: null (owns nothing).
    Own() = default;
    /// Takes ownership of a raw pointer. Caller must not delete `ptr` afterwards.
    explicit Own(T* ptr) : m_ptr(ptr) {}
    /// Deletes the owned object.
    ~Own() { delete m_ptr; }

    Own(Own&& other) noexcept : m_ptr(other.m_ptr) {
        // BorrowTracker stays default-initialized (fresh, no borrows).
        // No borrows should be active when moving ownership.
        other.m_ptr = nullptr;
    }

    Own& operator=(Own&& other) noexcept {
        if (this != &other) {
            delete m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            // m_tracker resets implicitly — new owner, fresh borrow state.
        }
        return *this;
    }

    Own(const Own&) = delete;
    Own& operator=(const Own&) = delete;

    /// Returns the raw pointer without transferring ownership.
    T* Get() const { return m_ptr; }
    /// Dereference operator for member access.
    T* operator->() const { return m_ptr; }
    /// Dereference operator for value access.
    T& operator*() const { return *m_ptr; }
    /// True if this owns a non-null object.
    explicit operator bool() const { return m_ptr != nullptr; }

    /// Releases ownership and returns the raw pointer. Caller is now responsible for deletion.
    T* Release() {
        T* p = m_ptr;
        m_ptr = nullptr;
        return p;
    }

    /// Deletes the current object and optionally takes ownership of a new one.
    void Reset(T* ptr = nullptr) {
        delete m_ptr;
        m_ptr = ptr;
    }

#ifndef UZ_SHIPPING
    BorrowTracker* GetTracker() { return &m_tracker; }
#endif

  private:
    T* m_ptr = nullptr;
#ifndef UZ_SHIPPING
    BorrowTracker m_tracker;
#endif
};

/// Creates an `Own<T>` by constructing T in-place (analogous to `std::make_unique`).
template <typename T, typename... Args> Own<T> MakeOwn(Args&&... args) {
    return Own<T>(new T(std::forward<Args>(args)...));
}

// ── Ref<T> — non-owning immutable borrow ────────────────────────────────────

/// Non-owning immutable borrow of an `Own<T>` resource.
///
/// Multiple `Ref<T>` borrows can coexist, but a `RefMut<T>` cannot be taken
/// while any `Ref<T>` is alive (enforced by `BorrowTracker` in debug builds).
/// Copyable — each copy increments the immutable borrow count in the tracker.
///
/// # Memory Layout
/// - **Debug**: `sizeof(const T*) + sizeof(BorrowTracker*)` = 16 bytes.
/// - **Shipping**: `sizeof(const T*)` = 8 bytes.
///
/// # Thread Safety
/// The `Ref<T>` object itself is NOT thread-safe (do not copy/move across
/// threads without synchronization). However, the const access it provides
/// is safe if `T`'s const methods are thread-safe.
///
/// # Panics (Debug Builds)
/// Construction asserts if a `RefMut<T>` is currently active on the same
/// `Own<T>`. Destruction never panics.
///
/// # Examples
/// ```cpp
/// Own<Config> cfg = MakeOwn<Config>();
/// Ref<Config> r1(cfg);  // borrow 1
/// Ref<Config> r2(cfg);  // borrow 2 (OK: multiple immutable)
/// r1->GetValue("key");  // read-only access via const T*
/// ```
template <typename T> class Ref {
  public:
    /// Default: null borrow.
    Ref() = default;

    explicit Ref(const Own<T>& owner)
        : m_ptr(owner.Get())
#ifndef UZ_SHIPPING
          ,
          m_tracker(const_cast<Own<T>&>(owner).GetTracker())
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker)
            m_tracker->AcquireImmutable();
#endif
    }

    ~Ref() {
#ifndef UZ_SHIPPING
        if (m_tracker)
            m_tracker->ReleaseImmutable();
#endif
    }

    Ref(const Ref& other)
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
          ,
          m_tracker(other.m_tracker)
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker)
            m_tracker->AcquireImmutable();
#endif
    }

    Ref& operator=(const Ref& other) {
        if (this != &other) {
#ifndef UZ_SHIPPING
            if (m_tracker)
                m_tracker->ReleaseImmutable();
            m_tracker = other.m_tracker;
            if (m_tracker)
                m_tracker->AcquireImmutable();
#endif
            m_ptr = other.m_ptr;
        }
        return *this;
    }

    Ref(Ref&& other) noexcept
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
          ,
          m_tracker(other.m_tracker)
#endif
    {
        other.m_ptr = nullptr;
#ifndef UZ_SHIPPING
        other.m_tracker = nullptr;
#endif
    }

    Ref& operator=(Ref&& other) noexcept {
        if (this != &other) {
#ifndef UZ_SHIPPING
            if (m_tracker)
                m_tracker->ReleaseImmutable();
            m_tracker = other.m_tracker;
            other.m_tracker = nullptr;
#endif
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    /// Returns const pointer to the borrowed object.
    const T* Get() const { return m_ptr; }
    /// Const member access.
    const T* operator->() const { return m_ptr; }
    /// Const dereference.
    const T& operator*() const { return *m_ptr; }
    /// True if this borrow is active (non-null).
    explicit operator bool() const { return m_ptr != nullptr; }

  private:
    const T* m_ptr = nullptr;
#ifndef UZ_SHIPPING
    BorrowTracker* m_tracker = nullptr;
#endif
};

// ── RefMut<T> — exclusive mutable borrow ────────────────────────────────────

/// Non-owning exclusive mutable borrow of an `Own<T>` resource.
///
/// Move-only — cannot be copied (exclusivity enforced at compile time).
/// Cannot be taken while any `Ref<T>` or other `RefMut<T>` exists on the
/// same `Own<T>` (enforced by `BorrowTracker` at runtime in debug builds).
///
/// # Memory Layout
/// - **Debug**: `sizeof(T*) + sizeof(BorrowTracker*)` = 16 bytes.
/// - **Shipping**: `sizeof(T*)` = 8 bytes.
///
/// # Thread Safety
/// NOT thread-safe. Provides mutable access — concurrent use from multiple
/// threads is a data race. Use external synchronization if needed.
///
/// # Panics (Debug Builds)
/// Construction asserts if:
/// - Any `Ref<T>` is active on the same `Own<T>` ("mutable borrow while
///   immutable borrows active").
/// - Another `RefMut<T>` is active on the same `Own<T>` ("double mutable
///   borrow").
///
/// Destruction never panics (assuming no double-release bug).
///
/// # Examples
/// ```cpp
/// Own<Scene> scene = MakeOwn<Scene>();
/// {
///     RefMut<Scene> rm(scene);
///     rm->AddEntity("player");  // exclusive mutable access
/// } // borrow released — Ref<T> can be taken again
/// ```
template <typename T> class RefMut {
  public:
    /// Default: null borrow.
    RefMut() = default;

    explicit RefMut(Own<T>& owner)
        : m_ptr(owner.Get())
#ifndef UZ_SHIPPING
          ,
          m_tracker(owner.GetTracker())
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker)
            m_tracker->AcquireMutable();
#endif
    }

    ~RefMut() {
#ifndef UZ_SHIPPING
        if (m_tracker)
            m_tracker->ReleaseMutable();
#endif
    }

    // Move only — exclusive borrow
    RefMut(const RefMut&) = delete;
    RefMut& operator=(const RefMut&) = delete;

    RefMut(RefMut&& other) noexcept
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
          ,
          m_tracker(other.m_tracker)
#endif
    {
        other.m_ptr = nullptr;
#ifndef UZ_SHIPPING
        other.m_tracker = nullptr;
#endif
    }

    RefMut& operator=(RefMut&& other) noexcept {
        if (this != &other) {
#ifndef UZ_SHIPPING
            if (m_tracker)
                m_tracker->ReleaseMutable();
            m_tracker = other.m_tracker;
            other.m_tracker = nullptr;
#endif
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    /// Returns mutable pointer to the borrowed object.
    T* Get() const { return m_ptr; }
    /// Mutable member access.
    T* operator->() const { return m_ptr; }
    /// Mutable dereference.
    T& operator*() const { return *m_ptr; }
    /// True if this borrow is active (non-null).
    explicit operator bool() const { return m_ptr != nullptr; }

  private:
    T* m_ptr = nullptr;
#ifndef UZ_SHIPPING
    BorrowTracker* m_tracker = nullptr;
#endif
};

} // namespace uz
