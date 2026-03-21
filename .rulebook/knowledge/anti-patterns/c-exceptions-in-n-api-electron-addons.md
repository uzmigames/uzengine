# C++ Exceptions in N-API/Electron Addons

**Category**: code
**Tags**: napi, electron, vscode, exceptions, crash, safety

## Description

Using C++ exceptions in N-API addons loaded into VSCode/Electron. C++ exceptions crash Electron instantly — there is no recovery. NAPI_DISABLE_CPP_EXCEPTIONS must be defined. All error handling in the addon must use return codes, napi_status checks, or napi_throw_error() for JS-visible errors. This also applies to any thirdparty library linked into the addon.

## Example

// WRONG — will crash Electron\ntry {\n    throw std::runtime_error(\"error\");\n} catch (...) { /* never reaches here */ }\n\n// CORRECT — N-API error handling\nnapi_status status = napi_create_int32(env, value, &result);\nif (status != napi_ok) {\n    napi_throw_error(env, nullptr, \"Failed to create int\");\n    return nullptr;\n}

## When to Use

When writing or reviewing any C++ code that runs inside the N-API addon. When linking thirdparty libraries into the addon.
