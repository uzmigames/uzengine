/// UzEngine Drag & Drop Implementation (Linux/X11)
///
/// XDnd protocol version 5 implementation for receiving file drops.
/// Parses `text/uri-list` selections, URL-decodes `file://` URIs,
/// and delivers UTF-8 file paths to the registered FileDropCallback.

#include "drag_drop.h"

#ifdef __linux__

#include "log.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <cctype>
#include <cstring>
#include <sstream>
#include <unordered_map>

UZ_DEFINE_LOG_CATEGORY(DragDrop, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// XDnd Atom Cache
// ─────────────────────────────────────────────────────────────────────────────

/// Lazily-initialized XDnd protocol atoms.
///
/// Internment is deferred until the first `EnableDragDrop` call so that
/// a valid `Display*` is available.
struct XdndAtoms {
    Atom XdndAware       = None;
    Atom XdndEnter       = None;
    Atom XdndPosition    = None;
    Atom XdndStatus      = None;
    Atom XdndDrop        = None;
    Atom XdndLeave       = None;
    Atom XdndFinished    = None;
    Atom XdndSelection   = None;
    Atom XdndActionCopy  = None;
    Atom textUriList     = None;
    Atom uzEngineProp    = None; ///< Scratch property for XConvertSelection target.
};

// ─────────────────────────────────────────────────────────────────────────────
// Per-Window State
// ─────────────────────────────────────────────────────────────────────────────

/// All mutable state associated with one registered window.
struct WindowDragState {
    Display*          display     = nullptr;
    ::Window          window      = 0;

    FileDropCallback  onDrop      = nullptr;
    void*             dropUserData = nullptr;

    DragHoverCallback onHover     = nullptr;
    DragLeaveCallback onLeave     = nullptr;
    void*             hoverLeaveUserData = nullptr;

    // XDnd in-flight state
    ::Window sourceWindow   = 0;      ///< Source window for current drag operation.
    bool     offersUriList  = false;  ///< True if source advertised text/uri-list.
    int      pendingDropTime = 0;     ///< Timestamp from XdndDrop, used in XdndFinished.
};

// ─────────────────────────────────────────────────────────────────────────────
// Module-level State
// ─────────────────────────────────────────────────────────────────────────────

/// One entry per registered window.
static std::unordered_map<::Window, WindowDragState> s_windows;

/// Cached atom set — valid as long as any window is registered.
static XdndAtoms s_atoms;

/// True if s_atoms has been populated for the current display.
static bool s_atomsInited = false;

// ─────────────────────────────────────────────────────────────────────────────
// Atom Initialisation
// ─────────────────────────────────────────────────────────────────────────────

/// Interns all required XDnd atoms.
///
/// # Arguments
/// * `dpy` — Open X11 display connection.
static void InitAtoms(Display* dpy) {
    if (s_atomsInited) return;

    s_atoms.XdndAware      = XInternAtom(dpy, "XdndAware",      False);
    s_atoms.XdndEnter      = XInternAtom(dpy, "XdndEnter",      False);
    s_atoms.XdndPosition   = XInternAtom(dpy, "XdndPosition",   False);
    s_atoms.XdndStatus     = XInternAtom(dpy, "XdndStatus",     False);
    s_atoms.XdndDrop       = XInternAtom(dpy, "XdndDrop",       False);
    s_atoms.XdndLeave      = XInternAtom(dpy, "XdndLeave",      False);
    s_atoms.XdndFinished   = XInternAtom(dpy, "XdndFinished",   False);
    s_atoms.XdndSelection  = XInternAtom(dpy, "XdndSelection",  False);
    s_atoms.XdndActionCopy = XInternAtom(dpy, "XdndActionCopy", False);
    s_atoms.textUriList    = XInternAtom(dpy, "text/uri-list",  False);
    s_atoms.uzEngineProp   = XInternAtom(dpy, "_UZ_DND_DATA",   False);

    s_atomsInited = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// URI Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Decodes a single %XX percent-encoded character.
///
/// # Arguments
/// * `hex` — Two-character hex string (e.g., "20" for space).
///
/// # Returns
/// The decoded byte, or '\0' if `hex` is not valid hex.
static char DecodeHexByte(const char* hex) {
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    const int hi = hexVal(hex[0]);
    const int lo = hexVal(hex[1]);
    if (hi < 0 || lo < 0) return '\0';
    return static_cast<char>((hi << 4) | lo);
}

/// URL-decodes a string (percent-encoding only, no + → space).
///
/// # Arguments
/// * `encoded` — URL-encoded string.
///
/// # Returns
/// Decoded UTF-8 string.
static std::string UrlDecode(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size()) {
            const char decoded = DecodeHexByte(&encoded[i + 1]);
            if (decoded != '\0') {
                result += decoded;
                i += 2;
                continue;
            }
        }
        result += encoded[i];
    }
    return result;
}

