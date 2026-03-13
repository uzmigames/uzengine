# Godot Engine Platform Layer — Comprehensive Inventory

**Analysis Date**: 2026-03-12
**Godot Version**: 4.x (master branch)
**Scope**: Complete platform layer at `/e/godot/platform/`

This document inventories what every platform implements. Focus: Windows and Linux as primary targets for UzEngine.

---

## File Statistics

| Platform | .h/.cpp Files | Sub-dirs | Notes |
|----------|---------------|----------|-------|
| **Windows** | 40 | 1 | Monolithic, DX12/Vulkan/OpenGL, IPC via OLE |
| **Linux/BSD** | 63 | 4 | Dual display servers: X11 + Wayland |
| **macOS** | 36 | 2 | Objective-C headers, embedded mode support |
| **Android** | 62 | 8 | Heavy JNI, Java wrapper layer |
| **iOS** | 15 | 2 | Minimal, inherits from AppleEmbedded |
| **Web** | 32 | 5 | Emscripten, WebAssembly focus |

---

## WINDOWS Platform (`/platform/windows/`)

### **Files: 40 .h/.cpp files**

#### Core OS Layer
- `os_windows.h / os_windows.cpp` — OS abstraction, process management, environment
- `windows.h` — Platform-specific headers aggregation
- `godot_windows.cpp` — Main entry point, WinMain()
- `libgodot_windows.cpp` — Dynamic library export (DLL interface)

#### Window & Display Management
- `display_server_windows.h / display_server_windows.cpp` — **Main window management**
  - Multi-window support with window IDs
  - Window state: minimized, maximized, fullscreen, borderless, resizable
  - HDR output support (hdr_output_requested, reference_luminance)
  - Transient window relationships (parent/child)
  - Popup window list and safe rect tracking
  - Multi-monitor DPI-aware support via HMONITOR
  - Window flags: always_on_top, no_focus, exclusive, no_redirection_bitmap
  - Window sizing: min_size, max_size, pre-fullscreen rect saving

#### Rendering Context & Graphics
- `gl_manager_windows_native.h/.cpp` — OpenGL (native WGL)
  - WGL context creation, pixel format selection
  - Swap interval (vsync) control

- `gl_manager_windows_angle.h/.cpp` — OpenGL via ANGLE (DirectX backend)
  - ANGLE abstraction for D3D11/D3D12 rendering
  - EGL context management over ANGLE

- `rendering_context_driver_vulkan_windows.h/.cpp` — Vulkan on Windows
  - Vulkan surface creation via VK_KHR_win32_surface
  - Instance & device initialization

- `platform_gl.h` — GL extension macros

#### Input Handling
- Key events: `key_mapping_windows.h/.cpp`
  - WM_KEYDOWN, WM_KEYUP message mapping to engine key codes
  - Keyboard layout handling via GetKeyboardLayout()
  - Language table: `lang_table.h` (VK_* to Godot KeyCode)

- Mouse/Pen Input:
  - `display_server_windows.h`: raw WM_MOUSE* messages (left, right, middle, double-click)
  - Tablet support via WinTab API (pressure, tilt, orientation, inversion)
    - `LOGCONTEXTW`, `PACKET`, pressure ranges, tilt vectors
    - WinTab context per window: `HANDLE wtctx`, min/max pressure, tilt_supported
  - Pen inverted state tracking
  - Cursor shape management (HCURSOR), custom cursors support

- Gamepad/Joystick:
  - Integrated via SDL (JoypadSDL) referenced in display_server_windows.h

- Touch Input:
  - Touch state map: `RBMap<int, Vector2> touch_state`
  - Touch ID to position tracking

- IME (Input Method Editor):
  - Per-window: `HIMC im_himc`, `im_position`, `ime_active`, `ime_in_progress`
  - WM_IME_* message handling (composition, finalization)

- Clipboard:
  - OLE drag-drop via `DropTargetWindows` (COM object)
  - Files drag-drop callback

#### Audio System
- `drivers/wasapi/audio_driver_wasapi.h/.cpp` — Windows Audio Session API
  - Spatial audio capable, WASAPI exclusive/shared mode

- `drivers/xaudio2/audio_driver_xaudio2.h/.cpp` — XAudio2 (legacy/fallback)
  - Supported via `XAUDIO2_ENABLED` flag in os_windows.h

- `drivers/winmidi/midi_driver_winmidi.h` — MIDI input
  - WinMM MIDI device enumeration & callbacks

#### Text-to-Speech
- `tts_windows.h / tts_windows.cpp` — Windows Speech API (SAPI)
  - Voice selection (male/female)
  - Speech rate/volume control

