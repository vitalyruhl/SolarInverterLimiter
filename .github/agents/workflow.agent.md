# Workflow Agent

Purpose:
Provide repository workflow rules for branches, issues, PRs, checkpoints,
release branches, and end-of-session handling.

This agent must apply `.github/AGENTS.md`.

## Branch Model

- `main` is the published/released branch.
- `release/*` branches are runnable snapshot branches and must stay buildable and
  runnable.
- `release/*` branches are versioned by release, for example `release/v4.0.0`
  or `release/v4.1.0`.
- `feature/*` branches are work-in-progress branches and may be unfinished or
  temporarily broken.
- Work on one side branch at a time.
- Do not change `main` directly.
- Docs-only TODO updates under `docs/todo.md` or `docs/todo_*.md` may be
  committed directly to `main` when the user explicitly requests that workflow.

## Git Command Rules

- Read-only git commands may be run without asking:
  - `git status`
  - `git diff`
  - `git log`
  - `git show`
  - `git branch`
  - `git remote -v`
- Commands that modify history or working tree require confirmation:
  - `git add`
  - `git commit`
  - `git switch` / `git checkout`
  - `git reset`
  - `git merge`
  - `git rebase`
  - `git clean`
  - `git stash`
  - `git cherry-pick`
- Stage, commit, and push only on explicit user request or when a named workflow
  explicitly requires it.
- If staging is requested, prefer `git add -A`.
- Do not prepend `Set-Location` to git commands. Use the configured working
  directory. For non-git commands that must change directory, use
  `Push-Location` / `Pop-Location`.

## Branch Safety

- Before multi-file refactors or risky changes, ensure the current baseline is
  understood and either clean, committed, or intentionally dirty by user request.
- Work incrementally: fix, verify, checkpoint or commit only when requested, then
  continue.
- Verify the active branch matches the task.
- If the active branch is `main` or `master`, emit a `[W]` warning before
  file-changing work.
- If branch naming does not match the task, warn and propose suitable branch
  names instead of silently switching.
- Never revert user edits unless explicitly asked.

## GitHub Workflow

- Prefer `gh` for PRs, CI checks, and issues when available.
- Keep PRs scoped to one coherent change.
- Do not claim merge readiness without running or reporting the relevant
  validation.
- GitHub Issue titles and bodies created by agents must be written in English.
- GitHub Pull Request titles and descriptions created by agents must be written
  in English.
- GitHub comments created by agents should be written in English unless the user
  explicitly requests otherwise.
- Internal chat with the user may remain informal German; do not use that as a
  reason to create German GitHub issue or PR text by default.
- Use GitHub Issues or PRs as task tracking when the user asks for tracked
  workflow, but do not invent mandatory project-board rules for this repository.
- GitHub Project usage is optional and controlled by `.github/AGENTS.md`
  `Tracking Policy`.
- When `Current GitHub Project` is `none`:
  - do not require project-board actions
  - do not report missing project-board state as a blocker
  - do not require project item status changes
  - do not require documentation or TODO synchronization into a project board
- When a concrete GitHub Project is configured later, workflow rules may use it
  as an optional coordination mechanism for issue, PR, and project status.
- Issues and PRs remain allowed regardless of GitHub Project configuration.

## PlatformIO Workflow

- Default build validation:
  - `pio run -e usb`
- OTA environment validation, when explicitly relevant:
  - `pio run -e ota`
- Upload commands require explicit user request because they interact with
  hardware or network devices.
- Serial monitor commands require explicit user request because they attach to
  local devices.
- If no `.cpp` or `.h` files changed, skip PlatformIO build unless requested.

## Release Branch Workflow

- `release/*` branches should represent runnable snapshots.
- Prefer fast-forward updates when moving a release branch to a verified feature
  state.
- If fast-forward is not possible, ask explicitly before force-pushing. Prefer
  `--force-with-lease` if force-push is approved.
- Do not invent semantic-version or `pyproject.toml` release steps for this
  repository unless the repository later adds such files and policy.

## End-Of-Session Workflow

Trigger phrases:

- `ich mach jetzt feierabend`
- `ich will jetzt feierabend machen`

Treat these as an end-of-session cue, not as an implicit release-promotion
request.

Default behavior:

1. Inspect repository state.
2. If there are coherent changes on a non-`main` work branch, use
   `workflow.checkpoint` semantics when the user requested a checkpoint or an
   end-of-session save.
3. If `.cpp` or `.h` files changed, run `pio run -e usb` before claiming the
   checkpoint is validated.
4. If only Markdown or governance files changed, skip PlatformIO build and report
   that reason.
5. Do not update `release/*` or `main` implicitly.
6. Use `workflow.ready` only when the user asks to prepare validated work for
   review or integration.

If the active branch is `main` or `master` during this workflow:

- Emit a `[W]` warning.
- Ask before committing directly to `main`.

## Workflow Shortcuts

These names describe expected intent if the user invokes them:

- `workflow.begin`: prepare the correct branch for a new task.
- `workflow.checkpoint`: create a commit and push the current coherent state.
- `workflow.docs`: perform a narrow documentation-only synchronization.
- `workflow.audit`: read-only workflow or repository-state audit.
- `workflow.ship`: build and verify artifacts without implicit merge.
- `workflow.ready`: prepare work for review or integration.
- `workflow.toMain`: get validated work onto `main` through the agreed workflow.
- `workflow.cleanBranches`: delete only branches verified as integrated.
- `workflow.end`: end the session safely without claiming merge or fix success.

Shortcut behavior must remain conservative:

- Inspect repository state first.
- Avoid destructive operations.
- Report blockers plainly.
- Do not create parallel branch lines for the same work.

## Mandatory Reporting

Report:

- current branch
- files changed when relevant
- git actions performed
- validation run
- validation skipped with reason
- release branch updates if any
- remaining blockers or risks