/// Parses a `text/uri-list` payload into a list of local file paths.
///
/// The format per RFC 2483:
/// - Lines beginning with `#` are comments.
/// - Each non-comment line is a URI, terminated by `\r\n` or `\n`.
/// - Only `file://` URIs are extracted; others are silently skipped.
/// - The `file://host/path` form strips the (usually empty) host component.
///
/// # Arguments
/// * `data` — Raw bytes of the `text/uri-list` selection.
/// * `length` — Number of bytes in `data`.
///
/// # Returns
/// Vector of absolute UTF-8 file paths. May be empty if no `file://` URIs found.
static std::vector<std::string> ParseUriList(const char* data, size_t length) {
    std::vector<std::string> paths;

    const std::string text(data, length);
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        // Only handle file:// URIs
        constexpr std::string_view kFileScheme = "file://";
        if (line.rfind(kFileScheme, 0) != 0) {
            UZ_LOG(DragDrop, Warning, "Skipping non-file URI: {}", line);
            continue;
        }

        // Strip "file://" prefix
        std::string path = line.substr(kFileScheme.size());

        // Strip optional authority (e.g., "localhost" in "file://localhost/path")
        // Authority ends at the first '/' which begins the path component.
        if (!path.empty() && path[0] != '/') {
            const size_t slashPos = path.find('/');
            if (slashPos == std::string::npos) {
                UZ_LOG(DragDrop, Warning, "Malformed file URI (no path): {}", line);
                continue;
            }
            path = path.substr(slashPos);
        }

        paths.push_back(UrlDecode(path));
    }

    return paths;
}

// ─────────────────────────────────────────────────────────────────────────────
// XDnd Protocol Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Sends an XdndStatus reply to the drag source.
///
/// Informs the source whether we accept the drop and what action we'd perform.
///
/// # Arguments
/// * `dpy`          — X11 display connection.
/// * `targetWindow` — Our window (the target).
/// * `sourceWindow` — The source window to reply to.
/// * `accept`       — Whether we accept the drop.
static void SendXdndStatus(Display* dpy, ::Window targetWindow,
                           ::Window sourceWindow, bool accept) {
    XEvent reply = {};
    reply.type                 = ClientMessage;
    reply.xclient.display      = dpy;
    reply.xclient.window       = sourceWindow;
    reply.xclient.message_type = s_atoms.XdndStatus;
    reply.xclient.format       = 32;
    reply.xclient.data.l[0]    = static_cast<long>(targetWindow);
    // Bit 0: accept drop. Bit 1: send position updates while in a non-action rect.
    reply.xclient.data.l[1]    = accept ? 1L : 0L;
    // Null rectangle (no "don't-send-me-positions-in-this-rect" optimisation)
    reply.xclient.data.l[2]    = 0;
    reply.xclient.data.l[3]    = 0;
    reply.xclient.data.l[4]    = accept ? static_cast<long>(s_atoms.XdndActionCopy) : None;

    XSendEvent(dpy, sourceWindow, False, NoEventMask, &reply);
    XFlush(dpy);
}

/// Sends XdndFinished to the drag source after the drop has been processed.
///
/// # Arguments
/// * `dpy`          — X11 display connection.
/// * `targetWindow` — Our window (the target).
/// * `sourceWindow` — The source window.
/// * `accepted`     — Whether we successfully consumed the drop.
static void SendXdndFinished(Display* dpy, ::Window targetWindow,
                             ::Window sourceWindow, bool accepted) {
    XEvent reply = {};
    reply.type                 = ClientMessage;
    reply.xclient.display      = dpy;
    reply.xclient.window       = sourceWindow;
    reply.xclient.message_type = s_atoms.XdndFinished;
    reply.xclient.format       = 32;
    reply.xclient.data.l[0]    = static_cast<long>(targetWindow);
    // Bit 0: we accepted and performed the action
    reply.xclient.data.l[1]    = accepted ? 1L : 0L;
    reply.xclient.data.l[4]    = accepted ? static_cast<long>(s_atoms.XdndActionCopy) : None;

    XSendEvent(dpy, sourceWindow, False, NoEventMask, &reply);
    XFlush(dpy);
}

