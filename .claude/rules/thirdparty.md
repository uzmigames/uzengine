# Thirdparty Dependencies

## ABSOLUTELY FORBIDDEN
- `git clone` into `runtime/thirdparty/`
- `curl`/`wget`/`Download-File` to manually place files in thirdparty

## What To Do Instead
1. Edit `scripts/setup.ps1` — update version/tag/URL
2. Delete target directory: `rm -rf runtime/thirdparty/<lib>`
3. Run setup script: `powershell -ExecutionPolicy Bypass -File scripts/setup.ps1`
4. Verify build: `cd runtime && zig build`