#### OS Services & System Integration
- **Process Management**
  - `execute()` — Run external process, capture stdout/stderr
  - `create_process()` — Spawn process without waiting
  - `kill()` — Terminate by ProcessID
  - `is_process_running()`, `get_process_exit_code()`
  - ProcessInfo struct: STARTUPINFOEX, PROCESS_INFORMATION, exit code tracking

- **Environment & System Info**
  - `get_environment()`, `set_environment()`, `unset_environment()`
  - `get_unique_id()` — Machine GUID via Win32 API
  - `get_processor_name()` — CPU brand via WMI or registry
  - `get_model_name()` — Device model

- **Paths & Directories**
  - `get_config_path()` — APPDATA (roaming user data)
  - `get_data_path()` — LOCAL_APPDATA (local-only data)
  - `get_cache_path()` — Local cache, temp files
  - `get_temp_path()` — Windows temp directory
  - `get_system_dir(SystemDir)` — Desktop, Documents, Downloads, etc.
  - `get_cwd()`, `set_cwd()` — Current working directory

- **Font System**
  - DirectWrite integration: `IDWriteFactory`, `IDWriteFactory2`, `IDWriteFontCollection`
  - `get_system_fonts()` — Enumerate system fonts
  - `get_system_font_path()` — Resolve font file by name/weight/stretch/italic
  - `get_system_font_path_for_text()` — Font fallback for specific text/locale/script
  - Font weight/stretch conversion to DirectWrite enums
  - System font fallback chain

- **Dynamic Library Loading**
  - `open_dynamic_library()` — LoadLibrary wrapper (with temp copy for safety)
  - `close_dynamic_library()` — FreeLibrary
  - `get_dynamic_library_symbol_handle()` — GetProcAddress
  - Temp library cache to avoid in-use DLL issues

- **File Operations**
  - `move_to_trash()` — SHFileOperation for Recycle Bin
  - `shell_open()` — ShellExecute for launching URLs/documents
  - `shell_show_in_file_manager()` — Explorer.exe with /select

- **Time & Timing**
  - `get_unix_time()` — Unix timestamp (seconds since epoch)
  - `get_datetime()` — Year/month/day/hour/min/sec (UTC or local)
  - `get_time_zone_info()` — Timezone name, UTC offset, DST rules
  - `get_ticks_usec()` — Microsecond timer via QueryPerformanceCounter
  - `delay_usec()` — High-precision sleep

- **Crash Handling**
  - `crash_handler_windows_seh.cpp` — Structured Exception Handling (SEH)
  - `crash_handler_windows_signal.cpp` — Signal handler registration
  - Crash handler can be disabled (for external tooling)

- **System Theme (Dark Mode)**
  - UXTheme API detection: `ShouldAppsUseDarkMode`, `GetImmersiveColorFromColorSetEx`
  - Dark title bar support (Windows 10 20H1+)
  - Legacy dark mode fallback (pre-20H1)

- **GPU/Driver Info**
  - `get_video_adapter_driver_info()` — GPU name/driver version via registry or WMI
  - `get_user_prefers_integrated_gpu()` — Power preference

- **Taskbar Integration**
  - ITaskbarList3: progress indicator (progress state, value)
  - Used for game loading progress display

#### Console & Logging
- `console_wrapper_windows.cpp` — Console window attachment/detachment
  - Can be wrapped for debug output redirection

- `windows_terminal_logger.h/.cpp` — Terminal/console output formatting
  - ANSI color codes via ENABLE_VIRTUAL_TERMINAL_PROCESSING
  - OutputDebugString for IDE debugging

#### Utilities
- `windows_utils.h/.cpp` — Helper functions
  - Quote command line arguments for subprocess execution

- `native_menu_windows.h/.cpp` — Native context menus
- `drop_target_windows.h/.cpp` — OLE Drag & Drop (IDropTarget COM object)
- `key_mapping_windows.h/.cpp` — WM_* to Godot key mapping
- `platform_config.h` — Compilation flags

---

## LINUX/BSD Platform (`/platform/linuxbsd/`)

### **Files: 63 .h/.cpp files** (split between X11, Wayland, shared core)

#### Core OS Layer
- `os_linuxbsd.h / os_linuxbsd.cpp` — OS abstraction for Linux/FreeBSD
- `godot_linuxbsd.cpp` — Main entry point
- `libgodot_linuxbsd.cpp` — Shared library export (.so interface)
- `platform_config.h` — Compilation flags

#### Display Servers (Dual Protocol Support)

##### **X11 Display Server** (`x11/` subdirectory, 15+ files)