// ─────────────────────────────────────────────────────────────────────────────
// XDnd Message Handlers
// ─────────────────────────────────────────────────────────────────────────────

/// Handles XdndEnter: source advertises available MIME types.
///
/// XdndEnter data layout (XEvent.xclient.data.l):
///   l[0] = source window
///   l[1] = (version << 24) | flags — bit 0: more than 3 types
///   l[2..4] = up to 3 MIME type atoms (if bit 0 of l[1] == 0)
///
/// If the source offers more than 3 types, the full list is in the
/// `XdndTypeList` property on the source window.
static void HandleXdndEnter(WindowDragState& state, const XEvent& ev) {
    state.sourceWindow  = static_cast<::Window>(ev.xclient.data.l[0]);
    state.offersUriList = false;

    const long flags    = ev.xclient.data.l[1];
    const bool manyTypes = (flags & 1L) != 0;

    if (manyTypes) {
        // Fetch full type list from source window property
        Atom actualType = None;
        int  actualFormat = 0;
        unsigned long itemCount = 0;
        unsigned long bytesAfter = 0;
        unsigned char* propData = nullptr;

        if (XGetWindowProperty(state.display, state.sourceWindow,
                               XInternAtom(state.display, "XdndTypeList", False),
                               0, 65536, False, XA_ATOM,
                               &actualType, &actualFormat,
                               &itemCount, &bytesAfter, &propData) == Success
            && propData != nullptr) {
            auto* atoms = reinterpret_cast<Atom*>(propData);
            for (unsigned long i = 0; i < itemCount; ++i) {
                if (atoms[i] == s_atoms.textUriList) {
                    state.offersUriList = true;
                    break;
                }
            }
            XFree(propData);
        }
    } else {
        // Up to 3 types encoded directly in the message
        for (int i = 2; i <= 4; ++i) {
            if (static_cast<Atom>(ev.xclient.data.l[i]) == s_atoms.textUriList) {
                state.offersUriList = true;
                break;
            }
        }
    }

    UZ_LOG(DragDrop, Trace, "XdndEnter from window={} offersUriList={}",
           state.sourceWindow, state.offersUriList);
}

/// Handles XdndPosition: source sends current mouse position.
///
/// XdndPosition data layout:
///   l[0] = source window
///   l[2] = (rootX << 16) | rootY  (root-relative coordinates)
///   l[3] = timestamp
///   l[4] = proposed action atom
static void HandleXdndPosition(WindowDragState& state, const XEvent& ev) {
    if (!state.offersUriList) {
        SendXdndStatus(state.display, state.window, state.sourceWindow, false);
        return;
    }

    // Decode root-relative coordinates and translate to window-local
    const long rootCoords = ev.xclient.data.l[2];
    const int rootX = static_cast<int>((rootCoords >> 16) & 0xFFFF);
    const int rootY = static_cast<int>(rootCoords & 0xFFFF);

    int localX = rootX;
    int localY = rootY;
    ::Window child = 0;
    XTranslateCoordinates(state.display,
                          DefaultRootWindow(state.display),
                          state.window,
                          rootX, rootY,
                          &localX, &localY, &child);

    SendXdndStatus(state.display, state.window, state.sourceWindow, true);

    if (state.onHover) {
        state.onHover(static_cast<float>(localX),
                      static_cast<float>(localY),
                      state.hoverLeaveUserData);
    }
}

/// Handles XdndLeave: source cancelled the drag before dropping.
///
/// XdndLeave data layout:
///   l[0] = source window
static void HandleXdndLeave(WindowDragState& state, const XEvent& ev) {
    const ::Window src = static_cast<::Window>(ev.xclient.data.l[0]);
    if (src != state.sourceWindow) return; // Stale leave from a previous drag

    UZ_LOG(DragDrop, Trace, "XdndLeave from window={}", state.sourceWindow);

    state.sourceWindow  = 0;
    state.offersUriList = false;

    if (state.onLeave) {
        state.onLeave(state.hoverLeaveUserData);
    }
}

