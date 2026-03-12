#pragma once

/// UzEngine Input System (macOS)
///
/// Re-exports the platform-independent input interface from the Windows
/// platform directory. The InputSystem, Key, and MouseButton types are
/// fully cross-platform -- only the event dispatch layer differs per OS.
///
/// # Why a separate header?
/// CMake adds `platform/macos/` (not `platform/windows/`) to the include
/// path on Apple builds. This forwarding header ensures `#include "input.h"`
/// resolves correctly on macOS without duplicating the interface definition.
///
/// # Input dispatch on macOS
/// NSEvent-based input routing is not yet implemented. The InputSystem
/// can still be used for action mapping and state queries -- it just won't
/// receive events from the window until macOS input dispatch is wired up.

#include "../windows/input.h"
