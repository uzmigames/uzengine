#pragma once

/// UzEngine Memory Ownership System
///
/// Implements Rust-inspired ownership semantics in C++:
/// - **Own<T>**: Exclusive ownership (unique_ptr-like)
/// - **Ref<T>**: Immutable borrow (shared_ptr-like, but no modification)
/// - **RefMut<T>**: Mutable borrow (exclusive mutable access)
///
/// # Borrow Checking
/// - **Debug builds**: Runtime borrow tracking detects violations (asserts)
/// - **Release builds**: Zero overhead (tracking compiled out)
/// - **Rules enforced**:
///   - Cannot hold mutable borrow while immutable borrows exist
///   - Cannot hold multiple mutable borrows
///   - Can hold multiple immutable borrows simultaneously
///
/// # Design Philosophy
/// Catch memory safety bugs at compile-time (via type system) and runtime (borrow checks)
/// rather than discovering them in production.
/// Inspired by Rust's ownership model, adapted for C++ game engine constraints.

#include <cstdint>
#include <memory>
#include <utility>

#ifndef UZ_SHIPPING
#include <atomic>
#include <cassert>
#include <cstdio>
#endif

namespace uz {

// ── Borrow tracking (debug only) ────────────────────────────────────────────

#ifndef UZ_SHIPPING

/// Runtime borrow checker for `Own<T>` resources (debug builds only).
///
/// Tracks counts of active immutable and mutable borrows using atomics.
/// Asserts on violations:
/// - Immutable borrow while mutable borrow is active
/// - Mutable borrow while any borrow (immutable or mutable) is active
///
/// Compiled out entirely in `UZ_SHIPPING` builds for zero overhead.
struct BorrowTracker {
    /// Number of active `Ref<T>` borrows.
    std::atomic<int32_t> immutableCount{0};
    /// Number of active `RefMut<T>` borrows (should be 0 or 1).
    std::atomic<int32_t> mutableCount{0};

    /// Registers a new immutable borrow. Asserts if a mutable borrow is active.
    void AcquireImmutable() {
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: immutable borrow while mutable borrow active!\n");
            assert(false && "Borrow violation: immutable borrow while mutable borrow active");
        }
        immutableCount.fetch_add(1, std::memory_order_release);
    }

    /// Releases an immutable borrow. Asserts if count would go negative.
    void ReleaseImmutable() {
        int32_t prev = immutableCount.fetch_sub(1, std::memory_order_release);
        assert(prev > 0 && "Released immutable borrow that wasn't held");
    }

    /// Registers a new mutable borrow. Asserts if any borrows are active.
    void AcquireMutable() {
        int32_t imm = immutableCount.load(std::memory_order_acquire);
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (imm != 0) {
            std::fprintf(stderr, "UZ_BORROW: mutable borrow while %d immutable borrows active!\n",
                         imm);
            assert(false && "Borrow violation: mutable borrow while immutable borrows active");
        }
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: double mutable borrow!\n");
            assert(false && "Borrow violation: double mutable borrow");
        }
        mutableCount.fetch_add(1, std::memory_order_release);
    }

    /// Releases a mutable borrow. Asserts if count would go negative.
    void ReleaseMutable() {
        int32_t prev = mutableCount.fetch_sub(1, std::memory_order_release);
        assert(prev > 0 && "Released mutable borrow that wasn't held");
    }
};

#endif // !UZ_SHIPPING

// ── Own<T> — unique owning pointer (move-only) ─────────────────────────────

/// Exclusive-ownership smart pointer (move-only, similar to `std::unique_ptr`).
///
/// Owns the pointed-to object and deletes it on destruction.
/// In debug builds, integrates with `BorrowTracker` to detect borrow violations.
///
/// # Thread Safety
/// NOT thread-safe for concurrent access. BorrowTracker uses atomics for
/// cross-thread borrow checking in debug builds only.
///
/// # Examples
/// ```cpp
/// Own<Mesh> mesh = MakeOwn<Mesh>(vertices, indices);
/// mesh->Draw();
/// Ref<Mesh> r = Ref<Mesh>(mesh);       // immutable borrow
/// RefMut<Mesh> rm = RefMut<Mesh>(mesh); // mutable borrow (fails if r still alive)
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
/// Copyable — each copy increments the immutable borrow count.
///
/// # Examples
/// ```cpp
/// Own<Config> cfg = MakeOwn<Config>();
/// Ref<Config> r1(cfg);  // borrow 1
/// Ref<Config> r2(cfg);  // borrow 2 (OK: multiple immutable)
/// r1->GetValue("key");  // read-only access
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
/// Move-only — cannot be copied (exclusivity). Cannot be taken while any
/// `Ref<T>` or other `RefMut<T>` exists (enforced by `BorrowTracker` in debug).
///
/// # Examples
/// ```cpp
/// Own<Scene> scene = MakeOwn<Scene>();
/// {
///     RefMut<Scene> rm(scene);
///     rm->AddEntity("player");  // mutable access
/// } // borrow released here
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
