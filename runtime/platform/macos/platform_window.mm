/// UzEngine Platform Window Implementation (macOS)
///
/// Native NSWindow creation and Cocoa event handling.
/// Creates an NSWindow with a CAMetalLayer-backed NSView for Metal rendering.
/// Integrates with the engine logging and profiling systems.
///
/// # Architecture
/// - **NSWindow**: Native Cocoa window with configurable style mask
/// - **UZMetalView**: Custom NSView subclass that uses CAMetalLayer as backing layer
/// - **UZWindowDelegate**: NSWindowDelegate that routes resize/close to PlatformWindow
/// - **Event pump**: PumpMessages() drains NSEvents via nextEventMatchingMask:
///
/// # Rendering Integration
/// Pass GetNativeHandle() to `RHISwapChainDesc::windowHandle` for Metal swapchain.
/// The Metal backend casts back to `NSWindow*` via `__bridge`.

#if defined(__APPLE__)

#include "platform_window.h"
#include "input.h"
#include "log.h"
#include "profiler.h"

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <QuartzCore/CAMetalLayer.h>

/// Log category for window lifecycle and event messages.
UZ_DEFINE_LOG_CATEGORY(Window, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// Friend Callbacks — Bridge between ObjC delegate and C++ PlatformWindow
// ─────────────────────────────────────────────────────────────────────────────

namespace uz {

/// Updates PlatformWindow dimensions and sets the resized flag.
///
/// Called by UZWindowDelegate::windowDidResize: when the user resizes the
/// window or the system triggers a layout change. Accesses private members
/// via friend declaration in platform_window.h.
///
/// # Arguments
/// * `w` - The PlatformWindow to update. Must not be nullptr.
/// * `width` - New content width in pixels.
/// * `height` - New content height in pixels.
void MacOS_NotifyResize(PlatformWindow* w, int32_t width, int32_t height) {
    w->m_width = width;
    w->m_height = height;
    w->m_resized = true;
}

/// Marks the PlatformWindow as closed.
///
/// Called by UZWindowDelegate::windowShouldClose: when the user clicks the
/// close button or Cmd+W. PumpMessages() will return false on next call.
///
/// # Arguments
/// * `w` - The PlatformWindow to close. Must not be nullptr.
void MacOS_NotifyClose(PlatformWindow* w) {
    w->m_open = false;
}

} // namespace uz

// ─────────────────────────────────────────────────────────────────────────────
// macOS Key Code → Windows VK Code Mapping
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a macOS virtual key code (from NSEvent.keyCode / Carbon kVK_*)
/// to a Windows-style virtual key code (VK_*) compatible with uz::Key.
///
/// # Arguments
/// * `macKeyCode` — macOS virtual key code (0x00–0x7F range).
///
/// # Returns
/// The equivalent Windows VK code, or 0 if the key is not mapped.
static uint16_t MapMacKeyToVK(unsigned short macKeyCode) {
    switch (macKeyCode) {
        // Letters
        case kVK_ANSI_A: return 'A';
        case kVK_ANSI_B: return 'B';
        case kVK_ANSI_C: return 'C';
        case kVK_ANSI_D: return 'D';
        case kVK_ANSI_E: return 'E';
        case kVK_ANSI_F: return 'F';
        case kVK_ANSI_G: return 'G';
        case kVK_ANSI_H: return 'H';
        case kVK_ANSI_I: return 'I';
        case kVK_ANSI_J: return 'J';
        case kVK_ANSI_K: return 'K';
        case kVK_ANSI_L: return 'L';
        case kVK_ANSI_M: return 'M';
        case kVK_ANSI_N: return 'N';
        case kVK_ANSI_O: return 'O';
        case kVK_ANSI_P: return 'P';
        case kVK_ANSI_Q: return 'Q';
        case kVK_ANSI_R: return 'R';
        case kVK_ANSI_S: return 'S';
        case kVK_ANSI_T: return 'T';
        case kVK_ANSI_U: return 'U';
        case kVK_ANSI_V: return 'V';
        case kVK_ANSI_W: return 'W';
        case kVK_ANSI_X: return 'X';
        case kVK_ANSI_Y: return 'Y';
        case kVK_ANSI_Z: return 'Z';

        // Numbers
        case kVK_ANSI_0: return '0';
        case kVK_ANSI_1: return '1';
        case kVK_ANSI_2: return '2';
        case kVK_ANSI_3: return '3';
        case kVK_ANSI_4: return '4';
        case kVK_ANSI_5: return '5';
        case kVK_ANSI_6: return '6';
        case kVK_ANSI_7: return '7';
        case kVK_ANSI_8: return '8';
        case kVK_ANSI_9: return '9';

        // Function keys
        case kVK_F1:  return 0x70;
        case kVK_F2:  return 0x71;
        case kVK_F3:  return 0x72;
        case kVK_F4:  return 0x73;
        case kVK_F5:  return 0x74;
        case kVK_F6:  return 0x75;
        case kVK_F7:  return 0x76;
        case kVK_F8:  return 0x77;
        case kVK_F9:  return 0x78;
        case kVK_F10: return 0x79;
        case kVK_F11: return 0x7A;
        case kVK_F12: return 0x7B;

        // Special keys
        case kVK_Space:            return 0x20;
        case kVK_Escape:           return 0x1B;
        case kVK_Return:           return 0x0D;
        case kVK_Tab:              return 0x09;
        case kVK_Delete:           return 0x08; // macOS "Delete" is Backspace
        case kVK_ForwardDelete:    return 0x2E; // VK_DELETE

        // Modifiers
        case kVK_Shift:            return 0xA0; // VK_LSHIFT
        case kVK_RightShift:       return 0xA1; // VK_RSHIFT
        case kVK_Control:          return 0xA2; // VK_LCONTROL
        case kVK_RightControl:     return 0xA3; // VK_RCONTROL
        case kVK_Option:           return 0xA4; // VK_LMENU (Alt)
        case kVK_RightOption:      return 0xA5; // VK_RMENU

        // Arrow keys
        case kVK_UpArrow:          return 0x26;
        case kVK_DownArrow:        return 0x28;
        case kVK_LeftArrow:        return 0x25;
        case kVK_RightArrow:       return 0x27;

        // Navigation
        case kVK_Home:             return 0x24; // VK_HOME
        case kVK_End:              return 0x23; // VK_END
        case kVK_PageUp:           return 0x21;
        case kVK_PageDown:         return 0x22;

        default: return 0;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// UZMetalView — Custom NSView that uses CAMetalLayer as its backing layer
// ─────────────────────────────────────────────────────────────────────────────

/// NSView subclass that provides a CAMetalLayer for GPU rendering.
///
/// Metal requires a CAMetalLayer-backed view to present drawable textures.
/// This view overrides the layer creation methods to ensure the backing
/// layer is always a CAMetalLayer, regardless of the system's default.
///
/// Also handles keyboard and mouse input by forwarding NSEvents to the
/// engine's InputSystem via the On* callback methods.
@interface UZMetalView : NSView

/// Non-owning pointer to the engine's InputSystem for event dispatch.
/// Set by PlatformWindow::SetInputSystem(). Must outlive this view while set.
@property(nonatomic, assign) uz::InputSystem* inputSystem;

@end

@implementation UZMetalView

// ── Layer setup ─────────────────────────────────────────────────────────────

/// Returns YES to opt into layer-backed rendering.
- (BOOL)wantsLayer {
    return YES;
}

/// Returns YES to skip drawRect: and use updateLayer instead.
- (BOOL)wantsUpdateLayer {
    return YES;
}

/// Creates a CAMetalLayer as the backing layer for this view.
///
/// Called once by AppKit when the view first needs its layer. All subsequent
/// rendering goes through this Metal layer.
- (CALayer*)makeBackingLayer {
    CAMetalLayer* layer = [CAMetalLayer layer];
    // Match the view's bounds so Metal drawables are the right size
    layer.contentsScale = self.window.backingScaleFactor;
    return layer;
}

/// Accepts first responder status so the view receives keyboard events.
- (BOOL)acceptsFirstResponder {
    return YES;
}

// ── Keyboard events ─────────────────────────────────────────────────────────

/// Handles key-down events by mapping the macOS key code to a VK code
/// and forwarding to the InputSystem.
- (void)keyDown:(NSEvent*)event {
    if (!self.inputSystem) return;
    uint16_t vk = MapMacKeyToVK([event keyCode]);
    if (vk != 0) {
        self.inputSystem->OnKeyDown(vk);
    }
}

/// Handles key-up events by mapping the macOS key code to a VK code
/// and forwarding to the InputSystem.
- (void)keyUp:(NSEvent*)event {
    if (!self.inputSystem) return;
    uint16_t vk = MapMacKeyToVK([event keyCode]);
    if (vk != 0) {
        self.inputSystem->OnKeyUp(vk);
    }
}

/// Handles modifier flag changes (Shift, Control, Option/Alt, Command).
///
/// macOS sends flagsChanged: instead of keyDown:/keyUp: for modifier keys.
/// We detect press/release by checking whether the modifier bit is set.
- (void)flagsChanged:(NSEvent*)event {
    if (!self.inputSystem) return;

    uint16_t vk = MapMacKeyToVK([event keyCode]);
    if (vk == 0) return;

    // Determine if the modifier is now pressed by checking the flags mask
    NSEventModifierFlags flags = [event modifierFlags];
    bool isDown = false;

    switch ([event keyCode]) {
        case kVK_Shift:
        case kVK_RightShift:
            isDown = (flags & NSEventModifierFlagShift) != 0;
            break;
        case kVK_Control:
        case kVK_RightControl:
            isDown = (flags & NSEventModifierFlagControl) != 0;
            break;
        case kVK_Option:
        case kVK_RightOption:
            isDown = (flags & NSEventModifierFlagOption) != 0;
            break;
        default:
            return;
    }

    if (isDown) {
        self.inputSystem->OnKeyDown(vk);
    } else {
        self.inputSystem->OnKeyUp(vk);
    }
}

// ── Mouse events ────────────────────────────────────────────────────────────

/// Converts an NSEvent's window-relative location to view-relative coordinates
/// with origin at top-left (matching InputSystem's expected coordinate space).
- (NSPoint)viewPointFromEvent:(NSEvent*)event {
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    // NSView has origin at bottom-left; flip Y for top-left origin
    point.y = self.bounds.size.height - point.y;
    return point;
}

- (void)mouseDown:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Left, true);
}

- (void)mouseUp:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Left, false);
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Right, true);
}

