#pragma once

/// UzEngine Platform Window (macOS forwarding header)
///
/// Re-exports the cross-platform PlatformWindow interface from the shared
/// header in platform/windows/. The class declaration is platform-independent;
/// only the implementation files (.cpp on Windows, .mm on macOS) differ.

#include "../windows/platform_window.h"
