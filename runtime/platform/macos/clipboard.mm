/// UzEngine Clipboard — macOS NSPasteboard implementation.
///
/// Implements ClipboardSetText, ClipboardGetText, ClipboardSetData, and
/// ClipboardGetData using the AppKit NSPasteboard API. Compiled as
/// Objective-C++ (.mm) because NSPasteboard is an AppKit class.
///
/// Binary formats use a "uz.engine.<formatName>" UTI prefix to avoid
/// collisions with system pasteboard types.

#if defined(__APPLE__)

#include "clipboard.h"
#include "log.h"

#include <cstdio>
#include <cstring>

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

UZ_DECLARE_LOG_CATEGORY(Clipboard);
UZ_DEFINE_LOG_CATEGORY(Clipboard, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Builds the UTI type string used to identify a custom binary format on the
/// pasteboard. Prefixes `formatName` with `"uz.engine."` to form a reverse-DNS
/// style identifier (e.g., `"UzEngine/SceneNode"` → `"uz.engine.UzEngine/SceneNode"`).
///
/// Returns an autoreleased NSString; must be called inside an @autoreleasepool.
[[nodiscard]] NSString* BuildUtiType(const char* formatName) {
    return [NSString stringWithFormat:@"uz.engine.%s", formatName];
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardSetText — write UTF-8 text as NSPasteboardTypeString
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool ClipboardSetText(const char* text) {
    @autoreleasepool {
        NSString* nsText = [NSString stringWithUTF8String:text];
        if (!nsText) {
            // The input bytes are not valid UTF-8 — cannot construct NSString.
            UZ_LOG(Clipboard, Error,
                   "ClipboardSetText: failed to convert input to NSString (invalid UTF-8)");
            return false;
        }

        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        const BOOL ok = [pasteboard setString:nsText forType:NSPasteboardTypeString];
        if (!ok) {
            UZ_LOG(Clipboard, Error,
                   "ClipboardSetText: NSPasteboard setString:forType: returned NO");
            return false;
        }

        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardGetText — read NSPasteboardTypeString as UTF-8
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] std::string ClipboardGetText() {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSString* nsText = [pasteboard stringForType:NSPasteboardTypeString];
        if (!nsText) {
            // No string data on the pasteboard — this is expected when the
            // clipboard holds non-text content; not an error worth logging.
            return {};
        }

        const char* utf8 = [nsText UTF8String];
        if (!utf8) {
            UZ_LOG(Clipboard, Error,
                   "ClipboardGetText: UTF8String conversion returned nullptr");
            return {};
        }

        return std::string{utf8};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardSetData — write raw bytes under a custom UTI type
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool ClipboardSetData(const char* formatName, const void* data, size_t size) {
    @autoreleasepool {
        NSString* uti = BuildUtiType(formatName);

        // NSData bridging: a nil data pointer with size 0 is valid — represents
        // an empty entry. Guard against a non-null size with null pointer, which
        // would produce undefined behaviour inside NSData's initialiser.
        NSData* nsData = nil;
        if (size == 0U) {
            nsData = [NSData data];
        } else {
            nsData = [NSData dataWithBytes:data length:size];
            if (!nsData) {
                UZ_LOG(Clipboard, Error,
                       "ClipboardSetData: NSData allocation failed for format '{}'", formatName);
                return false;
            }
        }

        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        // Declare the UTI type so the pasteboard knows what is available.
        [pasteboard declareTypes:@[uti] owner:nil];

        const BOOL ok = [pasteboard setData:nsData forType:uti];
        if (!ok) {
            UZ_LOG(Clipboard, Error,
                   "ClipboardSetData: setData:forType: returned NO for format '{}'", formatName);
            return false;
        }

        return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ClipboardGetData — read raw bytes from a custom UTI type
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] std::vector<uint8_t> ClipboardGetData(const char* formatName) {
    @autoreleasepool {
        NSString* uti = BuildUtiType(formatName);

        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSData* nsData = [pasteboard dataForType:uti];
        if (!nsData) {
            // Format not present on the pasteboard — normal case after a cross-
            // app paste where no UzEngine data was set; not an error.
            return {};
        }

        const auto* bytes = static_cast<const uint8_t*>([nsData bytes]);
        const NSUInteger length = [nsData length];

        if (length == 0U || !bytes) {
            return {};
        }

        return std::vector<uint8_t>{bytes, bytes + length};
    }
}

} // namespace uz

#endif // __APPLE__
