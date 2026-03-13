/// UzEngine Native Dialogs — macOS AppKit implementation.
///
/// NSOpenPanel / NSSavePanel are used for file dialogs.
/// NSAlert is used for message boxes.
///
/// Extension lists from DialogFilter are parsed here once and handed to the
/// panel as `allowedFileTypes` (NSArray<NSString*>). The caller-facing API
/// accepts the same semicolon-delimited `*.ext` wildcard syntax used on Windows
/// so that cross-platform call sites are identical.

#if defined(__APPLE__)

#include "native_dialogs.h"
#include "log.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

UZ_DECLARE_LOG_CATEGORY(NativeDialog);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Parses a semicolon-separated wildcard pattern (e.g. `"*.png;*.jpg;*.*"`)
/// into a flat NSArray of bare extension strings (e.g. `["png", "jpg"]`).
///
/// Entries whose extension is `"*"` (match-all wildcard) are skipped; returning
/// them to `allowedFileTypes` would restrict the panel to literally files named
/// `"*"`. When all entries are wildcards the function returns nil, which signals
/// callers to leave the panel unrestricted.
///
/// Returns an autoreleased NSArray, or nil if no usable extensions were found.
[[nodiscard]] NSArray<NSString*>* ParseExtensions(const char* pattern) {
    if (!pattern || pattern[0] == '\0') {
        return nil;
    }

    NSString* nsPattern = [NSString stringWithUTF8String:pattern];
    if (!nsPattern) {
        return nil;
    }

    NSArray<NSString*>* tokens = [nsPattern componentsSeparatedByString:@";"];
    NSMutableArray<NSString*>* exts = [NSMutableArray arrayWithCapacity:[tokens count]];

    for (NSString* token in tokens) {
        NSString* trimmed = [token stringByTrimmingCharactersInSet:
                             [NSCharacterSet whitespaceCharacterSet]];

        // Strip leading "*." from "*.png" → "png".
        // Also accept bare "png" in case the caller omitted the wildcard prefix.
        NSString* ext = trimmed;
        if ([trimmed hasPrefix:@"*."]) {
            ext = [trimmed substringFromIndex:2];
        } else if ([trimmed hasPrefix:@"."]) {
            ext = [trimmed substringFromIndex:1];
        }

        // Skip the match-all wildcard — an empty or "*" extension would confuse
        // NSOpenPanel into refusing all files.
        if ([ext length] == 0U || [ext isEqualToString:@"*"]) {
            continue;
        }

        [exts addObject:ext];
    }

    return ([exts count] > 0U) ? [NSArray arrayWithArray:exts] : nil;
}

/// Builds a merged NSArray of all allowed extensions across every DialogFilter.
///
/// Returns nil (no restriction) when any filter contains a match-all wildcard,
/// since the presence of "*.*" means the dialog should accept every file type.
[[nodiscard]] NSArray<NSString*>* BuildAllowedTypes(
    const DialogFilter* filters, int filterCount)
{
    if (!filters || filterCount <= 0) {
        return nil;
    }

    NSMutableArray<NSString*>* merged = [NSMutableArray array];

    for (int i = 0; i < filterCount; ++i) {
        const char* pattern = filters[i].extensions;
        if (!pattern) {
            continue;
        }

        // If any filter is "*.*" (all files), return nil to lift all restrictions.
        NSString* nsPattern = [NSString stringWithUTF8String:pattern];
        if (nsPattern && [nsPattern rangeOfString:@"*.*"].location != NSNotFound) {
            return nil;
        }

        NSArray<NSString*>* exts = ParseExtensions(pattern);
        if (exts) {
            [merged addObjectsFromArray:exts];
        }
    }

    return ([merged count] > 0U) ? [NSArray arrayWithArray:merged] : nil;
}

