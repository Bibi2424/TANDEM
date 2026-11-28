# SPDX-License-Identifier: GPL-3.0-only
import argparse
import json
import os
import re
import subprocess
import sys
from typing import Optional

SEMVER_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)"
    r"(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?"
    r"(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)

TARGET_SUFFIX = "lib/tandem_core/src/core_info.cpp"
DEFAULT_BASE_VERSION = "0.1.0"


def _normalize_semver(raw: str) -> Optional[str]:
    candidate = raw.strip()
    if candidate[:1] in ("v", "V"):
        candidate = candidate[1:]
    if candidate and SEMVER_RE.fullmatch(candidate):
        return candidate
    return None


def _run_git(project_dir: str, args: list) -> Optional[str]:
    try:
        out = subprocess.check_output(
            ["git"] + args,
            cwd=project_dir,
            stderr=subprocess.DEVNULL,
            timeout=3,
        )
        return out.decode("utf-8", errors="ignore").strip()
    except Exception:
        return None


def resolve_git_commit(project_dir: str) -> str:
    sha = _run_git(project_dir, ["rev-parse", "--short", "HEAD"])
    if sha and re.fullmatch(r"[0-9a-fA-F]{4,16}", sha):
        return sha
    return "unknown"


def _resolve_base_version(project_dir: str) -> str:
    for pattern in ("v[0-9]*", "[0-9]*"):
        tag = _run_git(
            project_dir,
            ["describe", "--tags", "--abbrev=0", "--match", pattern, "HEAD"],
        )
        if tag:
            normalized = _normalize_semver(tag)
            if normalized:
                match = SEMVER_RE.fullmatch(normalized)
                if match:
                    return f"{match.group(1)}.{match.group(2)}.{match.group(3)}"

    for candidate_path in (
        os.path.join(project_dir, "lib", "tandem_core", "library.json"),
        os.path.join(project_dir, "firmware", "lib", "tandem_core", "library.json"),
    ):
        try:
            with open(candidate_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            normalized = _normalize_semver(str(data.get("version", "")))
            if normalized:
                match = SEMVER_RE.fullmatch(normalized)
                if match:
                    return f"{match.group(1)}.{match.group(2)}.{match.group(3)}"
        except Exception:
            pass

    return DEFAULT_BASE_VERSION


def resolve_fw_version(
    project_dir: str, commit: str, env_override: Optional[str] = None
) -> str:
    raw_override = (
        env_override
        if env_override is not None
        else os.environ.get("TANDEM_FW_VERSION", "")
    )
    if raw_override.strip():
        normalized = _normalize_semver(raw_override)
        if normalized:
            return normalized
        print(
            f"Warning: ignoring invalid TANDEM_FW_VERSION override '{raw_override.strip()}' (expected SemVer).",
            file=sys.stderr,
        )

    exact_tags = _run_git(
        project_dir, ["tag", "--points-at", "HEAD", "--sort=-v:refname"]
    )
    if exact_tags:
        for line in exact_tags.splitlines():
            normalized = _normalize_semver(line)
            if normalized:
                return normalized

    base = _resolve_base_version(project_dir)
    if commit != "unknown":
        return f"{base}-dev+{commit}"
    return f"{base}-dev"


def main(argv: Optional[list] = None) -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    parser = argparse.ArgumentParser(
        description="Resolve Tandem Controls firmware SemVer and Git commit metadata."
    )
    parser.add_argument(
        "--project-dir",
        default=repo_root,
        help="Path to the project Git repository (default: repo root).",
    )
    parser.add_argument(
        "--print-version",
        action="store_true",
        help="Print resolved TANDEM_FW_VERSION string and exit.",
    )
    parser.add_argument(
        "--print-commit",
        action="store_true",
        help="Print resolved TANDEM_GIT_COMMIT short hash and exit.",
    )
    args = parser.parse_args(argv)

    commit = resolve_git_commit(args.project_dir)
    if args.print_commit and not args.print_version:
        print(commit)
        return 0
    if args.print_version and not args.print_commit:
        print(resolve_fw_version(args.project_dir, commit))
        return 0

    fw_version = resolve_fw_version(args.project_dir, commit)
    print(f"TANDEM_FW_VERSION={fw_version}")
    print(f"TANDEM_GIT_COMMIT={commit}")
    return 0


if "Import" in globals():
    Import("env")

    _project_dir = env.subst("$PROJECT_DIR")
    _commit = resolve_git_commit(_project_dir)
    _fw_version = resolve_fw_version(_project_dir, _commit)

    def inject_version_defines(target_env, node):
        """Attach TANDEM_FW_VERSION and TANDEM_GIT_COMMIT only to core_info.cpp."""
        src_path = node.srcnode().get_path().replace("\\", "/")
        if src_path != TARGET_SUFFIX and not src_path.endswith("/" + TARGET_SUFFIX):
            return node

        node_env = target_env.Clone()
        node_env.ProcessUnFlags(node_env.get("BUILD_UNFLAGS"))
        node_env.Append(
            CPPDEFINES=[
                ("TANDEM_FW_VERSION", node_env.StringifyMacro(_fw_version)),
                ("TANDEM_GIT_COMMIT", node_env.StringifyMacro(_commit)),
            ]
        )
        return node_env.Object(node)

    env.AddBuildMiddleware(inject_version_defines, "*core_info.cpp")
elif __name__ == "__main__":
    raise SystemExit(main())
