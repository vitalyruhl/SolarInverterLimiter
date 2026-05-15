# AGENTS.md

Repository guidance for contributors and coding agents.

This repository is `SolarInverterLimiter`, a C++/ESP32 project built with
PlatformIO and the Arduino framework.

## Communication

- Use informal German ("du") when talking to the user.
- Keep explanations short unless detail is explicitly requested.
- After completing work, summarize briefly.
- Internal chat with the user may remain informal German according to these
  communication rules.

## GitHub Text Language

- GitHub Issue titles must be written in English.
- GitHub Issue bodies must be written in English.
- GitHub Pull Request titles and descriptions must be written in English.
- GitHub comments created by agents should be written in English unless the user
  explicitly requests otherwise.
- Do not create German GitHub issue or PR text by default.
- These rules apply only to GitHub and repository artifacts, not to normal chat
  with the user.

## Repository Scope

- Primary project configuration: `platformio.ini`.
- Main source code lives under `src/`.
- Use `include/`, `lib/`, and `test/` when present or introduced by the
  PlatformIO project structure.
- Supporting documentation lives under `docs/` and `readme.md`.
- Wokwi simulation files live under `Wokwi/`.
- Do not assume Python application, Vue UI, database, Excel export, or
  `pyproject.toml` release flows for this repository.

## Agent Routing

- `.github/AGENTS.md` is the canonical source for repository-wide rules.
- `.github/agents/control-plane.agent.md` only routes work to the correct agent.
- Use `.github/agents/refactor.agent.md` for code changes, refactors, tests, and
  build validation.
- Use `.github/agents/workflow.agent.md` for branches, issues, PRs, releases,
  checkpoints, and end-of-session flows.
- Use `.github/agents/docs.agent.md` for documentation work.
- Agent files may add scope-specific rules, but they must not contradict this
  file. If they do, follow this file and report the drift.

## Tracking Policy

- GitHub Issues may be used for tracked work when useful.
- GitHub Pull Requests may be used for review and integration when useful.
- GitHub Project usage is optional and repository-specific.
- Current GitHub Project: `none`
- If Current GitHub Project is `none`, agents must not require project-board
  updates, project item status changes, or project-specific documentation sync.
- If a concrete GitHub Project is configured later, workflow rules may use it for
  issue, PR, and project status coordination.

## Safety Principles

- User changes are sacred. Never revert or overwrite user edits without asking.
- Analyze before modifying files.
- Do not make functional project-code changes unless explicitly requested or
  strictly required to correct governance references.
- Do not invent project rules. Preserve existing project-specific rules unless
  they are clearly obsolete or consciously replaced.
- Do not mark an issue as solved or fixed until the user confirms it works.
- Before rename/delete operations, search references first and update them.
- Before terminal commands that modify the workspace, ensure goal and scope are
  clear.
- If requirements are ambiguous or a change impacts multiple subsystems/files,
  ask 1-3 precise questions or propose 2-3 options before editing.
- Work incrementally in small steps: fix, verify, checkpoint or commit only when
  requested, then continue.

## Autonomy Levels

- Level A, safe: read-only actions such as search, read, and analysis may be done
  immediately.
- Level B, normal: small, clearly scoped changes may be implemented immediately.
- Level C, risky: changes involving settings structure, storage/NVS, OTA,
  security, build pipelines, or large refactors require explicit confirmation.

## Code Rules

- Code comments must be in English.
- Identifiers and function names must be in English.
- Error and log messages must be in English.
- Emojis are forbidden in code, comments, logs, and generated outputs.
- Favor small, coherent changes over broad speculative refactors.
- Keep hardware-facing and configuration-sensitive changes conservative.

## Logging Policy

All source code, headers, examples, tests, and code blocks inside Markdown must
follow this policy.

- Log messages must use short ASCII-only severity tags:
  - `[E]` error
  - `[W]` warning
  - `[I]` info
  - `[D]` debug
  - `[T]` trace
- Long tags such as `[ERROR]`, `[WARNING]`, `[INFO]`, `[DEBUG]`, and `[SUCCESS]`
  are forbidden in code/log output.
- Verbose is not a severity level. Verbose controls whether logs are emitted;
  severity tags describe impact.
- Severity reassignment is allowed when technically justified.
- Log message updates are not public API renames.
- Keep log text semantically clear and as short as reasonably possible to reduce
  flash usage.
- Prefer common abbreviations such as `cfg`, `init`, `ok`, `fail`, `adc`, `pwm`,
  and `io` where clarity remains intact.
- Do not repeat information already encoded by severity tags, module prefixes,
  or surrounding context.
- IO and hardware-level logs are usually `[D]` or `[T]`. Use `[W]` for notable
  recoverable conditions and `[E]` for critical failures.

## Rename And Logging Interaction

- API renames and logging normalization are separate concerns.
- Do not mix logging normalization into API rename phases.
- Before any API rename, perform a full reference search with `rg`.
- After renaming, rerun `rg` to ensure old names do not remain in relevant
  project files such as `src/`, `include/`, `lib/`, `test/`, `docs/`, and
  examples when present.

## Tool Policy

- Prefer available VS Code or agent-workspace tools first when they fit the task.
- Prefer locally installed CLI tools next.
- Do not use unnecessarily heavy tools when simple search or inspection is
  enough.
- Preferred tools:
  - `rg` for text search, audits, and reference checks.
  - `fd` for file discovery, audits, and reference checks.
  - `git` for version-control inspection and explicit version-control actions.
  - `platformio` / `pio` for PlatformIO build, upload, monitor, and test flows.
  - `jc` when structured CLI output is useful.
  - `gh` for GitHub PRs, CI checks, and issues when available.
- If `rg` or `fd` is missing, report that and give a simple install hint instead
  of silently using a weaker search path for audits or reference checks.
- When reporting search results, include the `rg` pattern used.
- On Windows, do not assume `sed` or `awk` are available. Prefer
  PowerShell-native commands unless availability was confirmed.

## Known Local Tools

Known local tools on the user's Windows/PowerShell environment may include:

- `git`
- `gh`
- `rg`
- `fd`
- `jq`
- `dasel`
- `jc`
- `7z`
- `pwsh`
- `winget`
- `choco`
- `coreutils`
- `node` / `npm`
- `python`
- `uv`
- `platformio` / `pio`, if installed for this repository

Rules:

- Prefer VS Code or agent-workspace tools first when suitable.
- Prefer these local CLI tools next when available.
- Do not assume every listed tool is installed on every machine.
- If a required tool is missing, report it clearly and give the command that
  failed.
- Do not install or upgrade tools unless the user explicitly asks.
- On Windows, prefer PowerShell-native commands unless Unix-style tools are
  confirmed available.
- Use `dasel` for YAML, TOML, JSON, or XML inspection and edits when it is the
  safest fit.
- Use `jq` for JSON.
- Use `rg` and `fd` for searches and file discovery.
- Use `gh` for GitHub operations when authenticated and available.
- Use `7z` for archive inspection or extraction when needed.
- Use `uv` only if the repository actually uses Python tooling; do not infer
  Python project workflows from the presence of `uv`.

## Validation Baseline

- Always run at least one PlatformIO build after `.cpp` or `.h` changes.
- Default build check:
  - `pio run -e usb`
- If only Markdown/governance files changed, skip PlatformIO build unless the
  user asks for it.
- If tests are affected, run `pio test` for at least one relevant environment.
- If a required validation cannot run, report that plainly.

## Mandatory Reporting

After file-changing work, report:

- branch used
- files changed
- validation run
- skipped validation with reason
- remaining risks or blockers
