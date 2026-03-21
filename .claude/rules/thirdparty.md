# Thirdparty Dependencies

## ABSOLUTELY FORBIDDEN
- `git clone` into `runtime/thirdparty/` — NEVER, not even "just to test"
- `curl`/`wget`/`Download-File` to manually place files in thirdparty
- Running `git clone` inline in a Bash tool call to bypass the setup script
- Any manual download of dependencies — ALL deps go through `setup.ps1`

## What To Do Instead
1. Edit `scripts/setup.ps1` — add/update the `Clone-Repo` or `Download-Header` call
2. Tell the user to run the setup script: `powershell -ExecutionPolicy Bypass -File scripts/setup.ps1`
3. **WAIT for the user to confirm** the script ran successfully before proceeding
4. Verify build: `cd runtime && zig build`

## NEVER
- Run the setup script yourself — the user runs it manually
- Skip step 2-3 by cloning directly — setup.ps1 handles cleanup, patching, and directory structure