/// Handles XdndDrop: source requests that we retrieve the dragged data.
///
/// XdndDrop data layout:
///   l[0] = source window
///   l[2] = timestamp
///
/// We call XConvertSelection to request the data as `text/uri-list`.
/// The X server will deliver a SelectionNotify event when the data is ready.
static void HandleXdndDrop(WindowDragState& state, const XEvent& ev) {
    const ::Window src = static_cast<::Window>(ev.xclient.data.l[0]);
    if (src != state.sourceWindow) {
        UZ_LOG(DragDrop, Warning, "XdndDrop from unexpected source={}", src);
        return;
    }

    if (!state.offersUriList) {
        // Reject — we have nothing to receive
        SendXdndFinished(state.display, state.window, state.sourceWindow, false);
        state.sourceWindow  = 0;
        state.offersUriList = false;
        return;
    }

    // Store drop timestamp for XdndFinished reply
    state.pendingDropTime = static_cast<int>(ev.xclient.data.l[2]);

    // Ask the source to convert the selection to text/uri-list and store
    // it in the _UZ_DND_DATA property on our window.
    XConvertSelection(state.display,
                      s_atoms.XdndSelection,
                      s_atoms.textUriList,
                      s_atoms.uzEngineProp,
                      state.window,
                      static_cast<Time>(state.pendingDropTime));
    XFlush(state.display);

    UZ_LOG(DragDrop, Trace, "XdndDrop received — awaiting SelectionNotify");
}

/// Handles SelectionNotify: the selection data is now available in our property.
///
/// Reads the `_UZ_DND_DATA` property from our window, parses `text/uri-list`,
/// fires the drop callback, then sends `XdndFinished` to the source.
static void HandleSelectionNotify(WindowDragState& state, const XEvent& ev) {
    const auto& sel = ev.xselection;

    // Ignore if this notify isn't for our drag-drop selection
    if (sel.selection != s_atoms.XdndSelection ||
        sel.property  != s_atoms.uzEngineProp) {
        return;
    }

    bool success = false;

    if (sel.property != None) {
        Atom actualType    = None;
        int  actualFormat  = 0;
        unsigned long itemCount   = 0;
        unsigned long bytesAfter  = 0;
        unsigned char* propData   = nullptr;

        const int result = XGetWindowProperty(
            state.display, state.window,
            s_atoms.uzEngineProp,
            0, (65536 / 4),  // max 64 KiB, in 4-byte units
            True,            // delete property after reading
            AnyPropertyType,
            &actualType, &actualFormat,
            &itemCount, &bytesAfter, &propData);

        if (result == Success && propData != nullptr && itemCount > 0) {
            const size_t byteCount =
                static_cast<size_t>(itemCount) * static_cast<size_t>(actualFormat / 8);

            const std::vector<std::string> paths =
                ParseUriList(reinterpret_cast<const char*>(propData), byteCount);

            XFree(propData);

            if (!paths.empty() && state.onDrop) {
                // We don't have exact drop coordinates here (XdndDrop doesn't
                // re-send position). Re-query the pointer position as an approximation.
                int rootX = 0, rootY = 0, winX = 0, winY = 0;
                unsigned int mask = 0;
                ::Window rootReturn = 0, childReturn = 0;
                XQueryPointer(state.display, state.window,
                              &rootReturn, &childReturn,
                              &rootX, &rootY, &winX, &winY, &mask);

                DropData dropData;
                dropData.mouseX    = static_cast<float>(winX);
                dropData.mouseY    = static_cast<float>(winY);
                dropData.filePaths = paths;

                UZ_LOG(DragDrop, Info, "Drop: {} file(s) at ({:.0f}, {:.0f})",
                       paths.size(), dropData.mouseX, dropData.mouseY);

                state.onDrop(dropData, state.dropUserData);
                success = true;
            } else if (paths.empty()) {
                UZ_LOG(DragDrop, Warning,
                       "SelectionNotify: text/uri-list contained no file:// URIs");
            }
        } else {
            if (propData) XFree(propData);
            UZ_LOG(DragDrop, Error,
                   "SelectionNotify: XGetWindowProperty failed (result={})", result);
        }
    } else {
        // sel.property == None means the source refused the conversion
        UZ_LOG(DragDrop, Warning, "SelectionNotify: selection conversion refused by source");
    }

    SendXdndFinished(state.display, state.window, state.sourceWindow, success);

    // Reset in-flight state
    state.sourceWindow  = 0;
    state.offersUriList = false;
    state.pendingDropTime = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool EnableDragDrop(void* nativeWindow, FileDropCallback onDrop, void* userData) {
    if (!nativeWindow || !onDrop) return false;

    const ::Window xwin = reinterpret_cast<::Window>(nativeWindow);

    // Open a display connection for this window
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        UZ_LOG(DragDrop, Error, "EnableDragDrop: XOpenDisplay failed — $DISPLAY not set?");
        return false;
    }

    InitAtoms(dpy);

    // Remove any previous registration for this window
    const auto existing = s_windows.find(xwin);
    if (existing != s_windows.end()) {
        XCloseDisplay(existing->second.display);
        s_windows.erase(existing);
    }

    // Advertise XDnd version 5 support by setting XdndAware on our window
    const long xdndVersion = 5;
    XChangeProperty(dpy, xwin,
                    s_atoms.XdndAware,
                    XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&xdndVersion), 1);
    XFlush(dpy);

    WindowDragState state;
    state.display       = dpy;
    state.window        = xwin;
    state.onDrop        = onDrop;
    state.dropUserData  = userData;

    s_windows.emplace(xwin, std::move(state));

    UZ_LOG(DragDrop, Info, "Drag-drop enabled for window={} (XDnd v5)", xwin);
    return true;
}

