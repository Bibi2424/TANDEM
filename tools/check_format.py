"""Check or apply clang-format across T.A.N.D.E.M. C/C++ source and header files."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import sysconfig
from typing import Optional

PINNED_CLANG_FORMAT_VERSION = "23.1.5"
TARGET_DIRS = ("include", "lib", "src", "test")
CPP_EXTENSIONS = {".c", ".h", ".cpp", ".hpp"}
SKIP_DIRS = {".git", ".pio", "build"}
CHUNK_SIZE = 50
VERSION_RE = re.compile(r"clang-format version (\d+\.\d+\.\d+)")


def find_clang_format(override: Optional[str]) -> Optional[Path]:
    """Locate the clang-format executable from an override, PATH, or Python Scripts dirs."""
    if override:
        which_override = shutil.which(override)
        if which_override:
            return Path(which_override)
        candidate = Path(override)
        return candidate if candidate.is_file() else None

    which_path = shutil.which("clang-format")
    if which_path:
        return Path(which_path)

    exe_dir = Path(sys.executable).resolve().parent
    candidate_dirs: list[Path] = [exe_dir, exe_dir / "Scripts"]
    for scheme in (None, "nt_user", "posix_user"):
        try:
            scripts_dir = (
                sysconfig.get_path("scripts", scheme=scheme)
                if scheme
                else sysconfig.get_path("scripts")
            )
            if scripts_dir:
                candidate_dirs.append(Path(scripts_dir))
        except (KeyError, ValueError):
            continue

    seen: set[Path] = set()
    for directory in candidate_dirs:
        if directory in seen:
            continue
        seen.add(directory)
        for binary_name in ("clang-format.exe", "clang-format"):
            candidate = directory / binary_name
            if candidate.is_file():
                return candidate
    return None


def detect_version(cf_bin: Path) -> Optional[str]:
    """Return the X.Y.Z version string reported by `clang-format --version`."""
    try:
        output = subprocess.check_output(
            [str(cf_bin), "--version"],
            stderr=subprocess.STDOUT,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    match = VERSION_RE.search(output)
    return match.group(1) if match else None


def collect_cpp_files(repo_root: Path) -> list[str]:
    """Collect sorted POSIX-relative C/C++ paths under TARGET_DIRS."""
    collected: list[str] = []
    for root_name in TARGET_DIRS:
        root_dir = repo_root / root_name
        if not root_dir.is_dir():
            continue
        for path in root_dir.rglob("*"):
            if any(part in SKIP_DIRS for part in path.parts):
                continue
            if path.is_file() and path.suffix.lower() in CPP_EXTENSIONS:
                collected.append(path.relative_to(repo_root).as_posix())
    collected.sort()
    return collected


def run_clang_format(cf_bin: Path, repo_root: Path, files: list[str], fix: bool) -> int:
    """Run clang-format in check or fix mode in chunks and return a process exit code."""
    flags = ["--style=file", "-i"] if fix else ["--style=file", "--dry-run", "--Werror"]
    exit_code = 0
    for idx in range(0, len(files), CHUNK_SIZE):
        batch = files[idx : idx + CHUNK_SIZE]
        result = subprocess.run([str(cf_bin), *flags, *batch], cwd=repo_root)
        if result.returncode != 0:
            exit_code = result.returncode
    return exit_code


def build_parser() -> argparse.ArgumentParser:
    """Construct the CLI argument parser."""
    parser = argparse.ArgumentParser(
        description="Check or auto-format T.A.N.D.E.M. C/C++ files using .clang-format."
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Format C/C++ files in-place instead of checking with --dry-run --Werror.",
    )
    parser.add_argument(
        "--strict-version",
        action="store_true",
        help=f"Fail if clang-format version does not match {PINNED_CLANG_FORMAT_VERSION}.",
    )
    parser.add_argument(
        "--print-version",
        action="store_true",
        help="Print the pinned clang-format version and exit.",
    )
    parser.add_argument(
        "--clang-format",
        dest="clang_format_path",
        default=None,
        help="Explicit path or command name for the clang-format binary.",
    )
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    """Entry point for C++ formatting verification and auto-fixing."""
    args = build_parser().parse_args(argv)
    if args.print_version:
        print(PINNED_CLANG_FORMAT_VERSION)
        return 0

    cf_bin = find_clang_format(args.clang_format_path)
    if cf_bin is None:
        print(
            "Error: clang-format executable not found.\n"
            f"Install the pinned version with: python -m pip install clang-format=={PINNED_CLANG_FORMAT_VERSION}",
            file=sys.stderr,
        )
        return 1

    detected_version = detect_version(cf_bin)
    if detected_version != PINNED_CLANG_FORMAT_VERSION:
        version_desc = detected_version or "unknown"
        msg = (
            f"clang-format version mismatch: found {version_desc} at {cf_bin}, "
            f"expected {PINNED_CLANG_FORMAT_VERSION}. "
            f"Install via: python -m pip install clang-format=={PINNED_CLANG_FORMAT_VERSION}"
        )
        if args.strict_version:
            print(f"Error: {msg}", file=sys.stderr)
            return 2
        print(f"Warning: {msg}", file=sys.stderr)

    repo_root = Path(__file__).resolve().parent.parent
    files = collect_cpp_files(repo_root)
    if not files:
        print(f"Error: no C/C++ files found under {', '.join(TARGET_DIRS)}.", file=sys.stderr)
        return 1

    rc = run_clang_format(cf_bin, repo_root, files, fix=args.fix)
    version_label = detected_version or "unknown"
    if rc != 0:
        if not args.fix:
            print(
                "\nFormatting check failed. Run: python tools/check_format.py --fix",
                file=sys.stderr,
            )
        return rc

    if args.fix:
        print(f"Formatted {len(files)} C/C++ files with clang-format {version_label}.")
    else:
        print(f"Checked {len(files)} C/C++ files with clang-format {version_label} — OK.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