- (void)rightMouseUp:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Right, false);
}

- (void)otherMouseDown:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Middle, true);
}

- (void)otherMouseUp:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseButton(uz::MouseButton::Middle, false);
}

- (void)mouseMoved:(NSEvent*)event {
    if (!self.inputSystem) return;
    NSPoint p = [self viewPointFromEvent:event];
    self.inputSystem->OnMouseMove(static_cast<float>(p.x), static_cast<float>(p.y));
}

- (void)mouseDragged:(NSEvent*)event {
    if (!self.inputSystem) return;
    NSPoint p = [self viewPointFromEvent:event];
    self.inputSystem->OnMouseMove(static_cast<float>(p.x), static_cast<float>(p.y));
}

- (void)rightMouseDragged:(NSEvent*)event {
    if (!self.inputSystem) return;
    NSPoint p = [self viewPointFromEvent:event];
    self.inputSystem->OnMouseMove(static_cast<float>(p.x), static_cast<float>(p.y));
}

- (void)otherMouseDragged:(NSEvent*)event {
    if (!self.inputSystem) return;
    NSPoint p = [self viewPointFromEvent:event];
    self.inputSystem->OnMouseMove(static_cast<float>(p.x), static_cast<float>(p.y));
}

- (void)scrollWheel:(NSEvent*)event {
    if (!self.inputSystem) return;
    self.inputSystem->OnMouseScroll(static_cast<float>([event scrollingDeltaY]));
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// UZWindowDelegate — Routes NSWindow lifecycle events to PlatformWindow
// ─────────────────────────────────────────────────────────────────────────────

/// NSWindowDelegate that bridges Cocoa window events to the C++ PlatformWindow.
///
/// Stores a raw pointer to the owning PlatformWindow (non-owning, must outlive
/// this delegate). Routes resize and close events through the friend callback
/// functions declared in platform_window.h.
@interface UZWindowDelegate : NSObject <NSWindowDelegate>

/// Non-owning pointer to the C++ PlatformWindow that owns this delegate.
/// Set during Create(), cleared during Destroy().
@property(nonatomic, assign) uz::PlatformWindow* owner;

@end

@implementation UZWindowDelegate

/// Called when the window finishes resizing (user drag, fullscreen toggle, etc.).
///
/// Reads the new content size from the NSWindow's contentView and forwards
/// it to PlatformWindow via MacOS_NotifyResize().
- (void)windowDidResize:(NSNotification*)notification {
    if (!self.owner) return;

    NSWindow* window = [notification object];
    NSRect contentRect = [[window contentView] frame];
    int32_t newWidth = static_cast<int32_t>(contentRect.size.width);
    int32_t newHeight = static_cast<int32_t>(contentRect.size.height);

    uz::MacOS_NotifyResize(self.owner, newWidth, newHeight);
    UZ_LOG(Window, Trace, "Window resized: {}x{}", newWidth, newHeight);
}

/// Called when the user requests window close (red button, Cmd+W).
///
/// Sets m_open = false via MacOS_NotifyClose(). Returns YES to allow
/// the window to actually close and release its resources.
- (BOOL)windowShouldClose:(NSWindow*)sender {
    if (self.owner) {
        uz::MacOS_NotifyClose(self.owner);
        UZ_LOG(Window, Info, "Window close requested");
    }
    return YES;
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// PlatformWindow Implementation (macOS)
// ─────────────────────────────────────────────────────────────────────────────

namespace uz {

/// Creates a native macOS window with a CAMetalLayer-backed view.
///
/// Initializes NSApplication if not already running, creates an NSWindow
/// with the requested size and style, and attaches a UZMetalView as the
/// content view. The UZWindowDelegate handles resize and close events.
///
/// ObjC objects are retained via CFBridgingRetain() and stored as void*
/// to avoid Objective-C types in the C++ header.
bool PlatformWindow::Create(const WindowDesc& desc) {
    UZ_PROFILE_FUNCTION();

    // Initialize NSApplication singleton (safe to call multiple times)
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Build style mask from WindowDesc flags
    NSWindowStyleMask style = NSWindowStyleMaskTitled
                            | NSWindowStyleMaskClosable
                            | NSWindowStyleMaskMiniaturizable;
    if (desc.resizable) {
        style |= NSWindowStyleMaskResizable;
    }

    NSRect frame = NSMakeRect(0, 0, desc.width, desc.height);

    NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:style
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    if (!window) {
        UZ_LOG(Window, Error, "Failed to create NSWindow");
        return false;
    }

    NSString* nsTitle = [NSString stringWithUTF8String:desc.title];
    [window setTitle:nsTitle];
    [window center];

    // Create Metal-backed content view
    UZMetalView* view = [[UZMetalView alloc] initWithFrame:frame];
    [window setContentView:view];

    // Wire up delegate for resize/close notifications
    UZWindowDelegate* delegate = [[UZWindowDelegate alloc] init];
    delegate.owner = this;
    [window setDelegate:delegate];

    // Make the Metal view first responder so it receives keyboard events,
    // and enable mouse-moved event dispatch without requiring a button press.
    [window makeFirstResponder:view];
    [window setAcceptsMouseMovedEvents:YES];

    // Retain ObjC objects and store as void* for C++ header compatibility.
    // Manual retain/release (MRR) — no ARC in engine code.
    [window retain];
    [view retain];
    [delegate retain];
    m_nsWindow = (__bridge void*)window;
    m_nsView = (__bridge void*)view;
    m_appDelegate = (__bridge void*)delegate;

    m_width = desc.width;
    m_height = desc.height;
    m_open = true;

    if (desc.fullscreen) {
        [window toggleFullScreen:nil];
    }

    UZ_LOG(Window, Info, "Window created: {}x{} ({})", m_width, m_height,
           desc.fullscreen ? "fullscreen" : "windowed");

    return true;
}

/// Destroys the NSWindow and releases all retained ObjC objects.
///
/// Uses CFBridgingRelease() to balance the CFBridgingRetain() calls in Create().
/// The delegate's owner pointer is cleared first to prevent callbacks during teardown.
/// Safe to call if the window was never created or was already destroyed.
void PlatformWindow::Destroy() {
    UZ_PROFILE_FUNCTION();

    if (!m_nsWindow) return;

    // Clear delegate's back-pointer to prevent callbacks during teardown
    UZWindowDelegate* delegate = (__bridge UZWindowDelegate*)m_appDelegate;
    delegate.owner = nullptr;

    NSWindow* window = (__bridge NSWindow*)m_nsWindow;
    [window setDelegate:nil];
    [window close];

    // Release retained ObjC objects (balances retain in Create)
    [(__bridge id)m_appDelegate release];
    [(__bridge id)m_nsView release];
    [(__bridge id)m_nsWindow release];

    m_appDelegate = nullptr;
    m_nsView = nullptr;
    m_nsWindow = nullptr;
    m_open = false;

    UZ_LOG(Window, Info, "Window destroyed");
}

/// Destroys the window if still open.
PlatformWindow::~PlatformWindow() {
    Destroy();
}

/// Drains the Cocoa event queue and dispatches events.
///
/// Polls for all pending NSEvents using nextEventMatchingMask: with
/// a nil date (non-blocking). Each event is sent to [NSApp sendEvent:]
/// for standard Cocoa dispatch (keyboard, mouse, system events).
///
/// # Returns
/// - true if the window is still open after processing all events
/// - false if the window was closed (user clicked close button or Cmd+Q)
bool PlatformWindow::PumpMessages() {
    UZ_PROFILE_FUNCTION();

    @autoreleasepool {
        NSEvent* event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:nil
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]) != nil) {
            [NSApp sendEvent:event];
        }
    }

    return m_open;
}