void SetDragHoverCallbacks(DragHoverCallback onHover, DragLeaveCallback onLeave,
                           void* userData) {
    // Apply to every registered window (single-window use case is typical)
    for (auto& [_, state] : s_windows) {
        state.onHover            = onHover;
        state.onLeave            = onLeave;
        state.hoverLeaveUserData = userData;
    }
}

void DisableDragDrop(void* nativeWindow) {
    if (!nativeWindow) return;

    const ::Window xwin = reinterpret_cast<::Window>(nativeWindow);
    const auto it = s_windows.find(xwin);
    if (it == s_windows.end()) return; // Never registered — no-op

    WindowDragState& state = it->second;

    // Remove the XdndAware advertisement so sources stop targeting us
    if (state.display && xwin != 0) {
        XDeleteProperty(state.display, xwin, s_atoms.XdndAware);
        XFlush(state.display);
        XCloseDisplay(state.display);
    }

    s_windows.erase(it);

    UZ_LOG(DragDrop, Info, "Drag-drop disabled for window={}", xwin);
}

void ProcessDragDropEvent(void* xevent) {
    if (!xevent) return;
    if (s_windows.empty()) return;

    auto* ev = static_cast<XEvent*>(xevent);

    // Determine which window this event targets
    ::Window targetWindow = 0;

    if (ev->type == ClientMessage) {
        targetWindow = ev->xclient.window;
    } else if (ev->type == SelectionNotify) {
        targetWindow = ev->xselection.requestor;
    } else {
        return; // Not an XDnd-relevant event type
    }

    const auto it = s_windows.find(targetWindow);
    if (it == s_windows.end()) return; // Event for an unregistered window

    WindowDragState& state = it->second;

    if (ev->type == SelectionNotify) {
        HandleSelectionNotify(state, *ev);
        return;
    }

    // All remaining relevant events are ClientMessage
    const Atom msgType = ev->xclient.message_type;

    if (msgType == s_atoms.XdndEnter) {
        HandleXdndEnter(state, *ev);
    } else if (msgType == s_atoms.XdndPosition) {
        HandleXdndPosition(state, *ev);
    } else if (msgType == s_atoms.XdndLeave) {
        HandleXdndLeave(state, *ev);
    } else if (msgType == s_atoms.XdndDrop) {
        HandleXdndDrop(state, *ev);
    }
    // All other ClientMessage types (WM_DELETE_WINDOW, etc.) are ignored.
}

} // namespace uz

#endif // __linux__
