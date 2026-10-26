---
name: pwsh-command-trailing-args-execute
description: Never pass a .ps1 path as an extra argument after `pwsh -Command <text>` — pwsh appends it to the command and executes the script (this ran scripts/setup.ps1 by accident)
metadata:
  type: feedback
---

Never call `pwsh -NoProfile -Command "<script text>" <path-to.ps1>` expecting `$args[0]` to be the path.
`-Command` joins every trailing argument into the command text, so the path becomes a statement and
the .ps1 is executed.

**Why:** On 2026-10-22 a parse-only test (`ParseFile($args[0], ...)`) for scripts/setup.ps1 did this
and ran the full setup script twice: it overwrote 67 tracked files in runtime/thirdparty/slang with
an older Slang release, replaced the local Agility SDK binaries and ran npm install — despite the
explicit "do not run setup.ps1" instruction.

**How to apply:** For PowerShell helpers that inspect a script, write the inspection code to a temp
.ps1 and run it with `-File`, passing paths through environment variables (or `-File` arguments).
Before running anything that names setup.ps1/setup-native.ps1, confirm the invocation cannot execute
it. Also remember a bare `--` is consumed by PowerShell when calling a *function*; quote it ('--')
when forwarding git pathspec separators through a wrapper.
