/// UzEngine Input System Implementation (macOS)
///
/// The InputSystem implementation is fully platform-independent -- no OS-specific
/// API calls. Rather than duplicating the code, this translation unit compiles
/// the shared implementation from the windows directory.
///
/// # Future Work
/// When macOS NSEvent input dispatch is added (keyboard, mouse, trackpad),
/// macOS-specific translation code (NSEvent keyCode -> uz::Key mapping) should
/// be added in a separate file (e.g., macos_input_dispatch.mm).

#include "../windows/input.cpp" // NOLINT(build/include) — intentional TU forwarding