- `display_server_x11.h / display_server_x11.cpp` — Main X11 window manager
  - Multi-window support with Atom-based protocol
  - XDND (X11 Drag and Drop) atoms: xdnd_enter, position, status, drop, finished, selection, aware
  - Clipboard via X11 selections (PRIMARY, CLIPBOARD)
  - Window manager hints (WM_DELETE_WINDOW, etc.)

- **Graphics Rendering**:
  - `gl_manager_x11.h/.cpp` — OpenGL (native GLX)
    - GLX context, visual selection, swap interval
  - `gl_manager_x11_egl.h/.cpp` — OpenGL via EGL
    - EGL display, context, surface
  - `rendering_context_driver_vulkan_x11.h/.cpp` — Vulkan on X11
    - VK_KHR_xlib_surface, instance/device init

- **Input Handling** (`x11/key_mapping_x11.h/.cpp`):
  - X11 events: KeyPress, KeyRelease, ButtonPress, ButtonRelease, MotionNotify
  - Keyboard: XKeyEvent, key symbols via XKeysymToKeycode
  - Mouse: Button 1-5 (scroll wheel), motion with pointer state
  - Touch: XI2 (XInput2) multi-touch events (TouchBegin, Update, End)
  - Pen/Tablet: XInput2 device classes for pressure, tilt, buttons
  - IME: XIM (X Input Method) via XIC, composition string
  - Clipboard: XSelection, conversion atoms (UTF8_STRING, TARGETS)
  - Gamepad: Via SDL (JoypadSDL)

- **Dynamic Linking** (`x11/dynwrappers/`):
  - `xlib-so_wrap.h` — Dynamic XLib symbols (for optional X11)
  - `xcursor-so_wrap.h` — Xcursor symbols (custom cursors)
  - `xinput2-so_wrap.h` — XInput2 symbols (touch, pen, multi-device)
  - `xrandr-so_wrap.h` — XRandR symbols (monitor enumeration, DPMS)
  - `xinerama-so_wrap.h` — Xinerama (multi-monitor legacy)
  - `xrender-so_wrap.h` — XRender (rendering acceleration)
  - `xext-so_wrap.h` — XExt (misc extensions)
  - `xkbcommon-so_wrap.h` — XKBCommon (keyboard layout)

- **Graphics Detection**:
  - `detect_prime_x11.h/.cpp` — PRIME GPU offloading detection (NVIDIA Optimus, AMD dGPU)
    - Checks for Nouveau/NVIDIA drivers with PRIME capability

##### **Wayland Display Server** (`wayland/` subdirectory, 15+ files)

- `display_server_wayland.h / display_server_wayland.cpp` — Main Wayland compositor protocol
  - Uses xdg-shell protocol (windows, popups, menus)
  - Uses wp_viewporter for viewport scaling
  - Libdecor for window decoration (client-side)

- **Graphics Rendering**:
  - `egl_manager_wayland.h/.cpp` — EGL on Wayland
    - EGL display via wl_egl_window, context, surface
  - `egl_manager_wayland_gles.h/.cpp` — GLES3 specific wrapper
  - `rendering_context_driver_vulkan_wayland.h/.cpp` — Vulkan on Wayland
    - VK_KHR_wayland_surface

- **Input Handling** (`wayland/key_mapping_xkb.h/.cpp`):
  - XKBCommon for keyboard layout handling
  - Wayland wl_keyboard events: keymap, enter, leave, key
  - Wayland wl_pointer events: enter, leave, motion, button, axis (scroll)
  - Touch via wl_touch: down, up, motion, frame
  - Pen/Tablet via tablet-v2 protocol

- **Wayland Protocol Integration**:
  - `wayland_thread.h/.cpp` — Wayland event loop thread
  - `wayland_embedder.h/.cpp` — Wayland surface composition for embedded use
  - `libdecor-so_wrap.h` — Dynamic Libdecor symbols (window decoration)
  - `wayland-client-core-so_wrap.h` — Core Wayland protocol
  - `wayland-cursor-so_wrap.h` — Cursor themes
  - `wayland-egl-core-so_wrap.h` — Wayland-EGL integration

- **Graphics Detection**:
  - `detect_prime_egl.h/.cpp` — PRIME detection on Wayland via EGL

#### Shared Core Services (Linux/BSD)

- **Audio System**:
  - `drivers/alsa/audio_driver_alsa.h/.cpp` — ALSA (Advanced Linux Sound Architecture)
    - PCM device enumeration, capture/playback
  - `drivers/pulseaudio/audio_driver_pulseaudio.h/.cpp` — PulseAudio daemon
    - Server connection, volume control, device routing
  - Both use dynamic symbol wrapping (`asound-so_wrap.h`, `pulse-so_wrap.h`)

- **MIDI**:
  - Via drivers/unix or platform-specific (X11/Wayland inherit SDL joystick)

