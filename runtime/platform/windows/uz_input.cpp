/// UzEngine Input System Implementation (Windows)
///
/// Implements InputSystem for keyboard, mouse, and gamepad input on Windows.
/// Maintains per-frame state tracking (current vs previous) for detecting transitions.
/// Integrates with WM_* messages from PlatformWindow::WndProc.

#include "uz_input.h"

#include <cstring>

namespace uz {

/// Initializes input state arrays to zero (no keys/buttons pressed).
///
/// # Behavior
/// - Zeros both current and previous key arrays
/// - Zeros both current and previous mouse button arrays
/// - Mouse position and delta left at default (will update on first move)
void InputSystem::Init() {
    std::memset(m_keyCurrent, 0, sizeof(m_keyCurrent));
    std::memset(m_keyPrevious, 0, sizeof(m_keyPrevious));
    std::memset(m_mouseCurrent, 0, sizeof(m_mouseCurrent));
    std::memset(m_mousePrevious, 0, sizeof(m_mousePrevious));
}

void InputSystem::Shutdown() {
    m_actions.clear();
}

void InputSystem::BeginFrame() {
    std::memcpy(m_keyPrevious, m_keyCurrent, sizeof(m_keyCurrent));
    std::memcpy(m_mousePrevious, m_mouseCurrent, sizeof(m_mouseCurrent));
    m_mouseDX = m_mouseX - m_prevMouseX;
    m_mouseDY = m_mouseY - m_prevMouseY;
    m_prevMouseX = m_mouseX;
    m_prevMouseY = m_mouseY;
    m_scrollDelta = 0.f;
}

bool InputSystem::IsKeyDown(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyCurrent[k];
}

bool InputSystem::IsKeyJustPressed(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyCurrent[k] && !m_keyPrevious[k];
}

bool InputSystem::IsKeyJustReleased(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && !m_keyCurrent[k] && m_keyPrevious[k];
}

bool InputSystem::IsMouseDown(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mouseCurrent[b];
}

bool InputSystem::IsMouseJustPressed(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mouseCurrent[b] && !m_mousePrevious[b];
}

bool InputSystem::IsMouseJustReleased(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && !m_mouseCurrent[b] && m_mousePrevious[b];
}

void InputSystem::RegisterAction(const std::string& name, Key key) {
    m_actions[name] = key;
}

bool InputSystem::IsActionDown(const std::string& name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    return IsKeyDown(it->second);
}

bool InputSystem::IsActionJustPressed(const std::string& name) const {
    auto it = m_actions.find(name);
    if (it == m_actions.end())
        return false;
    return IsKeyJustPressed(it->second);
}

void InputSystem::OnKeyDown(uint16_t vk) {
    if (vk < kMaxKeys)
        m_keyCurrent[vk] = true;
}

void InputSystem::OnKeyUp(uint16_t vk) {
    if (vk < kMaxKeys)
        m_keyCurrent[vk] = false;
}

void InputSystem::OnMouseMove(float x, float y) {
    m_mouseX = x;
    m_mouseY = y;
}

void InputSystem::OnMouseButton(MouseButton btn, bool down) {
    int b = static_cast<int>(btn);
    if (b < kMaxMouseButtons)
        m_mouseCurrent[b] = down;
}

void InputSystem::OnMouseScroll(float delta) {
    m_scrollDelta += delta;
}

} // namespace uz
