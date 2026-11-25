"""Format a gcovr --json-summary report as a GitHub Flavored Markdown table."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def _format_percent(value: float | int | None, total: int) -> str:
    if total == 0 or value is None:
        return "—"
    return f"{float(value):.1f}%"


def _format_count(covered: int, total: int) -> str:
    return f"{covered} / {total}"


def format_summary(summary: dict) -> str:
    """Return a Markdown coverage table from parsed gcovr json-summary data."""
    if "files" not in summary or not isinstance(summary["files"], list):
        raise ValueError("Missing or invalid 'files' list in gcovr summary JSON.")

    lines = [
        "### `lib/tandem_core/` Code Coverage",
        "",
        "| File | Lines | Line % | Branches | Branch % |",
        "| :--- | :---: | :---: | :---: | :---: |",
    ]

    for entry in sorted(summary["files"], key=lambda item: str(item.get("filename", ""))):
        filename = str(entry["filename"])
        line_cov = int(entry["line_covered"])
        line_tot = int(entry["line_total"])
        line_pct = _format_percent(entry.get("line_percent"), line_tot)
        branch_cov = int(entry["branch_covered"])
        branch_tot = int(entry["branch_total"])
        branch_pct = _format_percent(entry.get("branch_percent"), branch_tot)
        lines.append(
            f"| `{filename}` | {_format_count(line_cov, line_tot)} | {line_pct} | "
            f"{_format_count(branch_cov, branch_tot)} | {branch_pct} |"
        )

    tot_line_cov = int(summary["line_covered"])
    tot_line_tot = int(summary["line_total"])
    tot_line_pct = _format_percent(summary.get("line_percent"), tot_line_tot)
    tot_branch_cov = int(summary["branch_covered"])
    tot_branch_tot = int(summary["branch_total"])
    tot_branch_pct = _format_percent(summary.get("branch_percent"), tot_branch_tot)
    lines.append(
        f"| **Total** | **{_format_count(tot_line_cov, tot_line_tot)}** | **{tot_line_pct}** | "
        f"**{_format_count(tot_branch_cov, tot_branch_tot)}** | **{tot_branch_pct}** |"
    )
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"Usage: {Path(argv[0]).name} <gcovr-summary.json>", file=sys.stderr)
        return 2

    summary_path = Path(argv[1])
    if not summary_path.is_file():
        print(f"Error: coverage summary file not found: {summary_path}", file=sys.stderr)
        return 1

    try:
        data = json.loads(summary_path.read_text(encoding="utf-8"))
        output = format_summary(data)
    except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
        print(f"Error: failed to parse coverage summary {summary_path}: {exc}", file=sys.stderr)
        return 1

    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