- **Text-to-Speech**:
  - `tts_linux.h / tts_linux.cpp` — Speech Dispatcher (speechd)
    - Voice selection, rate/volume control

- **OS Services & System Integration**:
  - **File System** (in drivers/unix):
    - `file_access_unix.h/.cpp` — File I/O
    - `dir_access_unix.h/.cpp` — Directory enumeration
    - `file_access_unix_pipe.h/.cpp` — Pipe/subprocess I/O

  - **Networking** (in drivers/unix):
    - `net_socket_unix.h/.cpp` — POSIX sockets (TCP, UDP)
    - `ip_unix.h/.cpp` — DNS resolution, IP utilities

  - **Process Management**:
    - fork/exec for subprocess spawning
    - Signal handling (SIGCHLD for process exit)

  - **Paths & Directories**:
    - `$HOME/.config/godot` — config_path
    - `$HOME/.local/share/godot` — data_path
    - `$HOME/.cache/godot` — cache_path
    - `/tmp` — temp_path
    - XDG Base Directory spec compliance

  - **Threading** (in drivers/unix):
    - `thread_posix.h/.cpp` — pthreads (pthread_create, pthread_join)
    - Mutex/Semaphore via pthread_mutex, pthread_cond
    - TLS via pthread_key_create

  - **Font System**:
    - `fontconfig-so_wrap.h` — Fontconfig for font discovery
    - System font enumeration and fallback chains

- **FreeDesktop Integration** (D-Bus services):
  - `freedesktop_portal_desktop.h/.cpp` — XDG Desktop Portal
    - File open/save dialogs (sandboxing-aware)
    - Settings queries (dark mode, cursor theme)
  - `freedesktop_screensaver.h/.cpp` — Screensaver/DPMS control
    - Screen on/off, inhibit screensaver (keep_screen_on)
  - `freedesktop_at_spi_monitor.h/.cpp` — AT-SPI (Accessibility)
    - Screen reader integration

- **Crash Handling**:
  - `crash_handler_linuxbsd.h/.cpp` — Signal handlers (SIGSEGV, SIGABRT, etc.)
  - Backtrace via backtrace library

- **Native Menu**:
  - Per-display-server (X11 or Wayland), likely via GTK or similar

---

## MACOS Platform (`/platform/macos/`)

### **Files: 36 .h files** (Objective-C++ headers, mostly .mm in implementation)

**Note**: macOS platform files are predominantly Objective-C headers (.h) with .mm implementations. File structure is abstracted.

#### Core OS Layer
- `os_macos.h` — OS abstraction
- `godot_application.h` — NSApplication delegate

#### Window & Display Management
- `display_server_macos.h` — Main Cocoa window management
- `display_server_macos_base.h` — Base class (shared logic)
- `display_server_macos_embedded.h` — Embedded game view support

- **Cocoa UI Components**:
  - `godot_window.h` — NSWindow subclass
  - `godot_window_delegate.h` — NSWindowDelegate
  - `godot_content_view.h` — Main game view (NSView)
  - `godot_button_view.h` — Button handling
  - `godot_application_delegate.h` — NSApplicationDelegate

#### Input Handling
- `key_mapping_macos.h` — NSEvent key mapping to Godot codes
- Mouse/Touch: Via NSEvent (NSLeftMouseDown, NSLeftMouseDragged, etc.)
- Trackpad: Gesture support
- IME: Cocoa input manager

#### Rendering Context & Graphics
- `gl_manager_macos_angle.h` — OpenGL via ANGLE
- `gl_manager_macos_legacy.h` — OpenGL legacy (older macOS)
- `rendering_context_driver_vulkan_macos.h` — Metal Vulkan layer (MoltenVK)
- `embedded_gl_manager.h` — GL for embedded view

#### Audio System
- `drivers/coreaudio/audio_driver_coreaudio.h` — Core Audio framework
  - AudioUnit, AudioSession, device enumeration

#### MIDI
- `drivers/coremidi/` — Core MIDI framework

#### Text-to-Speech
- `tts_macos.h` — NSSpeechSynthesizer

#### Native UI
- `native_menu_macos.h` — NSMenu integration
- `godot_menu_delegate.h`, `godot_menu_item.h` — Menu handling
- `godot_open_save_delegate.h` — NSSavePanel, NSOpenPanel

#### Status/Tray
- `godot_status_item.h` — NSStatusBar (system tray)
- `godot_progress_view.h` — Progress indicator in dock

#### System Integration
- `dir_access_macos.h` — Directory enumeration
- `crash_handler_macos.h` — Signal handlers
- `macos_quartz_core_spi.h` — Quartz Core SPI (undocumented APIs)

