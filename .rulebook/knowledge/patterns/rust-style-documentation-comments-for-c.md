# Rust-Style Documentation Comments (/// for C++)

**Category**: code
**Tags**: documentation, code-style, rust-style, comments, api-docs

## Description

All public structs, functions, and methods use triple-slash (///) documentation comments following Rust conventions. Must include: what the function does, parameters with valid ranges/ownership, return value on success AND failure, side effects, thread safety, and code examples for non-obvious usage. Section separators use double-slash with em-dash lines (// ───). Inline comments only explain 'why', never 'what'. Override methods must have their own docs.

## Example

/// Reads an entire file into memory as a byte buffer.\n///\n/// The path is resolved relative to the VFS root directory.\n/// Uses forward slashes as separators (e.g., \"shaders/pbr.uzshader\").\n///\n/// # Returns\n/// - Non-empty vector containing file bytes on success.\n/// - Empty vector if the file does not exist or I/O error occurs.\n///\n/// # Errors\n/// Logs `Asset::Error` on I/O failure with the resolved absolute path.\nstd::vector<uint8_t> Read(const std::string& path) override;

## When to Use

Every public API declaration in header files. Every new struct, class, or function. When modifying existing public APIs.

## When NOT to Use

For private implementation details that are self-explanatory. For trivial getters/setters where the name is sufficient.
