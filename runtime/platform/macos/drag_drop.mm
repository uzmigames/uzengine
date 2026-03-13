/// UzEngine Drag & Drop — AppKit NSDraggingDestination implementation.
///
/// Implements `EnableDragDrop`, `SetDragHoverCallbacks`, and `DisableDragDrop`
/// using a custom `UzDropTargetView` NSView subclass. The view is installed as a
/// transparent overlay on top of the window's content view so it intercepts all
/// dragging protocol calls without disturbing the existing view hierarchy.
///
/// AppKit calls the `NSDraggingDestination` protocol methods automatically from
/// the main run loop — no explicit event pump integration is required.

#if defined(__APPLE__)

#include "drag_drop.h"
#include "log.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

UZ_DECLARE_LOG_CATEGORY(DragDrop);

// ─────────────────────────────────────────────────────────────────────────────
// Internal state — callback records keyed by NSWindow pointer
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// All callback state associated with a single registered window.
struct WindowCallbacks {
    uz::FileDropCallback  onDrop      = nullptr;
    uz::DragHoverCallback onHover     = nullptr;
    uz::DragLeaveCallback onLeave     = nullptr;
    void*                 dropData    = nullptr; ///< userData for onDrop
    void*                 hoverData   = nullptr; ///< userData for onHover/onLeave
};

/// Global map from NSWindow* (as uintptr_t) to callbacks.
/// Accessed only on the main thread — no locking required.
static std::unordered_map<uintptr_t, WindowCallbacks>& GetCallbackMap() {
    static std::unordered_map<uintptr_t, WindowCallbacks> sMap;
    return sMap;
}

