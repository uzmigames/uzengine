#pragma once

/// UzEngine Memory Foundation
/// Rust-inspired ownership: Own<T>, Ref<T>, RefMut<T>.
/// Debug borrow checker validates at runtime. Zero-cost in release.

#include <memory>
#include <cstdint>
#include <utility>

#ifndef UZ_SHIPPING
#include <atomic>
#include <cassert>
#include <cstdio>
#endif

namespace uz {

// ── Borrow tracking (debug only) ────────────────────────────────────────────

#ifndef UZ_SHIPPING

/// Tracks active borrows for an owned resource.
struct BorrowTracker {
    std::atomic<int32_t> immutableCount{0};
    std::atomic<int32_t> mutableCount{0};

    void AcquireImmutable() {
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: immutable borrow while mutable borrow active!\n");
            assert(false && "Borrow violation: immutable borrow while mutable borrow active");
        }
        immutableCount.fetch_add(1, std::memory_order_release);
    }

    void ReleaseImmutable() {
        int32_t prev = immutableCount.fetch_sub(1, std::memory_order_release);
        assert(prev > 0 && "Released immutable borrow that wasn't held");
    }

    void AcquireMutable() {
        int32_t imm = immutableCount.load(std::memory_order_acquire);
        int32_t mut = mutableCount.load(std::memory_order_acquire);
        if (imm != 0) {
            std::fprintf(stderr, "UZ_BORROW: mutable borrow while %d immutable borrows active!\n", imm);
            assert(false && "Borrow violation: mutable borrow while immutable borrows active");
        }
        if (mut != 0) {
            std::fprintf(stderr, "UZ_BORROW: double mutable borrow!\n");
            assert(false && "Borrow violation: double mutable borrow");
        }
        mutableCount.fetch_add(1, std::memory_order_release);
    }

    void ReleaseMutable() {
        int32_t prev = mutableCount.fetch_sub(1, std::memory_order_release);
        assert(prev > 0 && "Released mutable borrow that wasn't held");
    }
};

#endif // !UZ_SHIPPING

// ── Own<T> — unique owning pointer (move-only) ─────────────────────────────

template <typename T>
class Own {
public:
    Own() = default;
    explicit Own(T* ptr) : m_ptr(ptr) {}
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

    T* Get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    T* Release() {
        T* p = m_ptr;
        m_ptr = nullptr;
        return p;
    }

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

template <typename T, typename... Args>
Own<T> MakeOwn(Args&&... args) {
    return Own<T>(new T(std::forward<Args>(args)...));
}

// ── Ref<T> — non-owning immutable borrow ────────────────────────────────────

template <typename T>
class Ref {
public:
    Ref() = default;

    explicit Ref(const Own<T>& owner)
        : m_ptr(owner.Get())
#ifndef UZ_SHIPPING
        , m_tracker(const_cast<Own<T>&>(owner).GetTracker())
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker) m_tracker->AcquireImmutable();
#endif
    }

    ~Ref() {
#ifndef UZ_SHIPPING
        if (m_tracker) m_tracker->ReleaseImmutable();
#endif
    }

    Ref(const Ref& other)
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
        , m_tracker(other.m_tracker)
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker) m_tracker->AcquireImmutable();
#endif
    }

    Ref& operator=(const Ref& other) {
        if (this != &other) {
#ifndef UZ_SHIPPING
            if (m_tracker) m_tracker->ReleaseImmutable();
            m_tracker = other.m_tracker;
            if (m_tracker) m_tracker->AcquireImmutable();
#endif
            m_ptr = other.m_ptr;
        }
        return *this;
    }

    Ref(Ref&& other) noexcept
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
        , m_tracker(other.m_tracker)
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
            if (m_tracker) m_tracker->ReleaseImmutable();
            m_tracker = other.m_tracker;
            other.m_tracker = nullptr;
#endif
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    const T* Get() const { return m_ptr; }
    const T* operator->() const { return m_ptr; }
    const T& operator*() const { return *m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    const T* m_ptr = nullptr;
#ifndef UZ_SHIPPING
    BorrowTracker* m_tracker = nullptr;
#endif
};

// ── RefMut<T> — exclusive mutable borrow ────────────────────────────────────

template <typename T>
class RefMut {
public:
    RefMut() = default;

    explicit RefMut(Own<T>& owner)
        : m_ptr(owner.Get())
#ifndef UZ_SHIPPING
        , m_tracker(owner.GetTracker())
#endif
    {
#ifndef UZ_SHIPPING
        if (m_tracker) m_tracker->AcquireMutable();
#endif
    }

    ~RefMut() {
#ifndef UZ_SHIPPING
        if (m_tracker) m_tracker->ReleaseMutable();
#endif
    }

    // Move only — exclusive borrow
    RefMut(const RefMut&) = delete;
    RefMut& operator=(const RefMut&) = delete;

    RefMut(RefMut&& other) noexcept
        : m_ptr(other.m_ptr)
#ifndef UZ_SHIPPING
        , m_tracker(other.m_tracker)
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
            if (m_tracker) m_tracker->ReleaseMutable();
            m_tracker = other.m_tracker;
            other.m_tracker = nullptr;
#endif
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    T* Get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

private:
    T* m_ptr = nullptr;
#ifndef UZ_SHIPPING
    BorrowTracker* m_tracker = nullptr;
#endif
};

} // namespace uz