#### Embedded Mode
- `editor/embedded_game_view_plugin.h` — Game view for editor integration
- `editor/embedded_process_macos.h` — Process wrapper
- `embedded_debugger.h` — Debugger integration

#### Utilities
- `godot_core_cursor.h` — Cursor management
- `platform_config.h`, `platform_gl.h`, `platform_thread.h` — Config
- `key_mapping_macos.h` — Key mapping

---

## ANDROID Platform (`/platform/android/`)

### **Files: 62 .h/.cpp + 8 Java subdirectories**

#### Core OS Layer & JNI Bridge
- `os_android.h / os_android.cpp` — OS abstraction
- `java_godot_wrapper.h/.cpp` — JNI bridge to main GodotActivity
- `java_godot_io_wrapper.h/.cpp` — JNI bridge to I/O services
- `java_godot_view_wrapper.h/.cpp` — JNI bridge to game view (GLSurfaceView)
- `java_godot_lib_jni.h/.cpp` — Main JNI entry point (JNI_OnLoad, etc.)
- `jni_utils.h/.cpp` — JNI helper macros/functions
- `api/jni_singleton.h/.cpp` — Singleton pattern over JNI
- `api/java_class_wrapper.h/.cpp` — Dynamic Java class invocation

#### Display & Rendering
- `display_server_android.h / display_server_android.cpp`
  - GLSurfaceView management
  - Multi-window support (overlays, popups)

- **Graphics Rendering**:
  - OpenGL via `gl_manager_android.h` (EGL on Android)
  - Vulkan via `rendering_context_driver_vulkan_android.h/.cpp`
  - ANativeWindow for Vulkan surface

#### Input Handling
- `android_input_handler.h/.cpp` — Master input router
  - Keyboard: KeyEvent dispatch
  - Touch: Multi-touch via MotionEvent (ACTION_DOWN/MOVE/UP)
  - Gamepad: Generic motion events, pressure, axes
  - Buttons: Physical buttons (back, menu, etc.)

- `android_keys_utils.h/.cpp` — Android keycode to Godot mapping
- IME: Soft keyboard integration via InputMethodManager
- Pen/Stylus: Via MotionEvent tool type (TOOL_TYPE_STYLUS)

#### Audio System
- `audio_driver_opensl.h / audio_driver_opensl.cpp` — OpenSL ES
  - Android's native audio API (vs MediaPlayer)
  - PCM playback, recording

#### Text-to-Speech
- `tts_android.h / tts_android.cpp` — TextToSpeech API
  - Android TTS service via Java

#### File System & Storage
- `file_access_android.h/.cpp` — APK asset access
- `file_access_filesystem_jandroid.h/.cpp` — Filesystem I/O via Java wrapper
- `dir_access_jandroid.h/.cpp` — Directory enumeration
- Storage permission handling (READ_EXTERNAL_STORAGE, WRITE_EXTERNAL_STORAGE)

#### Networking
- `net_socket_android.h/.cpp` — Platform-specific socket wrapper
  - May use Java APIs for DNS, network state

#### System Integration
- **Dialogs**:
  - `dialog_utils_jni.h/.cpp` — Native alert dialogs, file pickers

- **Permissions**:
  - Android permission checking and requesting via Java

- **Sensors**:
  - Accelerometer, gyroscope, compass (via SensorManager)

- **Vibration/Haptics**:
  - Vibrator service

- **Process Management**:
  - Thread management via Android's looper/handler
  - `thread_jandroid.h/.cpp` — Android thread wrapper (may wrap Java threads)

- **Threading**:
  - POSIX threads (pthreads) on Android, or JNI threads

#### Java/Gradle Integration
- `java/` subdirectory (8 sub-dirs): Android app source, Gradle build, native configs
  - `java/app/` — GodotActivity, GodotFragment, etc.
  - `java/lib/` — Core library classes
  - `java/editor/` — Editor-only components
  - `java/gradle/` — Gradle templates

#### Export & Plugin System
- `export/export_plugin.h/.cpp` — Gradle export, APK building
- `export/godot_plugin_config.h/.cpp` — Plugin metadata
- `api/api.h/.cpp` — Godot API exposed to plugins
- `godot_plugin_jni.h/.cpp` — Plugin JNI interface

#### Native Menu
- Per-Android UI framework (might be Material or custom)

#### Utilities
- `platform_config.h`, `platform_gl.h` — Config

---

## IOS Platform (`/platform/ios/`)

### **Files: 15 .h files** (Objective-C headers)

**Note**: iOS is a minimal platform that inherits heavily from AppleEmbedded (drivers/apple_embedded/).

#### Core
- `os_ios.h` — Main OS class (inherits from OS_AppleEmbedded)
- `ios.h` — Platform header aggregation