/// Converts a UTF-8 directory path into an NSURL suitable for
/// `NSSavePanel.directoryURL`. Returns nil if `dir` is nullptr, empty, or
/// cannot be encoded as UTF-8 by Foundation.
[[nodiscard]] NSURL* DirectoryURL(const char* dir) {
    if (!dir || dir[0] == '\0') {
        return nil;
    }

    NSString* nsPath = [NSString stringWithUTF8String:dir];
    if (!nsPath) {
        UZ_LOG(NativeDialog, Warning,
               "ShowDialog: defaultDir is not valid UTF-8; ignoring directory hint");
        return nil;
    }

    return [NSURL fileURLWithPath:nsPath isDirectory:YES];
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ShowOpenFileDialog — NSOpenPanel
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] FileDialogResult ShowOpenFileDialog(
    const DialogFilter* filters, int filterCount,
    bool multiSelect,
    const char* defaultDir,
    void* /*parentWindow*/)
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        if (!panel) {
            UZ_LOG(NativeDialog, Error,
                   "ShowOpenFileDialog: [NSOpenPanel openPanel] returned nil");
            return FileDialogResult{false, {}};
        }

        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:(multiSelect ? YES : NO)];
        [panel setResolvesAliases:YES];

        NSArray<NSString*>* allowedTypes = BuildAllowedTypes(filters, filterCount);
        if (allowedTypes) {
            // `allowedFileTypes` is deprecated in macOS 12 but remains the
            // broadest-compatible API without requiring UniformTypeIdentifiers.
            [panel setAllowedFileTypes:allowedTypes];
        }

        NSURL* dirURL = DirectoryURL(defaultDir);
        if (dirURL) {
            [panel setDirectoryURL:dirURL];
        }

        const NSModalResponse response = [panel runModal];

        if (response != NSModalResponseOK) {
            // User cancelled or closed the panel — not an error.
            return FileDialogResult{false, {}};
        }

        NSArray<NSURL*>* urls = [panel URLs];
        std::vector<std::string> paths;
        paths.reserve(static_cast<size_t>([urls count]));

        for (NSURL* url in urls) {
            const char* fsPath = [[url path] UTF8String];
            if (fsPath) {
                paths.emplace_back(fsPath);
            }
        }

        if (paths.empty()) {
            UZ_LOG(NativeDialog, Warning,
                   "ShowOpenFileDialog: panel returned OK but URLs list is empty");
            return FileDialogResult{false, {}};
        }

        return FileDialogResult{true, std::move(paths)};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowSaveFileDialog — NSSavePanel
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] FileDialogResult ShowSaveFileDialog(
    const DialogFilter* filters, int filterCount,
    const char* defaultName,
    const char* defaultDir,
    void* /*parentWindow*/)
{
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        if (!panel) {
            UZ_LOG(NativeDialog, Error,
                   "ShowSaveFileDialog: [NSSavePanel savePanel] returned nil");
            return FileDialogResult{false, {}};
        }

        if (defaultName && defaultName[0] != '\0') {
            NSString* nsName = [NSString stringWithUTF8String:defaultName];
            if (nsName) {
                [panel setNameFieldStringValue:nsName];
            }
        }

        NSArray<NSString*>* allowedTypes = BuildAllowedTypes(filters, filterCount);
        if (allowedTypes) {
            [panel setAllowedFileTypes:allowedTypes];
        }

        // Allow the user to freely type any extension when filters impose no
        // restriction, and also when the first filter is a match-all wildcard.
        // `setAllowsOtherFileTypes:YES` prevents AppKit from forcibly appending
        // an extension the user did not type.
        [panel setAllowsOtherFileTypes:(allowedTypes == nil ? YES : NO)];
        [panel setExtensionHidden:NO];

        NSURL* dirURL = DirectoryURL(defaultDir);
        if (dirURL) {
            [panel setDirectoryURL:dirURL];
        }

        const NSModalResponse response = [panel runModal];

        if (response != NSModalResponseOK) {
            return FileDialogResult{false, {}};
        }

        NSURL* url = [panel URL];
        if (!url) {
            UZ_LOG(NativeDialog, Warning,
                   "ShowSaveFileDialog: panel returned OK but URL is nil");
            return FileDialogResult{false, {}};
        }

        const char* fsPath = [[url path] UTF8String];
        if (!fsPath) {
            UZ_LOG(NativeDialog, Error,
                   "ShowSaveFileDialog: could not convert result URL to UTF-8 path");
            return FileDialogResult{false, {}};
        }

        return FileDialogResult{true, {std::string{fsPath}}};
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowMessageBox — NSAlert
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] MessageBoxResult ShowMessageBox(
    const char* title, const char* message,
    MessageBoxButtons buttons,
    MessageBoxIcon icon,
    void* /*parentWindow*/)
{
    @autoreleasepool {
        NSAlert* alert = [[NSAlert alloc] init];
        if (!alert) {
            UZ_LOG(NativeDialog, Error,
                   "ShowMessageBox: [[NSAlert alloc] init] returned nil");
            return MessageBoxResult::Cancel;
        }

        // Title maps to messageText; body maps to informativeText.
        // A nil NSString would crash on setMessageText, so guard both.
        NSString* nsTitle = title ? [NSString stringWithUTF8String:title] : @"";
        NSString* nsMessage = message ? [NSString stringWithUTF8String:message] : @"";
        if (!nsTitle)   { nsTitle   = @""; }
        if (!nsMessage) { nsMessage = @""; }

        [alert setMessageText:nsTitle];
        [alert setInformativeText:nsMessage];

        // Map engine icon enum to NSAlertStyle.
        // NSAlertStyleQuestion does not exist in AppKit; Info is the closest match.
        switch (icon) {
            case MessageBoxIcon::Warning:
                [alert setAlertStyle:NSAlertStyleWarning];
                break;
            case MessageBoxIcon::Error:
                [alert setAlertStyle:NSAlertStyleCritical];
                break;
            case MessageBoxIcon::Info:
            case MessageBoxIcon::Question:
            default:
                [alert setAlertStyle:NSAlertStyleInformational];
                break;
        }

        // Add buttons in the order they should appear left-to-right.
        // NSAlert assigns return codes starting at NSAlertFirstButtonReturn (1000)
        // for the first button added, NSAlertSecondButtonReturn (1001) for the
        // second, and NSAlertThirdButtonReturn (1002) for the third.
        switch (buttons) {
            case MessageBoxButtons::Ok:
                [alert addButtonWithTitle:@"OK"];
                break;

            case MessageBoxButtons::OkCancel:
                [alert addButtonWithTitle:@"OK"];
                [alert addButtonWithTitle:@"Cancel"];
                break;

            case MessageBoxButtons::YesNo:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                break;

            case MessageBoxButtons::YesNoCancel:
                [alert addButtonWithTitle:@"Yes"];
                [alert addButtonWithTitle:@"No"];
                [alert addButtonWithTitle:@"Cancel"];
                break;
        }

        const NSModalResponse response = [alert runModal];

        // Translate NSModalResponse back to the engine's result enum.
        // The mapping depends on which buttons were added above.
        switch (buttons) {
            case MessageBoxButtons::Ok:
                // Only one button; any response is effectively "OK".
                return MessageBoxResult::Ok;

            case MessageBoxButtons::OkCancel:
                if (response == NSAlertFirstButtonReturn)  { return MessageBoxResult::Ok;     }
                if (response == NSAlertSecondButtonReturn) { return MessageBoxResult::Cancel;  }
                return MessageBoxResult::Cancel;

            case MessageBoxButtons::YesNo:
                if (response == NSAlertFirstButtonReturn)  { return MessageBoxResult::Yes; }
                if (response == NSAlertSecondButtonReturn) { return MessageBoxResult::No;  }
                return MessageBoxResult::Cancel;

            case MessageBoxButtons::YesNoCancel:
                if (response == NSAlertFirstButtonReturn)  { return MessageBoxResult::Yes;    }
                if (response == NSAlertSecondButtonReturn) { return MessageBoxResult::No;     }
                if (response == NSAlertThirdButtonReturn)  { return MessageBoxResult::Cancel; }
                return MessageBoxResult::Cancel;
        }

        // Unreachable; silence compiler warning.
        return MessageBoxResult::Cancel;
    }
}

} // namespace uz

#endif // __APPLE__
