"""Render a GitHub-flavored Markdown CI report from JUnit XML, gcovr JSON, and git diff."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET


_DIFF_FILE_RE = re.compile(r"^\+\+\+ b/(.+)$")
_DIFF_HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


@dataclass
class SuiteSummary:
    name: str
    passed: int
    failed: int
    skipped: int
    time_s: float


@dataclass
class FailureDetail:
    suite: str
    test: str
    message: str


@dataclass
class JunitSummary:
    suites: list[SuiteSummary]
    failures: list[FailureDetail]
    total_passed: int
    total_failed: int
    total_skipped: int
    total_time_s: float


@dataclass
class FileCoverage:
    filename: str
    line_covered: int
    line_total: int
    branch_covered: int
    branch_total: int
    covered_lines: set[int]
    uncovered_lines: set[int]

    @property
    def line_percent(self) -> float | None:
        return (self.line_covered * 100.0 / self.line_total) if self.line_total > 0 else None

    @property
    def branch_percent(self) -> float | None:
        return (self.branch_covered * 100.0 / self.branch_total) if self.branch_total > 0 else None


@dataclass
class CoverageReport:
    files: dict[str, FileCoverage]
    line_covered: int
    line_total: int
    branch_covered: int
    branch_total: int

    @property
    def line_percent(self) -> float | None:
        return (self.line_covered * 100.0 / self.line_total) if self.line_total > 0 else None

    @property
    def branch_percent(self) -> float | None:
        return (self.branch_covered * 100.0 / self.branch_total) if self.branch_total > 0 else None


def _norm_path(raw: str) -> str:
    norm = raw.replace("\\", "/").lstrip("./")
    return norm


def _status_badge(percent: float | None, total: int) -> str:
    if total == 0 or percent is None:
        return "⚪"
    if percent >= 90.0:
        return "🟢"
    if percent >= 75.0:
        return "🟡"
    return "🔴"


def _format_percent(percent: float | None, total: int) -> str:
    if total == 0 or percent is None:
        return "—"
    return f"{percent:.1f}%"


def _format_count(covered: int, total: int) -> str:
    return f"{covered} / {total}"


def format_line_ranges(lines: set[int] | list[int], max_ranges: int = 10) -> str:
    """Collapse line numbers into compact ranges like '65, 192–209'."""
    if not lines:
        return "—"
    ordered = sorted(set(lines))
    ranges: list[tuple[int, int]] = []
    start = prev = ordered[0]
    for num in ordered[1:]:
        if num == prev + 1:
            prev = num
        else:
            ranges.append((start, prev))
            start = prev = num
    ranges.append((start, prev))

    formatted: list[str] = []
    for s, e in ranges[:max_ranges]:
        formatted.append(str(s) if s == e else f"{s}–{e}")

    text = ", ".join(f"`{r}`" for r in formatted)
    if len(ranges) > max_ranges:
        remaining = sum((e - s + 1) for s, e in ranges[max_ranges:])
        text += f" … (+{remaining} more)"
    return text


def _safe_int(value: str | None, default: int = 0) -> int:
    try:
        return int(value) if value is not None else default
    except ValueError:
        return default


def _safe_float(value: str | None, default: float = 0.0) -> float:
    try:
        return float(value) if value is not None else default
    except ValueError:
        return default


def parse_junit_xml(path: Path) -> JunitSummary:
    """Parse JUnit XML test suites and failures."""
    tree = ET.parse(path)
    root = tree.getroot()
    if root.tag == "testsuite":
        suite_nodes = [root]
    else:
        suite_nodes = list(root.findall(".//testsuite"))

    suites: list[SuiteSummary] = []
    failures: list[FailureDetail] = []
    total_passed = 0
    total_failed = 0
    total_skipped = 0
    total_time_s = 0.0

    for suite in suite_nodes:
        suite_name = suite.attrib.get("name") or "(unnamed)"
        cases = list(suite.findall("testcase"))
        time_s = _safe_float(suite.attrib.get("time"), 0.0)

        if cases:
            passed = 0
            failed = 0
            skipped = 0
            for tc in cases:
                tc_name = tc.attrib.get("name") or "(unnamed)"
                fail_el = tc.find("failure")
                err_el = tc.find("error")
                skip_el = tc.find("skipped")
                if fail_el is not None or err_el is not None:
                    failed += 1
                    node = fail_el if fail_el is not None else err_el
                    msg = (node.attrib.get("message") or (node.text or "")).strip()
                    msg = " ".join(msg.split())[:140] or "Assertion failed"
                    failures.append(FailureDetail(suite=suite_name, test=tc_name, message=msg))
                elif skip_el is not None:
                    skipped += 1
                else:
                    passed += 1
        else:
            tests = _safe_int(suite.attrib.get("tests"), 0)
            failed = _safe_int(suite.attrib.get("failures"), 0) + _safe_int(
                suite.attrib.get("errors"), 0
            )
            skipped = _safe_int(suite.attrib.get("skipped"), 0)
            passed = max(0, tests - failed - skipped)

        if passed + failed + skipped == 0:
            continue

        suites.append(
            SuiteSummary(
                name=suite_name,
                passed=passed,
                failed=failed,
                skipped=skipped,
                time_s=time_s,
            )
        )
        total_passed += passed
        total_failed += failed
        total_skipped += skipped
        total_time_s += time_s

    return JunitSummary(
        suites=suites,
        failures=failures,
        total_passed=total_passed,
        total_failed=total_failed,
        total_skipped=total_skipped,
        total_time_s=total_time_s,
    )


def parse_gcovr_json(path: Path) -> CoverageReport:
    """Parse gcovr --json output into per-file line and branch coverage."""
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    raw_files = data.get("files")
    if not isinstance(raw_files, list):
        raise ValueError("Missing or invalid 'files' list in gcovr JSON.")

    files: dict[str, FileCoverage] = {}
    tot_line_cov = 0
    tot_line_tot = 0
    tot_branch_cov = 0
    tot_branch_tot = 0

    for entry in raw_files:
        filename = _norm_path(str(entry["file"]))
        line_covered = 0
        line_total = 0
        branch_covered = 0
        branch_total = 0
        covered_lines: set[int] = set()
        uncovered_lines: set[int] = set()

        for line_obj in entry.get("lines", []):
            if line_obj.get("gcovr/excluded") is True:
                continue
            lineno = int(line_obj["line_number"])
            count = int(line_obj.get("count", 0))
            line_total += 1
            if count > 0:
                line_covered += 1
                covered_lines.add(lineno)
            else:
                uncovered_lines.add(lineno)

            for br in line_obj.get("branches", []):
                if br.get("throw") is True:
                    continue
                branch_total += 1
                if int(br.get("count", 0)) > 0:
                    branch_covered += 1

        files[filename] = FileCoverage(
            filename=filename,
            line_covered=line_covered,
            line_total=line_total,
            branch_covered=branch_covered,
            branch_total=branch_total,
            covered_lines=covered_lines,
            uncovered_lines=uncovered_lines,
        )
        tot_line_cov += line_covered
        tot_line_tot += line_total
        tot_branch_cov += branch_covered
        tot_branch_tot += branch_total

    return CoverageReport(
        files=files,
        line_covered=tot_line_cov,
        line_total=tot_line_tot,
        branch_covered=tot_branch_cov,
        branch_total=tot_branch_tot,
    )


def parse_diff_hunks(diff_text: str) -> dict[str, set[int]]:
    """Parse unified=0 git diff text into changed 1-based line numbers per file."""
    changed: dict[str, set[int]] = {}
    current_file: str | None = None

    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            m = _DIFF_FILE_RE.match(line)
            current_file = _norm_path(m.group(1)) if m else None
            continue
        if current_file is None:
            continue
        m = _DIFF_HUNK_RE.match(line)
        if m:
            start = int(m.group(1))
            count = int(m.group(2)) if m.group(2) is not None else 1
            if count > 0:
                changed.setdefault(current_file, set()).update(range(start, start + count))

    return changed


def collect_changed_lines(base_ref: str) -> dict[str, set[int]]:
    """Run git diff against base_ref for lib/tandem_core/ and return changed lines."""
    cmd = ["git", "diff", "--unified=0", f"{base_ref}...HEAD", "--", "lib/tandem_core/"]
    res = subprocess.run(cmd, text=True, capture_output=True, encoding="utf-8", check=False)
    if res.returncode != 0:
        # Fallback to two-dot diff if merge-base is unavailable (e.g., shallow clone).
        fallback_cmd = ["git", "diff", "--unified=0", base_ref, "HEAD", "--", "lib/tandem_core/"]
        res = subprocess.run(
            fallback_cmd, text=True, capture_output=True, encoding="utf-8", check=False
        )
    if res.returncode != 0:
        print(
            f"Warning: git diff against {base_ref} failed: {res.stderr.strip()}",
            file=sys.stderr,
        )
        return {}
    return parse_diff_hunks(res.stdout)


def render_report(
    junit: JunitSummary | None,
    coverage: CoverageReport | None,
    changed_lines: dict[str, set[int]] | None,
    run_url: str | None,
    marker: str | None,
) -> str:
    """Render the complete Markdown report."""
    out: list[str] = []
    if marker:
        out.append(marker)
        out.append("")

    out.append("## Tandem Controls CI — Test & Coverage Report")
    out.append("")

    # Headline summary bullets
    if junit is not None:
        icon = "✅" if junit.total_failed == 0 else "❌"
        skipped_part = f" · `{junit.total_skipped} skipped`" if junit.total_skipped else ""
        out.append(
            f"- **Unit Tests:** {icon} `{junit.total_passed} passed` · "
            f"`{junit.total_failed} failed`{skipped_part} · "
            f"`{len(junit.suites)} suites` ({junit.total_time_s:.2f}s)"
        )

    diff_rows: list[tuple[str, FileCoverage, set[int], set[int]]] = []
    diff_cov_total = 0
    diff_cov_covered = 0
    if coverage is not None and changed_lines is not None:
        for fname in sorted(changed_lines):
            fc = coverage.files.get(fname)
            if fc is None:
                continue
            target_lines = changed_lines[fname]
            cov_hits = target_lines & fc.covered_lines
            uncov_hits = target_lines & fc.uncovered_lines
            inst_count = len(cov_hits) + len(uncov_hits)
            if inst_count > 0:
                diff_cov_covered += len(cov_hits)
                diff_cov_total += inst_count
                diff_rows.append((fname, fc, cov_hits, uncov_hits))

        if diff_cov_total > 0:
            diff_pct = diff_cov_covered * 100.0 / diff_cov_total
            badge = _status_badge(diff_pct, diff_cov_total)
            out.append(
                f"- **PR Diff Coverage:** {badge} **{diff_pct:.1f}%** "
                f"(`{diff_cov_covered} / {diff_cov_total}` changed lines)"
            )
        else:
            out.append(
                "- **PR Diff Coverage:** ⚪ No executable lines changed in `lib/tandem_core/`"
            )

    if coverage is not None:
        l_badge = _status_badge(coverage.line_percent, coverage.line_total)
        b_badge = _status_badge(coverage.branch_percent, coverage.branch_total)
        l_pct = _format_percent(coverage.line_percent, coverage.line_total)
        b_pct = _format_percent(coverage.branch_percent, coverage.branch_total)
        out.append(
            f"- **Total `lib/tandem_core/` Coverage:** "
            f"{l_badge} **{l_pct}** lines (`{coverage.line_covered} / {coverage.line_total}`) · "
            f"{b_badge} **{b_pct}** branches (`{coverage.branch_covered} / {coverage.branch_total}`)"
        )
    else:
        out.append("- **Code Coverage:** ⚪ Skipped (coverage data not generated)")

    out.append("")

    # Failed tests table (prominent when failures exist)
    if junit is not None and junit.failures:
        out.append("### Failed Tests")
        out.append("")
        out.append("| Suite | Test | Details |")
        out.append("| :--- | :--- | :--- |")
        for fail in junit.failures:
            details = fail.message.replace("|", "\\|")
            out.append(f"| `{fail.suite}` | `{fail.test}` | {details} |")
        out.append("")

    # Changed files PR diff table
    if diff_rows:
        out.append("### Changed Files in This PR (`lib/tandem_core/`)")
        out.append("")
        out.append("| Status | File | PR Diff Lines | Total Lines | Branches | Missing (PR) |")
        out.append("| :---: | :--- | :---: | :---: | :---: | :--- |")
        for fname, fc, cov_hits, uncov_hits in diff_rows:
            d_tot = len(cov_hits) + len(uncov_hits)
            d_pct = len(cov_hits) * 100.0 / d_tot
            d_badge = _status_badge(d_pct, d_tot)
            tot_lines = f"{_format_count(fc.line_covered, fc.line_total)} ({_format_percent(fc.line_percent, fc.line_total)})"
            tot_branches = f"{_format_count(fc.branch_covered, fc.branch_total)} ({_format_percent(fc.branch_percent, fc.branch_total)})"
            missing_pr = format_line_ranges(uncov_hits)
            out.append(
                f"| {d_badge} | `{fname}` | `{len(cov_hits)} / {d_tot}` ({d_pct:.1f}%) | "
                f"{tot_lines} | {tot_branches} | {missing_pr} |"
            )
        out.append("")

    # Collapsible Unit Test Suites breakdown
    if junit is not None and junit.suites:
        open_attr = " open" if junit.total_failed > 0 else ""
        out.append(f"<details{open_attr}>")
        out.append(
            f"<summary>Unit Test Suites ({len(junit.suites)} suites — "
            f"{junit.total_passed} passed, {junit.total_failed} failed)</summary>"
        )
        out.append("")
        out.append("| Status | Suite | Passed | Failed | Skipped | Time |")
        out.append("| :---: | :--- | :---: | :---: | :---: | :---: |")
        for s in junit.suites:
            s_icon = "✅" if s.failed == 0 else "❌"
            out.append(
                f"| {s_icon} | `{s.name}` | {s.passed} | {s.failed} | {s.skipped} | {s.time_s:.2f}s |"
            )
        out.append("")
        out.append("</details>")
        out.append("")

    # Collapsible Full Coverage Table
    if coverage is not None and coverage.files:
        out.append("<details>")
        out.append(
            f"<summary>Full <code>lib/tandem_core/</code> Coverage ({len(coverage.files)} files — "
            f"{_format_percent(coverage.line_percent, coverage.line_total)} lines)</summary>"
        )
        out.append("")
        out.append("| Status | File | Lines | Line % | Branches | Branch % | Missing Lines |")
        out.append("| :---: | :--- | :---: | :---: | :---: | :---: | :--- |")
        for fname in sorted(coverage.files):
            fc = coverage.files[fname]
            badge = _status_badge(fc.line_percent, fc.line_total)
            l_pct = _format_percent(fc.line_percent, fc.line_total)
            b_pct = _format_percent(fc.branch_percent, fc.branch_total)
            missing = format_line_ranges(fc.uncovered_lines)
            out.append(
                f"| {badge} | `{fname}` | {_format_count(fc.line_covered, fc.line_total)} | {l_pct} | "
                f"{_format_count(fc.branch_covered, fc.branch_total)} | {b_pct} | {missing} |"
            )
        t_badge = _status_badge(coverage.line_percent, coverage.line_total)
        t_l_pct = _format_percent(coverage.line_percent, coverage.line_total)
        t_b_pct = _format_percent(coverage.branch_percent, coverage.branch_total)
        out.append(
            f"| {t_badge} | **Total** | **{_format_count(coverage.line_covered, coverage.line_total)}** | "
            f"**{t_l_pct}** | **{_format_count(coverage.branch_covered, coverage.branch_total)}** | "
            f"**{t_b_pct}** | — |"
        )
        out.append("")
        out.append("</details>")
        out.append("")

    if run_url:
        out.append(
            f"📦 Full line-by-line HTML report (`tandem-core-coverage-html`) is attached to the [workflow run]({run_url})."
        )
        out.append("")

    return "\n".join(out)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coverage-json", type=Path, help="Path to gcovr --json output")
    parser.add_argument("--junit-xml", type=Path, help="Path to JUnit XML test report")
    parser.add_argument("--base-ref", type=str, help="Git base ref/SHA for PR diff coverage")
    parser.add_argument("--run-url", type=str, help="GitHub Actions workflow run URL")
    parser.add_argument("--marker", type=str, help="HTML comment marker for sticky PR comments")
    parser.add_argument("--output", type=Path, help="Output Markdown path (defaults to stdout)")
    args = parser.parse_args(argv[1:])

    junit: JunitSummary | None = None
    if args.junit_xml is not None and args.junit_xml.is_file():
        try:
            junit = parse_junit_xml(args.junit_xml)
        except (OSError, ET.ParseError, ValueError) as exc:
            print(f"Error: failed to parse JUnit XML {args.junit_xml}: {exc}", file=sys.stderr)
            return 1

    coverage: CoverageReport | None = None
    if args.coverage_json is not None and args.coverage_json.is_file():
        try:
            coverage = parse_gcovr_json(args.coverage_json)
        except (OSError, json.JSONDecodeError, KeyError, TypeError, ValueError) as exc:
            print(
                f"Error: failed to parse coverage JSON {args.coverage_json}: {exc}",
                file=sys.stderr,
            )
            return 1

    changed_lines: dict[str, set[int]] | None = None
    if args.base_ref:
        changed_lines = collect_changed_lines(args.base_ref)

    report = render_report(
        junit=junit,
        coverage=coverage,
        changed_lines=changed_lines,
        run_url=args.run_url,
        marker=args.marker,
    )

    if args.output is not None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