/// Makes the window visible and brings it to the front.
///
/// Also activates the application (ignoring other apps) to ensure the
/// window receives focus. Required on first show since NSApplication
/// starts inactive when initialized programmatically.
void PlatformWindow::Show() {
    if (!m_nsWindow) return;

    NSWindow* window = (__bridge NSWindow*)m_nsWindow;
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

/// Hides the window without destroying it.
///
/// The window remains allocated and can be shown again via Show().
/// Rendering can continue off-screen if desired.
void PlatformWindow::Hide() {
    if (!m_nsWindow) return;

    NSWindow* window = (__bridge NSWindow*)m_nsWindow;
    [window orderOut:nil];
}

/// Binds an InputSystem to this window and its content view.
///
/// Stores the input pointer in both the C++ member and the UZMetalView's
/// property so that NSEvent handlers can dispatch directly to the InputSystem
/// without an extra lookup through the PlatformWindow.
///
/// # Arguments
/// * `input` — Input system to receive keyboard/mouse events. Pass nullptr to unbind.
void PlatformWindow::SetInputSystem(InputSystem* input) {
    m_input = input;
    if (m_nsView) {
        UZMetalView* view = (__bridge UZMetalView*)m_nsView;
        view.inputSystem = input;
    }
}

} // namespace uz

#endif // __APPLE__