/// Converts an AppKit window-local point (bottom-left origin) to a top-left-
/// origin client coordinate. AppKit measures Y from the bottom of the window
/// frame; the engine and Windows platform expect Y from the top.
///
/// # Arguments
/// * `point`       — Point in AppKit window coordinates (bottom-left origin).
/// * `windowHeight` — The height of the window's content view in points.
///
/// # Returns
/// The equivalent point with Y axis flipped to top-left origin.
[[nodiscard]] NSPoint FlipY(NSPoint point, CGFloat windowHeight) {
    return NSMakePoint(point.x, windowHeight - point.y);
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// UzDropTargetView — transparent NSView overlay implementing NSDraggingDestination
// ─────────────────────────────────────────────────────────────────────────────

/// Transparent overlay view that implements NSDraggingDestination.
///
/// Installed as the topmost subview of the window's contentView. It covers the
/// entire content rect with a clear (transparent) background so it does not
/// occlude rendering. The owning NSWindow pointer is stored at init time so
/// the view can look up its callbacks from the global map.
@interface UzDropTargetView : NSView <NSDraggingDestination>

/// Initialise the view with the NSWindow that owns it.
///
/// # Arguments
/// * `window` — The window this view is installed in. Stored as a weak reference
///              (NSWindow is retained by the view hierarchy; the view must not
///              extend its lifetime independently).
- (instancetype)initWithWindow:(NSWindow*)window;

@end

@implementation UzDropTargetView {
    NSWindow* __weak _window; ///< Weak back-reference to the owning window.
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialisation
// ─────────────────────────────────────────────────────────────────────────────

- (instancetype)initWithWindow:(NSWindow*)window {
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        _window = window;
        // Accept file URL dragging types only.
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    }
    return self;
}

// ─────────────────────────────────────────────────────────────────────────────
// NSView overrides — keep the overlay transparent and non-opaque
// ─────────────────────────────────────────────────────────────────────────────

- (BOOL)isOpaque {
    // A non-opaque view lets the compositor pass rendering through to the views
    // below, preserving the engine's rendered output.
    return NO;
}

- (void)drawRect:(NSRect)__unused dirtyRect {
    // No drawing — this view exists solely as a dragging destination.
}

// ─────────────────────────────────────────────────────────────────────────────
// NSDraggingDestination — draggingEntered:
// ─────────────────────────────────────────────────────────────────────────────

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    @autoreleasepool {
        // Accept the drag if any of the items are file URLs.
        NSPasteboard* pasteboard = [sender draggingPasteboard];
        NSArray<NSURL*>* urls = [pasteboard
            readObjectsForClasses:@[[NSURL class]]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];

        if (!urls || [urls count] == 0) {
            return NSDragOperationNone;
        }

        // Fire hover callback for the entry point.
        const uintptr_t key = reinterpret_cast<uintptr_t>(_window);
        auto& map = GetCallbackMap();
        auto it = map.find(key);
        if (it != map.end() && it->second.onHover) {
            const CGFloat height = [self bounds].size.height;
            const NSPoint flipped = FlipY([sender draggingLocation], height);
            it->second.onHover(
                static_cast<float>(flipped.x),
                static_cast<float>(flipped.y),
                it->second.hoverData);
        }

        return NSDragOperationCopy;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NSDraggingDestination — draggingUpdated: (hover in motion)
// ─────────────────────────────────────────────────────────────────────────────

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    @autoreleasepool {
        const uintptr_t key = reinterpret_cast<uintptr_t>(_window);
        auto& map = GetCallbackMap();
        auto it = map.find(key);
        if (it != map.end() && it->second.onHover) {
            const CGFloat height = [self bounds].size.height;
            const NSPoint flipped = FlipY([sender draggingLocation], height);
            it->second.onHover(
                static_cast<float>(flipped.x),
                static_cast<float>(flipped.y),
                it->second.hoverData);
        }

        return NSDragOperationCopy;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NSDraggingDestination — draggingExited: (drag left without dropping)
// ─────────────────────────────────────────────────────────────────────────────

- (void)draggingExited:(nullable id<NSDraggingInfo>)__unused sender {
    @autoreleasepool {
        const uintptr_t key = reinterpret_cast<uintptr_t>(_window);
        auto& map = GetCallbackMap();
        auto it = map.find(key);
        if (it != map.end() && it->second.onLeave) {
            it->second.onLeave(it->second.hoverData);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// NSDraggingDestination — prepareForDragOperation:
// ─────────────────────────────────────────────────────────────────────────────

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)__unused sender {
    // Always accept — validation already happened in draggingEntered:.
    return YES;
}

// ─────────────────────────────────────────────────────────────────────────────
// NSDraggingDestination — performDragOperation: (drop completed)
// ─────────────────────────────────────────────────────────────────────────────

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    @autoreleasepool {
        const uintptr_t key = reinterpret_cast<uintptr_t>(_window);
        auto& map = GetCallbackMap();
        auto it = map.find(key);
        if (it == map.end() || !it->second.onDrop) {
            UZ_LOG(DragDrop, Warning,
                   "performDragOperation: no onDrop callback registered for window {:p}",
                   reinterpret_cast<void*>(key));
            return NO;
        }

        NSPasteboard* pasteboard = [sender draggingPasteboard];
        NSArray<NSURL*>* urls = [pasteboard
            readObjectsForClasses:@[[NSURL class]]
            options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];

        if (!urls || [urls count] == 0) {
            UZ_LOG(DragDrop, Warning,
                   "performDragOperation: pasteboard contained no file URLs");
            return NO;
        }

        uz::DropData dropData;

        // Convert each NSURL to a UTF-8 POSIX path string.
        for (NSURL* url in urls) {
            const char* posixPath = [[url path] UTF8String];
            if (posixPath) {
                dropData.filePaths.emplace_back(posixPath);
            } else {
                UZ_LOG(DragDrop, Warning,
                       "performDragOperation: failed to convert URL to UTF-8 path, skipping");
            }
        }

        if (dropData.filePaths.empty()) {
            UZ_LOG(DragDrop, Error,
                   "performDragOperation: all file URL conversions failed");
            return NO;
        }

        // Flip Y so the engine receives top-left-origin coordinates, matching
        // the Windows platform and the rest of the engine's coordinate system.
        const CGFloat height  = [self bounds].size.height;
        const NSPoint flipped = FlipY([sender draggingLocation], height);
        dropData.mouseX = static_cast<float>(flipped.x);
        dropData.mouseY = static_cast<float>(flipped.y);

        UZ_LOG(DragDrop, Info,
               "performDragOperation: delivering {} file(s) at ({:.1f}, {:.1f})",
               dropData.filePaths.size(), dropData.mouseX, dropData.mouseY);

        it->second.onDrop(dropData, it->second.dropData);
        return YES;
    }
}

@end

// ─────────────────────────────────────────────────────────────────────────────
// uz::EnableDragDrop
// ─────────────────────────────────────────────────────────────────────────────

namespace uz {

[[nodiscard]] bool EnableDragDrop(void* nsWindow, FileDropCallback onDrop, void* userData) {
    @autoreleasepool {
        if (!nsWindow) {
            UZ_LOG(DragDrop, Error, "EnableDragDrop: nsWindow is nullptr");
            return false;
        }
        if (!onDrop) {
            UZ_LOG(DragDrop, Error, "EnableDragDrop: onDrop callback is nullptr");
            return false;
        }

        NSWindow* window = (__bridge NSWindow*)nsWindow;
        NSView* contentView = [window contentView];
        if (!contentView) {
            UZ_LOG(DragDrop, Error,
                   "EnableDragDrop: window has no contentView — cannot install overlay");
            return false;
        }

        // Remove any previously installed overlay to avoid duplicates when
        // called multiple times on the same window.
        for (NSView* subview in [[contentView subviews] copy]) {
            if ([subview isKindOfClass:[UzDropTargetView class]]) {
                [subview removeFromSuperview];
            }
        }

        UzDropTargetView* overlay = [[UzDropTargetView alloc] initWithWindow:window];
        // Size the overlay to fill the entire content view and track resizes via
        // autoresizing masks so no manual resize is needed.
        [overlay setFrame:[contentView bounds]];
        [overlay setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

        // Add as topmost subview so it intercepts all dragging protocol calls.
        [contentView addSubview:overlay positioned:NSWindowAbove relativeTo:nil];

        // Register callbacks.
        const uintptr_t key = reinterpret_cast<uintptr_t>(window);
        auto& map = GetCallbackMap();

        // If SetDragHoverCallbacks was called before EnableDragDrop it wrote its
        // callbacks into a sentinel entry at key 0. Migrate those to the real key
        // now so hover/leave fire correctly from the first drag.
        auto sentinelIt = map.find(0U);
        WindowCallbacks& cb = map[key];
        if (sentinelIt != map.end() && sentinelIt != map.find(key)) {
            cb.onHover   = sentinelIt->second.onHover;
            cb.onLeave   = sentinelIt->second.onLeave;
            cb.hoverData = sentinelIt->second.hoverData;
            map.erase(sentinelIt);
        }

        cb.onDrop   = onDrop;
        cb.dropData = userData;

        UZ_LOG(DragDrop, Info,
               "EnableDragDrop: drag-drop overlay installed for window {:p}",
               nsWindow);
        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// uz::SetDragHoverCallbacks
// ─────────────────────────────────────────────────────────────────────────────

void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave, void* userData) {
    // SetDragHoverCallbacks has no window argument (matching the Windows API).
    // It updates all registered windows — typically there is only one window
    // per process during the editor lifecycle.
    auto& map = GetCallbackMap();
    for (auto& [key, cb] : map) {
        cb.onHover   = onHover;
        cb.onLeave   = onLeave;
        cb.hoverData = userData;
    }

    if (map.empty()) {
        // Called before EnableDragDrop — store in a sentinel entry at key 0 so
        // EnableDragDrop can pick it up. The sentinel is never used for dispatch
        // because real entries always have non-zero NSWindow addresses.
        auto& sentinel = map[0U];
        sentinel.onHover   = onHover;
        sentinel.onLeave   = onLeave;
        sentinel.hoverData = userData;
        UZ_LOG(DragDrop, Warning,
               "SetDragHoverCallbacks: called before EnableDragDrop; "
               "callbacks stored and will be applied on the next EnableDragDrop call");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// uz::DisableDragDrop
// ─────────────────────────────────────────────────────────────────────────────

void DisableDragDrop(void* nsWindow) {
    @autoreleasepool {
        if (!nsWindow) {
            return;
        }

        NSWindow* window = (__bridge NSWindow*)nsWindow;
        NSView* contentView = [window contentView];
        if (contentView) {
            for (NSView* subview in [[contentView subviews] copy]) {
                if ([subview isKindOfClass:[UzDropTargetView class]]) {
                    [subview removeFromSuperview];
                }
            }
        }

        const uintptr_t key = reinterpret_cast<uintptr_t>(window);
        GetCallbackMap().erase(key);

        UZ_LOG(DragDrop, Info,
               "DisableDragDrop: drag-drop overlay removed for window {:p}",
               nsWindow);
    }
}

} // namespace uz

#endif // __APPLE__