#### Display & Rendering
- `display_server_ios.h` — UIKit display server
- `display_layer_ios.h` — Metal/OpenGL layer management
- `godot_view_ios.h` — UIView game view

#### Graphics
- `platform_gl.h` — OpenGL config
- Metal support via driver (drivers/metal/)

#### Input
- Touch via UITouch (inherent in display_server_ios)
- Gamepad via GCController

#### Shared (AppleEmbedded)
- Audio: Core Audio (drivers/coreaudio/)
- MIDI: Core MIDI (drivers/coremidi/)
- TTS: NSSpeechSynthesizer

#### Utilities
- `device_metrics.h` — Screen DPI, safe area, notch handling
- `api/api.h` — Godot API for iOS plugins
- `platform_config.h`, `platform_thread.h` — Config

---

## WEB Platform (`/platform/web/`)

### **Files: 32 .h/.cpp + 5 JS subdirectories**

#### Core OS Layer
- `os_web.h / os_web.cpp` — OS abstraction for Emscripten
  - Inherits from OS_Unix (POSIX base)
  - PWA (Progressive Web App) support: `pwa_needs_update()`, `pwa_update()`
  - IndexedDB sync: `idb_is_syncing`, `idb_available`, `fs_sync_callback()`

#### Display & Rendering
- `display_server_web.h / display_server_web.cpp` — Canvas/WebGL display
  - HTML5 Canvas element management
  - Fullscreen API support
  - Pointer lock (mouse confinement)

- **Graphics Rendering**:
  - WebGL 2.0 support (GLES3 backend compiles to WebGL2)
  - `godot_webgl2.h` — WebGL 2 specific interfaces

- Platform config: `platform_gl.h`

#### Input Handling
- `godot_js.h` — JavaScript bridge for input events
  - Keyboard: keydown, keyup (via JS events)
  - Mouse: click, mousemove, wheel (via JS)
  - Touch: touchstart, touchmove, touchend (via JS)
  - Gamepad: Gamepad API (navigator.getGamepads())
  - Clipboard: navigator.clipboard API

#### Audio System
- `audio_driver_web.h / audio_driver_web.cpp` — Web Audio API
  - AudioContext, OscillatorNode, GainNode, etc.
  - Multiple audio drivers (e.g., SharedAudioProcessor)

#### MIDI
- `webmidi_driver.h / webmidi_driver.cpp` — Web MIDI API
  - MIDIAccess, port enumeration

#### Networking
- `net_socket_web.h` — WebSocket/HTTP client sockets
- `http_client_web.h / http_client_web.cpp` — HTTP fetch
- `ip_web.h` — DNS (limited on web)

#### File System
- Persistent storage via IndexedDB (JavaScript bridge)
- Temporary storage via FS API
- `file_access_callbacks`: open, close, remove (via JS)
- `fs_sync_callback()` — Sync IndexedDB with in-memory FS

#### System Integration
- **JavaScript Bridge** (`godot_js.h`, `javascript_bridge_singleton.h/.cpp`):
  - Call JavaScript functions from C++
  - JS callbacks to C++
  - Variant marshaling to/from JS

- **Audio Context** (`godot_audio.h`):
  - Web Audio API wrapper

- **Utilities**:
  - `platform_config.h` — Emscripten config

#### JavaScript/Emscripten Integration
- `js/` subdirectory (5 sub-dirs): JavaScript glue code
  - `js/engine/` — Engine initialization
  - `js/libs/` — Third-party JS libraries
  - `js/patches/` — Patches for libraries
  - `js/jsdoc2rst/` — Doc generation

#### Export & Build
- `export/export_plugin.h/.cpp` — Web export template generation
- `editor_http_server.h/.cpp` — Local dev server for testing

#### Platform Specifics
- `godot_midi.h` — Web MIDI specific
- `api/api.h/.cpp` — Web-specific APIs
- `web_tools_editor_plugin.h/.cpp` — Editor integration

---

## CROSS-PLATFORM ARCHITECTURE PATTERNS

### Shared Driver Backends (All Platforms)

Located in `/drivers/` — platform-agnostic or platform-specific:

#### Audio
- **WASAPI** (`drivers/wasapi/`) — Windows exclusive
- **XAudio2** (`drivers/xaudio2/`) — Windows exclusive
- **ALSA** (`drivers/alsa/`) — Linux/BSD
- **PulseAudio** (`drivers/pulseaudio/`) — Linux/BSD
- **CoreAudio** (`drivers/coreaudio/`) — macOS, iOS
- **Web Audio API** (`platform/web/audio_driver_web.h`) — Web

