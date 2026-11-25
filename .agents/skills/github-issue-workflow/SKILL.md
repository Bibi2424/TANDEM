---
name: github-issue-workflow
description: >-
  Manages the GitHub Issue, Project board status, and Pull Request lifecycle
  for T.A.N.D.E.M. tasks and parent Features. Use whenever starting a task,
  opening a PR, or when the user notifies that a PR has been closed/merged.
---

# GitHub Issue & PR Lifecycle Workflow

Follow this procedure for every **Task** (and its parent **Feature**, if any) in `Bibi2424/TANDEM`.

## 1. Taking a Task

1. **Assign the task** to `Bibi2424` (and assign the parent Feature if not already assigned).
2. **Check the target Milestone** (see [Section 5](#5-release-milestones)) and ensure the Task is assigned to its release Milestone.
3. **Check for a parent Feature** using `issue_read` (`method: "get_parent"`).
4. **Verify parent Feature status:**
   - Ensure the parent Feature is at least in **Ready** (or already **In progress**).
   - If it is not in **Ready** (e.g., Backlog or unset), **ask the user** whether it should be moved to **Ready** before starting.

## 2. Starting Work

1. Move the **Task** to **In progress**.
2. Move the **parent Feature** (if any) to **In progress**.
3. Create a dedicated topic branch from `main` (never commit directly to `main`).

## 3. Opening a Pull Request

1. **Verify before PR:** Ensure unit tests and affected builds pass, and any hardware-only modifications are verified on physical hardware first (unless the human specifies otherwise).
2. **Ask permission** before committing, pushing, or opening the PR.
3. Open the PR against `main` using `Closes #<task>` and `Part of #<feature>` (if applicable).
   - Do **not** merge the PR unless explicitly requested.
4. GitHub Project automation moves the linked **Task** to **In review** automatically when the PR is opened with `Closes #<task>`.

## 4. After the PR is Closed / Merged

A human closes/merges the PR and notifies the agent. Once notified:

1. Move the **Task** to **Done**.
2. Check the **parent Feature** (if any) for remaining open sub-issues using `issue_read` (`method: "get_sub_issues"`):
   - **Remaining open tasks exist:** Move the parent Feature back to **Ready**.
   - **No remaining open tasks:** **Ask the user** whether the parent Feature should be moved to **Done**.

## Tooling Note (Project Board Status)

- Updating **Ready / In progress / In review / Done** requires **GitHub Projects v2** access (either the `projects` MCP toolset on the `github` MCP server or `gh` CLI with `project` scope).
- Note that `issue_write` and `list_issue_fields` only manage **repository-level** issue fields/assignees/labels, **not** GitHub Projects v2 board columns.
- If neither Projects v2 MCP tools nor `gh` CLI are available in the active environment, the agent **cannot** move cards on the Project board itself — it must state the exact reason (missing Projects v2 tooling) and explicitly list the required board status transitions for the user to apply (or prompt to enable the `projects` MCP toolset / install `gh`).

## 5. Release Milestones

- **Single source of truth:** Native GitHub **Milestones** (e.g., `v0.1.0-alpha`, `v0.2.0-alpha`, `v0.3.0-beta`, `v1.0.0`, `v1.1.0`) track release targeting and feed GitHub Projects v2's built-in `Milestone` grouping (do not add `release:*` labels).
- **What gets a Milestone:**
  - Assign Milestones to **Tasks** and **Bugs**.
  - Leave multi-release umbrella **Features** (`#2`, `#6`, `#7`, `#8`) unmilestoned.
  - For unsplit future **Features** (`#51`, `#64`, `#72`, `#73`), keep a placeholder Milestone on the Feature until it is broken into Tasks — then assign the Milestones to the new Tasks and clear the Feature's Milestone if it spans releases.
- **Tooling:** Assign an existing Milestone via `issue_write` (`milestone` field) or `gh api`; creating or closing Milestones (aligned 1-to-1 with Git tags / GitHub Releases, e.g., archiving `v0.0.0-prototype`) uses `gh api` (`repo` scope).
