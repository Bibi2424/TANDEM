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
2. **Check for a parent Feature** using `issue_read` (`method: "get_parent"`).
3. **Verify parent Feature status:**
   - Ensure the parent Feature is at least in **Ready** (or already **In progress**).
   - If it is not in **Ready** (e.g., Backlog or unset), **ask the user** whether it should be moved to **Ready** before starting.

## 2. Starting Work

1. Move the **Task** to **In progress**.
2. Move the **parent Feature** (if any) to **In progress**.
3. Create a dedicated topic branch from `main` (never commit directly to `main`).

## 3. Opening a Pull Request

1. Open the PR against `main` using `Closes #<task>` and `Part of #<feature>` (if applicable).
   - Do **not** merge the PR unless explicitly requested.
2. Move the **Task** to **In review**.

## 4. After the PR is Closed / Merged

A human closes/merges the PR and notifies the agent. Once notified:

1. Move the **Task** to **Done**.
2. Check the **parent Feature** (if any) for remaining open sub-issues using `issue_read` (`method: "get_sub_issues"`):
   - **Remaining open tasks exist:** Move the parent Feature back to **Ready**.
   - **No remaining open tasks:** **Ask the user** whether the parent Feature should be moved to **Done**.

## Tooling Note

- If GitHub Projects v2 MCP tools (or `gh` CLI) are not available in the active environment to update Project board status columns directly, explicitly list the required status transitions for the user to apply (or prompt to enable the `projects` MCP toolset).