#### Graphics
- **Vulkan** (`drivers/vulkan/`) — Windows, Linux, Android, macOS (via MoltenVK)
- **D3D12** (`drivers/d3d12/`) — Windows exclusive
- **OpenGL** (`drivers/gles3/`) — Cross-platform (via ANGLE, native, or WebGL)
- **Metal** (`drivers/metal/`) — macOS, iOS (via MoltenVK for Vulkan)

#### Networking
- **POSIX Sockets** (`drivers/unix/`) — Linux, BSD, macOS, Android, Web (Emscripten)
- **Windows Sockets** (WinSock) — Windows (in OS_Windows)

#### Threading
- **POSIX Threads** (`drivers/unix/thread_posix.h`) — Linux, BSD, macOS, Android, Web
- **Windows Threads** (in OS_Windows) — Windows (CreateThread, etc.)

#### Input
- **SDL Joystick** (integrated) — Cross-platform gamepad/joystick
- **Platform-specific**: WinTab (Windows), XInput2 (Linux), GameController (macOS/iOS/Android)

### Key Design Patterns

1. **Display Server Abstraction**
   - Each platform implements `DisplayServer` interface
   - Windows: One class (DisplayServerWindows)
   - Linux: Two classes (DisplayServerX11, DisplayServerWayland)
   - macOS: One class (DisplayServerMacOS)
   - Android: One class (DisplayServerAndroid)
   - iOS: One class (DisplayServerIOS)
   - Web: One class (DisplayServerWeb)

2. **Graphics Context Driver Abstraction**
   - `RenderingContextDriver` per platform/backend
   - Vulkan rendering context per platform: _vulkan_{windows,x11,wayland,macos,android}.h
   - OpenGL rendering context per platform: gl_manager_{windows,x11,macos,android}.h
   - Web: Direct WebGL 2 via JavaScript bridge

3. **JNI Bridge Layer** (Android-specific)
   - Heavy use of JNI for OS integration
   - Java wrapper classes for GodotActivity, I/O, files, sensors
   - Bidirectional: C++ calls Java, Java calls C++ (via native methods)

4. **Dynamic Symbol Loading** (Linux/BSD)
   - Optional dependencies loaded at runtime via dlopen/dlsym
   - Libraries: Wayland, X11, Fontconfig, D-Bus, libdecor, etc.
   - Allows binary to run on systems without optional dependencies

5. **Objective-C/C++ Mixed Mode** (macOS/iOS)
   - Platform layer is mostly .mm (Objective-C++) files
   - Headers are .h, implementations are .mm
   - Heavy integration with Cocoa/UIKit frameworks

6. **Emscripten/JavaScript Bridge** (Web)
   - C++ calls JavaScript via Emscripten wrappers
   - IndexedDB for persistent storage (JavaScript-side FS implementation)
   - WASM main loop callback via requestAnimationFrame

### Threading Model

- **Windows**: CreateThread, events, critical sections, condition variables
- **POSIX** (Linux, macOS, Android, BSD, Web): pthread_create, pthread_mutex, pthread_cond
- **Platform-specific**:
  - Android: Can use Java threads via Handler/Looper for UI thread callbacks
  - Web: Emscripten worker threads (SharedArrayBuffer)

### Process & IPC

