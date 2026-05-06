/// UzEngine Slate UI — Attribute<T> pull-style data binding.
///
/// Phase 18 / Step 2. Implements **Decision 3** from `00-decisions.md`:
/// every widget property that may change between frames is typed as
/// `Attribute<T>`. The widget pulls the current value during Tick or
/// Paint via `attr.Get()`.
///
/// Two assignment forms:
///   - literal:  `text->Text = "hello"`              (snapshot value)
///   - lambda:   `text->Text = []{ return Now(); }`  (re-evaluated per Get)
///
/// Lambda assignment never mutates the underlying value field; the
/// getter is consulted on every `Get()` call. Literal assignment clears
/// any previous lambda. The widget is responsible for invalidating
/// itself when the pulled value differs from the previous frame
/// (Step 5 introduces the dirty-bit infrastructure that makes this
/// efficient).
///
/// This header is C++17, no engine dependencies — only `<functional>`
/// and `<utility>` from the standard library.

#pragma once

#include <functional>
#include <utility>

namespace uz::ui {

template <typename T>
class Attribute {
public:
    Attribute() = default;

    /// Implicit conversion from a literal — enables
    /// `text->Text = "hello"` and `Attribute<int> a = 7;`.
    Attribute(T value) : m_value(std::move(value)) {}

    /// Construct from a lambda getter.
    Attribute(std::function<T()> getter) : m_getter(std::move(getter)) {}

    /// Assigns a literal value. Clears any previously-bound lambda.
    Attribute& operator=(T value) {
        m_value = std::move(value);
        m_getter = nullptr;
        return *this;
    }

    /// Assigns a lambda getter. The literal field is unchanged but
    /// will not be consulted while the lambda is set.
    Attribute& operator=(std::function<T()> getter) {
        m_getter = std::move(getter);
        return *this;
    }

    /// Returns the current value: lambda result if bound, else literal.
    [[nodiscard]] T Get() const {
        return m_getter ? m_getter() : m_value;
    }

    /// True if a lambda is currently bound.
    [[nodiscard]] bool IsBound() const noexcept {
        return static_cast<bool>(m_getter);
    }

    /// Drops any bound lambda; subsequent Get() returns the literal.
    void Unbind() noexcept { m_getter = nullptr; }

private:
    T m_value{};
    std::function<T()> m_getter;
};

} // namespace uz::ui
