/// UzEngine Input System Implementation (Linux)
///
/// Implements InputSystem for keyboard, mouse, and gamepad input on Linux.
/// Maintains per-frame state tracking (current vs previous) for detecting transitions.
/// Integrates with X11 events from PlatformWindow::PumpMessages.

#include "input.h"

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
    std::memset(m_keyPressed, 0, sizeof(m_keyPressed));
    std::memset(m_keyReleased, 0, sizeof(m_keyReleased));
    std::memset(m_mouseCurrent, 0, sizeof(m_mouseCurrent));
    std::memset(m_mousePressed, 0, sizeof(m_mousePressed));
    std::memset(m_mouseReleased, 0, sizeof(m_mouseReleased));
}

void InputSystem::Shutdown() {
    m_actions.clear();
}

void InputSystem::BeginFrame() {
    EndFrame();
    PrepareFrame();
}

void InputSystem::EndFrame() {
    std::memset(m_keyPressed, 0, sizeof(m_keyPressed));
    std::memset(m_keyReleased, 0, sizeof(m_keyReleased));
    std::memset(m_mousePressed, 0, sizeof(m_mousePressed));
    std::memset(m_mouseReleased, 0, sizeof(m_mouseReleased));
    m_scrollDelta = 0.f;
}

void InputSystem::PrepareFrame() {
    m_mouseDX = m_mouseX - m_prevMouseX;
    m_mouseDY = m_mouseY - m_prevMouseY;
    m_prevMouseX = m_mouseX;
    m_prevMouseY = m_mouseY;
}

bool InputSystem::IsKeyDown(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyCurrent[k];
}

bool InputSystem::IsKeyJustPressed(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyPressed[k];
}

bool InputSystem::IsKeyJustReleased(Key key) const {
    uint16_t k = static_cast<uint16_t>(key);
    return k < kMaxKeys && m_keyReleased[k];
}

bool InputSystem::IsMouseDown(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mouseCurrent[b];
}

bool InputSystem::IsMouseJustPressed(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mousePressed[b];
}

bool InputSystem::IsMouseJustReleased(MouseButton btn) const {
    int b = static_cast<int>(btn);
    return b < kMaxMouseButtons && m_mouseReleased[b];
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
    if (vk < kMaxKeys && !m_keyCurrent[vk]) {
        m_keyPressed[vk] = true;
        m_keyCurrent[vk] = true;
    }
}

void InputSystem::OnKeyUp(uint16_t vk) {
    if (vk < kMaxKeys && m_keyCurrent[vk]) {
        m_keyReleased[vk] = true;
        m_keyCurrent[vk] = false;
    }
}

void InputSystem::OnMouseMove(float x, float y) {
    m_mouseX = x;
    m_mouseY = y;
}

void InputSystem::OnMouseButton(MouseButton btn, bool down) {
    int b = static_cast<int>(btn);
    if (b < kMaxMouseButtons && m_mouseCurrent[b] != down) {
        if (down) m_mousePressed[b] = true;
        else m_mouseReleased[b] = true;
        m_mouseCurrent[b] = down;
    }
}

void InputSystem::OnMouseScroll(float delta) {
    m_scrollDelta += delta;
}

} // namespace uz
