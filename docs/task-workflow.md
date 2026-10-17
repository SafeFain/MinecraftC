# Recoverable Task Workflow

These rules complement AGENTS.md. Task state and evidence remain local under the
existing ignore rules; automatic local Git commits follow the completion policy below.

## Start or resume

1. Read AGENTS.md, PLAN.md, PROGRESS.md and TASKS.md; inspect Git status/diff/history
   and the relevant implementation. Preserve unrelated changes.
2. If a completed task remains active, confirm its archive exists, index it, then
   rotate the activity files. Keep unfinished work active unless the user changes scope.
3. For a new task, initialize one plan with goals, non-goals, decisions, stages,
   files, risks, checks and completion criteria; record the matching objective,
   actual state and next actions in PROGRESS.md and TASKS.md.
4. Record the current user instruction and its authorized scope. Apply the Git
   completion policy; do not infer permission to rebase, reset, delete branches or delete documents.
5. Save a checkpoint before substantive code edits:

```bash
python3 tools/task_state.py checkpoint --task task-slug --stage initialized \
  --authorization 'The current user instruction, quoted exactly'
```

Task/stage slugs use lowercase letters, digits, hyphens or underscores. The tool
copies the five root context files, current/staged Git differences, HEAD/branch,
the caller-supplied instruction and a SHA-256 manifest into an immutable dated
directory under `docs/tasks/evidence/task-slug/`. It performs only Git reads.
The quote must be checked against the conversation; the tool does not authenticate it.
Use repeated `--file path/within/workspace` options to preserve task artifacts,
especially new untracked source/tool files that are absent from Git's diff.

## At meaningful milestones

After completing a stage, making a design/scope decision, finding a blocker,
obtaining key validation results, or preparing to switch/compact sessions:

1. Finish a small verifiable unit; inspect the implementation and differences.
2. Update PLAN.md when scope or design changed; update PROGRESS.md with actual
   completion, results, warnings, failures, limits and concrete next actions.
3. Keep failed attempts that explain a constraint or fix; do not conceal them or
   overwrite an unrun check with a claimed pass.
4. Capture a new checkpoint, attaching relevant existing test logs when useful:

```bash
python3 tools/task_state.py checkpoint --task task-slug --stage verified \
  --log /tmp/task-build.log --log /tmp/task-ctest.log --file path/to/new-source.cpp
python3 tools/task_state.py check
```

Snapshots preserve evidence outside the compact active files. Do not embed full
terminal outputs in PLAN.md / PROGRESS.md or load every historical snapshot on resume.
Snapshots reflect their actual capture time; never fabricate earlier checkpoints.

## Complete and rotate

1. Run the applicable configuration, build, complete CTest, difference checks and
   real Vulkan smoke for renderer/UI work. Report warnings and unrun checks separately.
2. Check every completion criterion against the actual implementation and evidence.
   Mark PROGRESS.md completed and save a final-validation checkpoint.
3. Create one `docs/tasks/archive/YYYY-MM-DD-short-task-name.md` containing:
   - Status, start/completion dates, related commits and branch.
   - Goal, final implementation, meaningful changed files and lasting decisions.
   - Useful rejected approaches, actual check commands/results, warnings and limits.
4. Unknown historical values must say `未记录` / `not recorded`. A filename date,
   current branch or nearby commit is not evidence of a historical task date/branch/commit.
   Label retrospective metadata with its original source and preserve original outcomes.
5. Add exactly one row for the archive in TASKS.md; preserve diagnosis-only, local-only,
   historical or other validation boundaries in its status. Promote lasting rules to AGENTS.md.
6. Rotate PLAN.md / PROGRESS.md to idle entries and set TASKS.md Active Task to none.
   Preserve CLAUDE.md and every state entry; do not delete them or force-add ignored
   local task records to a commit without an explicit request.
7. Run `python3 tools/task_state.py check` and `git diff --check`, then capture the
   archived checkpoint. Keep AGENTS.md / PLAN.md / PROGRESS.md within 300/250/150 lines.

The checker rejects missing/duplicate archive indexes, missing archive files/metadata,
inconsistent activity titles/status and overlong root state files. It does not prove
historical authorization, functional correctness or that claimed checks were actually run.
It is an explicit local-workspace tool, not a CI dependency on ignored context files.

## Git completion policy

- The user authorizes an automatic local commit once all goals in the current
  requested scope are complete, required validation has passed, and task records
  have been finalized. No additional confirmation is needed under these conditions.
- Completion means the entire user objective, including all stages and subtasks,
  not merely one turn, one task execution, a checkpoint, or a partial deliverable.
  Do not commit when work is paused, interrupted, blocked, or still has unfinished
  goals or required checks. Preserve changes and recovery checkpoints.
- Include user changes as well as agent changes in the completion commit, including
  pre-existing tracked modifications and non-ignored untracked files. Preserve their
  contents; do not discard or rewrite user work to prepare the commit. Existing
  ignore rules for local task records and generated files continue to apply.
- Inspect the complete staged changes and current branch before committing.
  If the commit fails, retain local work and report the exact remaining limitation.
- Do not push automatically. Push only when the user explicitly requests it.
- Rebase, reset, branch deletion, force pushes, tags and releases require separate
  explicit authorization. More specific user instructions such as “do not commit”
  or an explicit exclusion of files override automatic completion behavior.

## Historical evidence gaps

See `docs/tasks/evidence-gaps.md`. Unrecoverable history remains explicitly unknown;
fresh verification establishes present behavior and cannot prove earlier execution order.
The current instruction authorizes this remediation, not earlier unrelated Git actions.