- **Windows**: CreateProcess, pipes, WaitForSingleObject
- **POSIX**: fork/exec, pipes, wait()
- **Android**: Activity manager, Intent system (via Java)
- **Web**: None (single process, can't spawn subprocesses)

---

## SUMMARY TABLE: Feature Coverage by Platform

| Feature | Windows | Linux/BSD | macOS | Android | iOS | Web |
|---------|---------|-----------|-------|---------|-----|-----|
| **Multi-window** | Yes | Yes | Yes | Yes (overlay) | No | No |
| **Fullscreen** | Yes | Yes | Yes | Yes | Yes (immersive) | Yes (HTML5 API) |
| **Borderless** | Yes | Yes | Yes (native) | N/A | N/A | N/A |
| **Resizable** | Yes | Yes | Yes | N/A | N/A | N/A |
| **Window Minimize** | Yes | Yes | Yes | N/A | N/A | N/A |
| **Always-on-top** | Yes | Yes | Yes | N/A | N/A | N/A |
| **DPI Scaling** | Yes (DPI-aware) | Yes (Xrandr/Wayland) | Retina-aware | Yes (DisplayMetrics) | Yes (native) | Yes (CSS transform) |
| **Multi-monitor** | Yes (HMONITOR) | Yes (Xrandr) | Yes (NSScreen) | N/A | N/A | N/A |
| **Touch Input** | Yes | Limited (XI2) | Yes (trackpad) | Yes (MotionEvent) | Yes (UITouch) | Yes (DOM) |
| **Pen/Tablet** | Yes (WinTab) | Yes (XI2) | Trackpad | Yes (stylus) | Yes (pencil) | No |
| **Gamepad** | Yes (SDL/XInput) | Yes (SDL) | Yes (GCController) | Yes (Android API) | Yes (GCController) | Yes (Gamepad API) |
| **IME/Composition** | Yes | Yes (XIM/fcitx) | Yes | Yes | Yes | Yes (DOM) |
| **Clipboard** | Yes (OLE) | Yes (X11 selection) | Yes (NSPasteboard) | Yes (ClipboardManager) | Yes | Yes (navigator.clipboard) |
| **File Drag-Drop** | Yes (OLE) | Yes (XDND) | Yes (Cocoa) | Limited | Limited | No |
| **WASAPI Audio** | Yes | No | No | No | No | No |
| **ALSA Audio** | No | Yes | No | No | No | No |
| **PulseAudio** | No | Yes | No | No | No | No |
| **CoreAudio** | No | No | Yes | Yes | Yes | No |
| **Web Audio** | No | No | No | No | No | Yes |
| **MIDI** | Yes (WinMIDI) | Yes (ALSA MIDI) | Yes (CoreMIDI) | Via Android API | Yes (CoreMIDI) | Yes (Web MIDI) |
| **TTS** | Yes (SAPI) | Yes (speechd) | Yes (NSSpeechSynthesizer) | Yes (Android TTS) | Yes | No |
| **File I/O** | Win32 API | POSIX | POSIX/Cocoa | Android API/Java | iOS API | IndexedDB/FS API |
| **Networking** | Winsock | POSIX sockets | POSIX sockets | Java sockets | POSIX | WebSocket/Fetch |
| **Process Spawn** | CreateProcess | fork/exec | fork/exec | ActivityManager | Limited | None |
| **Environment Vars** | Windows API | POSIX | POSIX | Java System | Java | JavaScript |
| **System Fonts** | DirectWrite | Fontconfig | Cocoa | Java | iOS API | CSS fonts |
| **Crash Handler** | SEH/signals | Signals | Signals | Java | Signals | N/A |
| **Native Dialogs** | Win32 API | XDG Portal, GTK | Cocoa panels | Android API | iOS API | None |
| **System Theme** | UXTheme API | GTK/Wayland | System prefs | Material Design | System prefs | CSS media query |
| **Screensaver** | DPMS API | org.freedesktop.ScreenSaver | IOKit | Android API | Manual | HTML5 Wake Lock |
| **Virtual GPU** | N/A | PRIME (XRandR) | N/A | N/A | N/A | N/A |

---

## Key UzEngine Lessons

### For Windows
- **WinTab for pen input** is mature and well-documented; strongly prefer it
- **DirectWrite** is the modern way to enumerate and render system fonts
- **OLE Drag-Drop** (DropTargetWindows pattern) is the standard for file/object drop
- **D3D12MA** (Memory Allocator) is essential for efficient GPU memory management
- **Taskbar progress** (ITaskbarList3) is low-cost, high-value UX for loading screens

### For Linux
- **Dual display server support** (X11 + Wayland) is now mandatory
  - X11 still has more driver/app compatibility but is legacy
  - Wayland is the future (GNOME, KDE both default to it)
  - Godot's parallel implementation is the right approach
- **Dynamic symbol loading** enables shipping on systems without optional libs (no ALSA? No Fontconfig? OK!)
- **FreeDesktop Portal** (XDG) is the sandboxing-aware way to do file dialogs
- **XInput2** for multi-touch and pen is standard on modern X11

### For Cross-Platform
- **DisplayServer abstraction** is clean and worth adopting
- **RenderingContextDriver** pattern (per platform/backend) is solid
- **Variant-based scripting bridge** means lightweight serialization of complex objects
- **Platform layer size**: ~40-60 files per major platform is reasonable
- **JNI overhead** is significant on Android; Godot's wrapper pattern is necessary

---

## Files to Reference

For each platform, key files to study:

- **Windows**: `/e/godot/platform/windows/display_server_windows.h` (~1000 lines, comprehensive)
- **Linux/X11**: `/e/godot/platform/linuxbsd/x11/display_server_x11.h` (~1000 lines)
- **Linux/Wayland**: `/e/godot/platform/linuxbsd/wayland/display_server_wayland.h` (~800 lines)
- **macOS**: `/e/godot/platform/macos/display_server_macos.h` (Objective-C++, study with .mm)
- **Android**: `/e/godot/platform/android/display_server_android.h` + Java wrapper layer
- **Web**: `/e/godot/platform/web/display_server_web.h` + `godot_js.h`

---

**End of Inventory**
