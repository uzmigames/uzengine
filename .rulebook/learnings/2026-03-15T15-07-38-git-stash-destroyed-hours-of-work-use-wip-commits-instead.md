# git stash destroyed hours of work — use WIP commits instead
**Source**: manual
**Date**: 2026-03-15
**Tags**: git, workflow, safety, data-loss, critical
On 2026-03-13, git stash pop failed with merge conflicts and all uncommitted changes were irrecoverably lost. Hours of work destroyed. Root cause: stash entries are not protected by conflict resolution — if pop fails, changes can vanish. Resolution: ABSOLUTELY FORBIDDEN to use git stash in this project. Instead: create WIP commits (git commit -m 'wip: ...') or use temp branches (git checkout -b temp && git commit -am 'wip').