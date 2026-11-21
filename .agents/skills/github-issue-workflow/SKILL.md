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
